#!/bin/bash
set -e

################################################################################
# Minimal Build Script for Lotio
#
# This script builds lotio with ZERO bundled dependencies!
# All libraries use system/Homebrew versions - nothing is bundled in Skia.
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
# NOT NEEDED (removed from minimal build):
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
#
################################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia"
SKIA_LIB_DIR="$SKIA_ROOT/out/Release"
SRC_DIR="$PROJECT_ROOT/src"

echo "🔨 Building Lotio (Zero Bundled Dependencies)"
echo "=============================================="
echo "All libraries use system/Homebrew - nothing is bundled!"
echo ""

# Detect OS and architecture
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
# Step 0: Set up minimal Skia (no bundled dependencies - all use system libraries)
################################################################################

echo "🗑️  Step 0: Setting up minimal Skia structure..."
echo ""

THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
# Use system temp directory instead of project root
SKIA_TEMP_DIR=$(mktemp -d -t skia_clone_XXXXXX)

# Cleanup function for temp directory
cleanup_skia_temp() {
    if [ -n "$SKIA_TEMP_DIR" ] && [ -d "$SKIA_TEMP_DIR" ]; then
        rm -rf "$SKIA_TEMP_DIR" 2>/dev/null || true
    fi
}
trap cleanup_skia_temp EXIT

# Remove existing third_party if it exists
if [ -d "$THIRD_PARTY_DIR" ]; then
    echo "   Removing existing third_party directory..."
    rm -rf "$THIRD_PARTY_DIR"
fi

# Create fresh structure
mkdir -p "$THIRD_PARTY_DIR/skia"
cd "$THIRD_PARTY_DIR"

# Clone Skia repository
echo "   Cloning Skia repository (this may take a while)..."
cd "$SKIA_TEMP_DIR"
git clone --depth 1 https://skia.googlesource.com/skia.git
echo "   ✅ Skia cloned"

# Copy only essential Skia files (exclude .git, tests, docs, etc.)
echo "   Copying essential Skia files..."
cd skia

# Essential directories
cp -r include "$THIRD_PARTY_DIR/skia/"
cp -r modules "$THIRD_PARTY_DIR/skia/"
cp -r src "$THIRD_PARTY_DIR/skia/"
cp -r gn "$THIRD_PARTY_DIR/skia/"
cp -r bin "$THIRD_PARTY_DIR/skia/"
cp -r build_overrides "$THIRD_PARTY_DIR/skia/"
cp -r toolchain "$THIRD_PARTY_DIR/skia/"
# Copy tools directory (needed for git-sync-deps in WASM builds)
if [ -d "tools" ]; then
    cp -r tools "$THIRD_PARTY_DIR/skia/"
fi
# Copy experimental directory (needed for BUILD.gn references, even if we don't use it)
if [ -d "experimental" ]; then
    cp -r experimental "$THIRD_PARTY_DIR/skia/"
fi

# Essential files (DEPS will be created as minimal version)
cp BUILD.gn "$THIRD_PARTY_DIR/skia/"
cp LICENSE "$THIRD_PARTY_DIR/skia/"
cp README "$THIRD_PARTY_DIR/skia/" 2>/dev/null || true
cp .gn "$THIRD_PARTY_DIR/skia/" 2>/dev/null || true

# Create third_party structure (but not externals yet)
mkdir -p "$THIRD_PARTY_DIR/skia/third_party/externals"

# Copy third_party config files (but not externals)
if [ -d "third_party" ]; then
    find third_party -type f -name "*.gn" -o -name "*.gni" | while read -r file; do
        dir=$(dirname "$file")
        mkdir -p "$THIRD_PARTY_DIR/skia/$dir"
        cp "$file" "$THIRD_PARTY_DIR/skia/$file"
    done
fi

echo "   ✅ Essential Skia files copied"

# Create minimal DEPS file
echo "   Creating minimal DEPS file..."
cat > "$THIRD_PARTY_DIR/skia/DEPS" << 'EOF'
vars = {
  'checkout_android': False,
  'checkout_angle': False,
  'checkout_dawn': False,
  'checkout_emsdk': False,
  'checkout_ios': False,
  'checkout_nacl': False,
  'checkout_oculus_sdk': False,
  'checkout_skqp': False,
  'checkout_win_toolchain': False,
}

deps = {}

# No hooks needed - we manually fetch GN using bin/fetch-gn (which downloads directly from chrome-infra-packages)
hooks = []
EOF

echo "   ✅ Minimal DEPS file created (no dependencies - all use system libraries)"

# No dependencies to fetch - all libraries are system/Homebrew
echo "   ✅ No dependencies to fetch (all libraries use system/Homebrew versions)"
echo ""

################################################################################
# Step 1: Build Skia with minimal dependencies
################################################################################

echo "📦 Step 1: Building Skia with minimal dependencies..."
echo ""

cd "$SKIA_ROOT"

# Fetch GN if needed
if [ ! -f "bin/gn" ]; then
    echo "   Fetching GN binary..."
    python3 bin/fetch-gn
fi

# Generate build files with minimal configuration
echo "   Generating build files..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    GN_ARGS="target_cpu=\"${TARGET_CPU}\" \
        is_official_build=true \
        is_debug=false \
        skia_enable_skottie=true \
        skia_enable_fontmgr_fontconfig=true \
        skia_enable_fontmgr_custom_directory=true \
        skia_use_freetype=true \
        skia_use_libpng_encode=true \
        skia_use_libpng_decode=true \
        skia_use_libjpeg_turbo_decode=false \
        skia_use_libjpeg_turbo_encode=false \
        skia_use_libwebp_decode=false \
        skia_use_libwebp_encode=false \
        skia_use_wuffs=false \
        skia_use_expat=false \
        skia_enable_pdf=false \
        skia_use_dawn=false \
        skia_use_angle=false \
        skia_use_vulkan=false \
        skia_enable_ganesh=false \
        skia_enable_graphite=false \
        skia_enable_tools=false \
        skia_use_dng_sdk=false \
        skia_use_system_icu=true \
        skia_use_ffmpeg=false \
        extra_cflags=[\"-O3\", \"-I$HOMEBREW_PREFIX/include\", \"-I$FREETYPE_INCLUDE\", \"-I$HARFBUZZ_INCLUDE\", \"-I$ICU_INCLUDE\"]"
else
    GN_ARGS="target_cpu=\"${TARGET_CPU}\" \
        is_official_build=true \
        is_debug=false \
        skia_enable_skottie=true \
        skia_enable_fontmgr_fontconfig=true \
        skia_enable_fontmgr_custom_directory=true \
        skia_use_freetype=true \
        skia_use_libpng_encode=true \
        skia_use_libpng_decode=true \
        skia_use_libjpeg_turbo_decode=false \
        skia_use_libjpeg_turbo_encode=false \
        skia_use_libwebp_decode=false \
        skia_use_libwebp_encode=false \
        skia_use_wuffs=false \
        skia_use_expat=false \
        skia_enable_pdf=false \
        skia_use_dawn=false \
        skia_use_angle=false \
        skia_use_vulkan=false \
        skia_enable_ganesh=false \
        skia_enable_graphite=false \
        skia_enable_tools=false \
        skia_use_dng_sdk=false \
        skia_use_system_icu=true \
        skia_use_ffmpeg=false \
        extra_cflags=[\"-O3\", \"-Wno-psabi\", \"-I/usr/include\", \"-I/usr/include/freetype2\", \"-I/usr/include/harfbuzz\", \"-I/usr/include/fontconfig\", \"-I/usr/include/unicode\"]"
fi

./bin/gn gen out/Release --args="$GN_ARGS"

# Build gen/skia.h first (required)
echo "   Building gen/skia.h..."
cd out/Release
ninja gen/skia.h
cd ../..

# Build only the libraries we need
echo "   Building Skia libraries (this may take a while)..."
# Use all CPU cores explicitly for faster compilation
NINJA_JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)
echo "   Using $NINJA_JOBS parallel jobs..."
ninja -C out/Release -j$NINJA_JOBS :skia \
    modules/skottie:skottie \
    modules/skparagraph:skparagraph \
    modules/sksg:sksg \
    modules/skshaper:skshaper \
    modules/skunicode:skunicode \
    modules/skresources:skresources \
    modules/jsonreader:jsonreader

echo "✅ Skia built successfully"
echo ""

################################################################################
# Step 2: Build lotio
################################################################################

cd "$PROJECT_ROOT"

echo "📝 Step 2: Building lotio..."
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
    "$SRC_DIR/text/text_config.cpp"
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

# Get version from environment or default to "dev"
VERSION_NUMBER="${VERSION:-dev}"
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

