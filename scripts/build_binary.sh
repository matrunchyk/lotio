#!/bin/bash
set -e

################################################################################
# Binary Build Script for Lotio
#
# This script builds lotio with ZERO bundled dependencies!
# All libraries use system/Homebrew versions - nothing is bundled in Skia.
#
# Usage:
#   scripts/build_binary.sh [--target-cpu=arm64|x64] [--skip-skia] [--skip-skia-setup]
#
# For Docker multi-arch builds:
#   TARGET_CPU=arm64 scripts/build_binary.sh --skip-skia-setup
#   TARGET_CPU=x64 scripts/build_binary.sh --skip-skia-setup
#
# REQUIRED DEPENDENCIES: NONE (all use system libraries)
# =====================================================
# All dependencies are system/Homebrew libraries - nothing is bundled in Skia!
#
# SYSTEM LIBRARIES (used but not bundled):
# ========================================
# - freetype, harfbuzz, libpng, icu: Use system/Homebrew versions
# - zlib: System library (used by PNG encoding, available on all platforms)
# - expat: Linked because fontconfig requires it (system on macOS, system on Linux)
#
# NOT NEEDED (removed from binary build):
# =======================================
# - GPU backends: angle2, dawn, swiftshader, vulkan-*, opengl-registry
# - Other codecs: libavif, libjxl, libgav1
# - WASM toolchain: emsdk (only needed for JS/WASM builds, not native)
# - Other: brotli, dng_sdk, imgui, libyuv, oboe, perfetto, unicodetools, etc.
#
# PLATFORM-SPECIFIC NOTES:
# ========================
# macOS:
#   - Uses Homebrew packages (fontconfig, freetype, harfbuzz, libpng, icu4c - any version)
#   - Fontconfig links to system expat (/usr/lib/libexpat.1.dylib)
#   - ICU from Homebrew (any version 44-100, auto-detected) - Skia supports multiple ICU versions
#   - zlib: System library (/usr/lib/libz.1.dylib)
#   - Skia uses system libraries via skia_use_system_icu=true
#
# Linux (Ubuntu/Debian):
#   - Uses system packages via apt (libfontconfig1-dev, libexpat1-dev, etc.)
#   - All libraries (freetype, harfbuzz, libpng, icu, zlib) are system packages
#   - Libraries in /usr/lib or /usr/lib/x86_64-linux-gnu (or arm64 equivalent)
#   - ICU version may vary (system package, not necessarily 77)
#
# ENVIRONMENT VARIABLES:
# ======================
# - HOMEBREW_PREFIX: Homebrew installation path (macOS, auto-detected)
# - ICU_LIB: ICU library path (auto-detected per platform, any version 44-100 works)
# - TARGET_CPU: Target CPU architecture (arm64 or x64, auto-detected if not set)
# - VERSION: Version string for the binary (default: "dev")
#
################################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia"
SKIA_LIB_DIR="$SKIA_ROOT/out/Release"
SRC_DIR="$PROJECT_ROOT/src"

# Parse arguments
SKIP_SKIA=false
SKIP_SKIA_SETUP=false
TARGET_CPU=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-skia)
            SKIP_SKIA=true
            shift
            ;;
        --skip-skia-setup)
            SKIP_SKIA_SETUP=true
            shift
            ;;
        --target-cpu=*)
            TARGET_CPU="${1#*=}"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "🔨 Building Lotio (Zero Bundled Dependencies)"
echo "=============================================="
echo "All libraries use system/Homebrew - nothing is bundled!"
echo ""

# Detect OS and architecture
# Use TARGET_CPU from environment if set, otherwise auto-detect
if [[ -z "$TARGET_CPU" ]]; then
    if [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
        ARCH=$(uname -m)
        if [[ "$ARCH" == "arm64" ]]; then
            TARGET_CPU="arm64"
        else
            TARGET_CPU="x64"
        fi
    else
        OS="linux"
        ARCH=$(uname -m)
        if [[ "$ARCH" == "aarch64" ]] || [[ "$ARCH" == "arm64" ]]; then
            TARGET_CPU="arm64"
        else
            TARGET_CPU="x64"
        fi
    fi
else
    # TARGET_CPU was set via argument or environment
    if [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
    else
        OS="linux"
    fi
fi

echo "📋 Detected: $OS ($TARGET_CPU)"
echo ""

# Check for ccache (optional but recommended for faster rebuilds)
if command -v ccache >/dev/null 2>&1; then
    export CC="ccache clang"
    export CXX="ccache clang++"
    echo "✅ Using ccache for faster compilation"
    echo ""
else
    echo "⚠️  WARNING: ccache not found. Install for faster rebuilds:"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "   brew install ccache"
    else
        echo "   sudo apt-get install ccache  # or equivalent for your Linux distro"
    fi
    echo ""
fi

# Set up Homebrew paths (macOS)
if [[ "$OSTYPE" == "darwin"* ]]; then
    HOMEBREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/opt/homebrew")
    FREETYPE_INCLUDE="$HOMEBREW_PREFIX/include/freetype2"
    HARFBUZZ_INCLUDE="$HOMEBREW_PREFIX/include/harfbuzz"
    
    # Auto-detect ICU version (Skia supports ICU 44-100, so any version works)
    # Try to find any installed ICU version (prefer newer versions)
    ICU_CELLAR=""
    ICU_VERSION=""
    
    # Check for versioned ICU installations (icu4c@77, icu4c@78, etc.)
    for icu_dir in $(find /opt/homebrew/Cellar -maxdepth 1 -name "icu4c@*" -type d 2>/dev/null | sort -V -r); do
        if [ -d "$icu_dir" ] && [ -d "$icu_dir/lib" ] && [ -d "$icu_dir/include" ]; then
            ICU_CELLAR="$icu_dir"
            ICU_VERSION=$(basename "$icu_dir" | sed 's/icu4c@//')
            break
        fi
    done
    
    # Fallback: try unversioned icu4c
    if [ -z "$ICU_CELLAR" ]; then
        ICU_UNVERSIONED=$(find /opt/homebrew/Cellar -maxdepth 1 -name "icu4c" -type d 2>/dev/null | head -1)
        if [ -n "$ICU_UNVERSIONED" ] && [ -d "$ICU_UNVERSIONED/lib" ] && [ -d "$ICU_UNVERSIONED/include" ]; then
            ICU_CELLAR="$ICU_UNVERSIONED"
            ICU_VERSION="system"
        fi
    fi
    
    # Fallback: try brew --prefix
    if [ -z "$ICU_CELLAR" ]; then
        ICU_PREFIX=$(brew --prefix icu4c 2>/dev/null || echo "")
        if [ -n "$ICU_PREFIX" ] && [ -d "$ICU_PREFIX/lib" ] && [ -d "$ICU_PREFIX/include" ]; then
            ICU_CELLAR="$ICU_PREFIX"
            ICU_VERSION="system"
        fi
    fi
    
    if [ -z "$ICU_CELLAR" ] || [ ! -d "$ICU_CELLAR/lib" ] || [ ! -d "$ICU_CELLAR/include" ]; then
        echo "❌ ERROR: ICU libraries not found"
        echo "   Please install: brew install icu4c"
        echo "   Or a specific version: brew install icu4c@77"
        exit 1
    fi
    
    ICU_LIB="$ICU_CELLAR/lib"
    ICU_INCLUDE="$ICU_CELLAR/include"
    
    echo "✅ Using Homebrew paths:"
    echo "   Prefix: $HOMEBREW_PREFIX"
    echo "   ICU version: $ICU_VERSION (auto-detected)"
    echo "   ICU lib: $ICU_LIB"
    echo "   ICU include: $ICU_INCLUDE"
    echo ""
fi

# Check if source files exist
if [ ! -d "$SRC_DIR" ]; then
    echo "❌ ERROR: Source directory not found at $SRC_DIR"
    exit 1
fi

################################################################################
# Step 0: Build Skia (using shared script)
################################################################################

if [[ "$SKIP_SKIA" == "false" ]]; then
    if [[ "$SKIP_SKIA_SETUP" == "true" ]]; then
        "$SCRIPT_DIR/_build_skia.sh" --target=binary --target-cpu="$TARGET_CPU" --skip-setup
    else
        "$SCRIPT_DIR/_build_skia.sh" --target=binary --target-cpu="$TARGET_CPU"
    fi
else
    echo "⏭️  Skipping Skia build (--skip-skia specified)"
    echo ""
fi

################################################################################
# Step 1: Build lotio
################################################################################

cd "$PROJECT_ROOT"

echo "📝 Step 1: Building lotio..."
echo ""

# Set up library paths
if [[ "$OSTYPE" == "darwin"* ]]; then
    PNG_PREFIX=$(brew --prefix libpng 2>/dev/null || echo "$HOMEBREW_PREFIX")
    HARFBUZZ_PREFIX=$(brew --prefix harfbuzz 2>/dev/null || echo "$HOMEBREW_PREFIX")
    FREETYPE_PREFIX=$(brew --prefix freetype 2>/dev/null || echo "$HOMEBREW_PREFIX")
    FONTCONFIG_PREFIX=$(brew --prefix fontconfig 2>/dev/null || echo "$HOMEBREW_PREFIX")
    
    # Ensure ICU_LIB is set (re-find if not already set)
    if [ -z "$ICU_LIB" ] || [ ! -d "$ICU_LIB" ]; then
        # Re-detect ICU (same logic as earlier in script)
        ICU_CELLAR=""
        for icu_dir in $(find /opt/homebrew/Cellar -maxdepth 1 -name "icu4c@*" -type d 2>/dev/null | sort -V -r); do
            if [ -d "$icu_dir/lib" ] && [ -d "$icu_dir/include" ]; then
                ICU_CELLAR="$icu_dir"
                break
            fi
        done
        if [ -z "$ICU_CELLAR" ]; then
            ICU_PREFIX=$(brew --prefix icu4c 2>/dev/null || echo "")
            if [ -n "$ICU_PREFIX" ] && [ -d "$ICU_PREFIX/lib" ]; then
                ICU_CELLAR="$ICU_PREFIX"
            fi
        fi
        if [ -z "$ICU_CELLAR" ] || [ ! -d "$ICU_CELLAR/lib" ]; then
            echo "❌ ERROR: ICU libraries not found"
            echo "   Please install: brew install icu4c"
            exit 1
        fi
        ICU_LIB="$ICU_CELLAR/lib"
        ICU_INCLUDE="$ICU_CELLAR/include"
    fi
else
    # Linux: Use system packages (installed via apt)
    # Required packages: libfontconfig1-dev, libexpat1-dev, libfreetype6-dev, 
    #                    libharfbuzz-dev, libpng-dev, libicu-dev
    # Libraries are in standard system paths (/usr/lib, /usr/lib64 for x86_64)
    PNG_PREFIX=""
    HARFBUZZ_PREFIX=""
    FREETYPE_PREFIX=""
    FONTCONFIG_PREFIX=""
    
    # ICU: Try to find system ICU (version may vary)
    # On Ubuntu/Debian, ICU is typically in /usr/lib or /usr/lib/x86_64-linux-gnu
    if [ -d "/usr/lib/x86_64-linux-gnu" ]; then
        ICU_LIB="/usr/lib/x86_64-linux-gnu"
    elif [ -d "/usr/lib64" ]; then
        ICU_LIB="/usr/lib64"
    else
        ICU_LIB="/usr/lib"
    fi
fi

# Create temp include structure for <skia/...> includes
TEMP_INCLUDE_DIR=$(mktemp -d)
mkdir -p "$TEMP_INCLUDE_DIR/skia"
ln -sf "$SKIA_ROOT/include/core" "$TEMP_INCLUDE_DIR/skia/core"
ln -sf "$SKIA_ROOT/include" "$TEMP_INCLUDE_DIR/skia/include"
ln -sf "$SKIA_ROOT/modules" "$TEMP_INCLUDE_DIR/skia/modules"

# Cleanup function
cleanup_temp_include() {
    rm -rf "$TEMP_INCLUDE_DIR" 2>/dev/null || true
}
trap cleanup_temp_include EXIT

# Source files for library (excluding main.cpp)
LIBRARY_SOURCES=(
    "$SRC_DIR/core/argument_parser.cpp"
    "$SRC_DIR/core/animation_setup.cpp"
    "$SRC_DIR/core/frame_encoder.cpp"
    "$SRC_DIR/core/renderer.cpp"
    "$SRC_DIR/utils/crash_handler.cpp"
    "$SRC_DIR/utils/logging.cpp"
    "$SRC_DIR/utils/string_utils.cpp"
    "$SRC_DIR/utils/version.cpp"
    "$SRC_DIR/text/layer_overrides.cpp"
    "$SRC_DIR/text/text_processor.cpp"
    "$SRC_DIR/text/font_utils.cpp"
    "$SRC_DIR/text/text_sizing.cpp"
    "$SRC_DIR/text/json_manipulation.cpp"
)

# Main entry point (separate from library)
MAIN_SOURCE="$SRC_DIR/main.cpp"

# Output files
TARGET="$PROJECT_ROOT/lotio"
LIBRARY_TARGET="$PROJECT_ROOT/liblotio.a"

# Get version from environment or generate dev version with build datetime
if [ -z "$VERSION" ]; then
    # Generate dev version with build datetime (build-time, not runtime)
    BUILD_DATETIME=$(date +"%Y%m%d-%H%M%S")
    VERSION_NUMBER="dev-${BUILD_DATETIME}"
else
    VERSION_NUMBER="$VERSION"
fi
VERSION_DEFINE="-DVERSION=\"${VERSION_NUMBER}\""

# Compile library source files
echo "   Compiling library source files..."
LIBRARY_OBJECTS=()
for src in "${LIBRARY_SOURCES[@]}"; do
    if [ ! -f "$src" ]; then
        echo "⚠️  Warning: Source file not found: $src"
        continue
    fi
    obj="${src%.cpp}.o"
    echo "      Compiling: $(basename $src)"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        g++ -std=c++17 -O3 -DNDEBUG $VERSION_DEFINE \
            -I"$SKIA_ROOT" -I"$TEMP_INCLUDE_DIR" -I"$SRC_DIR" \
            -I"$HOMEBREW_PREFIX/include" -I"$FREETYPE_INCLUDE" -I"$ICU_INCLUDE" -I"$HARFBUZZ_INCLUDE" \
            -c "$src" -o "$obj"
    else
        g++ -std=c++17 -O3 -DNDEBUG $VERSION_DEFINE \
            -I"$SKIA_ROOT" -I"$TEMP_INCLUDE_DIR" -I"$SRC_DIR" \
            -c "$src" -o "$obj"
    fi
    LIBRARY_OBJECTS+=("$obj")
done

# Create static library
echo ""
echo "   Creating liblotio.a..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: use libtool
    libtool -static -o "$LIBRARY_TARGET" "${LIBRARY_OBJECTS[@]}" || {
        echo "❌ Failed to create liblotio.a"
        exit 1
    }
else
    # Linux: use ar
    ar rcs "$LIBRARY_TARGET" "${LIBRARY_OBJECTS[@]}" || {
        echo "❌ Failed to create liblotio.a"
        exit 1
    }
fi
echo "   ✅ Created: $LIBRARY_TARGET"

# Compile main.cpp separately
echo ""
echo "   Compiling main entry point..."
if [ ! -f "$MAIN_SOURCE" ]; then
    echo "❌ Error: Main source file not found: $MAIN_SOURCE"
    exit 1
fi
MAIN_OBJECT="${MAIN_SOURCE%.cpp}.o"
echo "      Compiling: $(basename $MAIN_SOURCE)"
if [[ "$OSTYPE" == "darwin"* ]]; then
    g++ -std=c++17 -O3 -DNDEBUG $VERSION_DEFINE \
        -I"$SKIA_ROOT" -I"$TEMP_INCLUDE_DIR" -I"$SRC_DIR" \
        -I"$HOMEBREW_PREFIX/include" -I"$FREETYPE_INCLUDE" -I"$ICU_INCLUDE" -I"$HARFBUZZ_INCLUDE" \
        -c "$MAIN_SOURCE" -o "$MAIN_OBJECT"
else
    g++ -std=c++17 -O3 -DNDEBUG $VERSION_DEFINE \
        -I"$SKIA_ROOT" -I"$TEMP_INCLUDE_DIR" -I"$SRC_DIR" \
        -c "$MAIN_SOURCE" -o "$MAIN_OBJECT"
fi

# Link the binary
echo "   Linking binary..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    # Verify ICU_LIB is set and exists (re-find if needed)
    # Re-detect ICU paths if not already set (for linking step)
    if [ -z "$ICU_LIB" ] || [ ! -d "$ICU_LIB" ]; then
        # Auto-detect any ICU version
        ICU_CELLAR=""
        for icu_dir in $(find /opt/homebrew/Cellar -maxdepth 1 -name "icu4c@*" -type d 2>/dev/null | sort -V -r); do
            if [ -d "$icu_dir/lib" ] && [ -d "$icu_dir/include" ]; then
                ICU_CELLAR="$icu_dir"
                break
            fi
        done
        if [ -z "$ICU_CELLAR" ]; then
            ICU_PREFIX=$(brew --prefix icu4c 2>/dev/null || echo "")
            if [ -n "$ICU_PREFIX" ] && [ -d "$ICU_PREFIX/lib" ]; then
                ICU_CELLAR="$ICU_PREFIX"
            fi
        fi
        if [ -z "$ICU_CELLAR" ] || [ ! -d "$ICU_CELLAR/lib" ]; then
            echo "❌ ERROR: ICU libraries not found"
            echo "   Please install: brew install icu4c"
            exit 1
        fi
        ICU_LIB="$ICU_CELLAR/lib"
        ICU_INCLUDE="$ICU_CELLAR/include"
    fi
    # Ensure ICU_INCLUDE is set for linking step
    if [ -z "$ICU_INCLUDE" ]; then
        ICU_INCLUDE="$ICU_LIB/../include"
        if [ ! -d "$ICU_INCLUDE" ]; then
            echo "❌ ERROR: ICU headers not found"
            exit 1
        fi
    fi
    
    echo "   Using ICU libraries from: $ICU_LIB"
    
    # Link binary: main.o + liblotio.a + Skia libraries
    echo ""
    echo "   Linking binary..."
    g++ -std=c++17 -O3 -DNDEBUG \
        "$MAIN_OBJECT" "$LIBRARY_TARGET" \
        -L"$SKIA_LIB_DIR" -Wl,-rpath,"$SKIA_LIB_DIR" \
        -L"$PNG_PREFIX/lib" \
        -L"$HARFBUZZ_PREFIX/lib" \
        -L"$FREETYPE_PREFIX/lib" \
        -L"$FONTCONFIG_PREFIX/lib" \
        -L"$ICU_LIB" \
        "$SKIA_LIB_DIR/libskresources.a" \
        "$SKIA_LIB_DIR/libskparagraph.a" \
        "$SKIA_LIB_DIR/libskottie.a" \
        "$SKIA_LIB_DIR/libskshaper.a" \
        "$SKIA_LIB_DIR/libskunicode_icu.a" \
        "$SKIA_LIB_DIR/libskunicode_core.a" \
        "$SKIA_LIB_DIR/libsksg.a" \
        "$SKIA_LIB_DIR/libjsonreader.a" \
        -Wl,-force_load,"$SKIA_LIB_DIR/libskia.a" \
        -lfreetype -lpng -lharfbuzz \
        -L"$ICU_LIB" -licuuc -licui18n -licudata \
        -lz -lfontconfig -lexpat -lm -lpthread \
        -framework CoreFoundation -framework CoreGraphics -framework CoreText \
        -framework CoreServices -framework AppKit \
        -o "$TARGET"
else
    # Link binary: main.o + liblotio.a + Skia libraries
    echo ""
    echo "   Linking binary..."
    g++ -std=c++17 -O3 -DNDEBUG \
        "$MAIN_OBJECT" "$LIBRARY_TARGET" \
        -L"$SKIA_LIB_DIR" -Wl,-rpath,"$SKIA_LIB_DIR" \
        "$SKIA_LIB_DIR/libskresources.a" \
        "$SKIA_LIB_DIR/libskparagraph.a" \
        "$SKIA_LIB_DIR/libskottie.a" \
        "$SKIA_LIB_DIR/libskshaper.a" \
        "$SKIA_LIB_DIR/libskunicode_icu.a" \
        "$SKIA_LIB_DIR/libskunicode_core.a" \
        "$SKIA_LIB_DIR/libsksg.a" \
        "$SKIA_LIB_DIR/libjsonreader.a" \
        "$SKIA_LIB_DIR/libskia.a" \
        -lfreetype -lpng -lharfbuzz -licuuc -licui18n -licudata \
        -lz -lfontconfig -lexpat -lm -lpthread \
        -lX11 -lGL -lGLU \
        -o "$TARGET"
fi

# Cleanup
echo ""
echo "   Cleaning up object files..."
rm -f "${LIBRARY_OBJECTS[@]}"
rm -f "$MAIN_OBJECT"
cleanup_temp_include

echo ""
echo "✅ Build complete!"
echo ""
echo "📦 Output:"
echo "   Binary: $TARGET"
echo "   Library: $LIBRARY_TARGET"
echo ""
echo "🧪 Test it:"
echo "   ./lotio --help"
echo "   ./lotio --version"
echo ""

