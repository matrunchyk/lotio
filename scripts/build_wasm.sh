#!/bin/bash
set -e

################################################################################
# Unified WASM Build Script for Lotio
#
# This script builds both Skia and lotio for WebAssembly in a single command.
# It handles everything: dependencies, Skia build, and lotio compilation.
#
# WASM DEPENDENCIES (bundled, not system):
# ============================================
# - freetype (with freetype-no-type1 config for WASM)
# - libpng
# - brotli
#
# All other dependencies are disabled (JPEG, WebP, Wuffs, ICU, HarfBuzz, etc.)
################################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia"
SKIA_LIB_DIR="$SKIA_ROOT/out/Wasm"
SRC_DIR="$PROJECT_ROOT/src"

echo "🔨 Building Lotio for WebAssembly"
echo "=================================="
echo ""

# Check for Emscripten
if [ -z "$EMSDK" ]; then
    # Try Homebrew installation - check common paths
    EMCC_PATH=""
    if command -v emcc >/dev/null 2>&1; then
        EMCC_PATH=$(which emcc)
        echo "✅ Using Homebrew's emscripten (emcc found in PATH: $EMCC_PATH)"
    elif [ -f "/opt/homebrew/bin/emcc" ]; then
        EMCC_PATH="/opt/homebrew/bin/emcc"
        export PATH="/opt/homebrew/bin:$PATH"
        echo "✅ Using Homebrew's emscripten (found at: $EMCC_PATH)"
    elif [ -f "/usr/local/bin/emcc" ]; then
        EMCC_PATH="/usr/local/bin/emcc"
        export PATH="/usr/local/bin:$PATH"
        echo "✅ Using Homebrew's emscripten (found at: $EMCC_PATH)"
    else
        echo "❌ Emscripten not found. Please install via:"
        echo "   Option 1 (Recommended): brew install emscripten"
        echo "   Option 2: git clone https://github.com/emscripten-core/emsdk.git"
        echo "            cd emsdk && ./emsdk install latest && ./emsdk activate latest"
        echo "            source ./emsdk_env.sh"
        exit 1
    fi
else
    # Emscripten SDK installation - activate it
    echo "✅ Using Emscripten SDK at: $EMSDK"
    source "$EMSDK/emsdk_env.sh"
fi

################################################################################
# Step 1: Build Skia for WASM (using shared script)
################################################################################

# Check if Skia is already built for WASM
if [ -f "$SKIA_LIB_DIR/libskia.a" ]; then
    echo "✅ Skia already built for WebAssembly (skipping Skia build)"
    echo ""
else
    # Check if Skia structure exists
    if [ ! -d "$SKIA_ROOT" ] || [ ! -f "$SKIA_ROOT/bin/gn" ]; then
        "$SCRIPT_DIR/_build_skia.sh" --target=wasm
    else
        "$SCRIPT_DIR/_build_skia.sh" --target=wasm --skip-setup
    fi
fi

################################################################################
# Step 2: Build lotio for WASM
################################################################################

cd "$PROJECT_ROOT"

echo "📝 Step 2: Building lotio for WebAssembly..."
echo ""

# Emscripten compiler
CXX=em++

# Get version from environment or generate dev version with build datetime
if [ -z "$VERSION" ]; then
    # Generate dev version with build datetime (build-time, not runtime)
    BUILD_DATETIME=$(date +"%Y%m%d-%H%M%S")
    VERSION_NUMBER="dev-${BUILD_DATETIME}"
else
    VERSION_NUMBER="$VERSION"
fi
VERSION_DEFINE="-DVERSION=\"${VERSION_NUMBER}\""

# Compilation flags (used during -c compilation phase)
COMPILE_FLAGS="-std=c++17 -O3 -DNDEBUG -Wall -Wextra $VERSION_DEFINE"

# Linker flags (used during linking phase only)
# Note: -sUSE_FREETYPE and -sUSE_LIBPNG are linker flags but can be specified during compilation
LINK_FLAGS="-sUSE_FREETYPE=1 \
    -sUSE_LIBPNG=1 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_FUNCTIONS=_lotio_init,_lotio_get_info,_lotio_render_frame,_lotio_render_frame_png,_lotio_cleanup,_lotio_register_font,_lotio_get_version,_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,HEAP8,HEAPU8,HEAP32,HEAPF32,HEAPF64 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createLotioModule \
    --bind"

# Create temporary include structure for <skia/...> includes
# This matches the installed structure: include/skia/core/SkCanvas.h
TEMP_INCLUDE_DIR=$(mktemp -d)
mkdir -p "$TEMP_INCLUDE_DIR/skia"
# Create symlinks to match installed structure:
# - skia/core/ -> include/core/ (for <skia/core/SkCanvas.h>)
# - skia/modules/ -> modules/ (for <skia/modules/skottie/include/Skottie.h>)
ln -sf "$SKIA_ROOT/include/core" "$TEMP_INCLUDE_DIR/skia/core" 2>/dev/null || true
ln -sf "$SKIA_ROOT/include" "$TEMP_INCLUDE_DIR/skia/include" 2>/dev/null || true
ln -sf "$SKIA_ROOT/modules" "$TEMP_INCLUDE_DIR/skia/modules" 2>/dev/null || true

INCLUDES="-I$SKIA_ROOT -I$TEMP_INCLUDE_DIR -I$SRC_DIR"
LDFLAGS="-L$SKIA_LIB_DIR"

# Cleanup function for temp directory
cleanup_temp_include() {
    rm -rf "$TEMP_INCLUDE_DIR" 2>/dev/null || true
}
trap cleanup_temp_include EXIT

# Skia libraries (static)
# Required libraries (must exist)
REQUIRED_LIBS="skottie skia sksg skshaper skresources jsonreader"
# Optional libraries (may not exist if features are disabled)
OPTIONAL_LIBS="skparagraph skunicode"
# Dependency libraries (brotli is needed by skottie for WOFF2 font support)
DEPENDENCY_LIBS="brotli"

SKIA_LIBS=""
MISSING_REQUIRED=""

# Check required libraries
for lib in $REQUIRED_LIBS; do
    if [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
        SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.a"
    else
        MISSING_REQUIRED="$MISSING_REQUIRED $lib"
    fi
done

# Check optional libraries
for lib in $OPTIONAL_LIBS; do
    if [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
        SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.a"
    fi
done

# Check dependency libraries (add if they exist)
for lib in $DEPENDENCY_LIBS; do
    if [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
        SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.a"
    fi
done

# Fail if required libraries are missing
if [ -n "$MISSING_REQUIRED" ]; then
    echo "❌ Error: Required Skia libraries not found:"
    for lib in $MISSING_REQUIRED; do
        echo "   - $SKIA_LIB_DIR/lib${lib}.a"
    done
    echo ""
    echo "Please rebuild Skia for WASM"
    exit 1
fi

# Source files (exclude files that use fontconfig/filesystem)
# Include font_utils and text_sizing for font measurement and auto-fitting
SOURCES=(
    "$SRC_DIR/wasm/lotio_wasm.cpp"
    "$SRC_DIR/core/frame_encoder.cpp"
    "$SRC_DIR/utils/logging.cpp"
    "$SRC_DIR/utils/string_utils.cpp"
    "$SRC_DIR/text/text_config.cpp"
    "$SRC_DIR/text/json_manipulation.cpp"
    "$SRC_DIR/text/font_utils.cpp"
    "$SRC_DIR/text/text_sizing.cpp"
)

# Output to browser/ directory (where the files are expected)
BROWSER_DIR="$PROJECT_ROOT/browser"
mkdir -p "$BROWSER_DIR"
OUTPUT="$BROWSER_DIR/lotio.js"

# Create temporary directory for object files
OBJ_DIR=$(mktemp -d)
trap "rm -rf $OBJ_DIR; cleanup_temp_include" EXIT

# Compile
OBJECTS=()
MISSING_SOURCES=""
for src in "${SOURCES[@]}"; do
    if [ ! -f "$src" ]; then
        MISSING_SOURCES="$MISSING_SOURCES $src"
        continue
    fi
    obj_name=$(basename "${src%.cpp}.o")
    obj="$OBJ_DIR/$obj_name"
    echo "   Compiling: $(basename $src)"
    $CXX $COMPILE_FLAGS $INCLUDES -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done

# Fail if required source files are missing
if [ -n "$MISSING_SOURCES" ]; then
    echo "❌ Error: Required source files not found:"
    for src in $MISSING_SOURCES; do
        echo "   - $src"
    done
    exit 1
fi

echo ""
echo "   Linking..."
$CXX $COMPILE_FLAGS $LINK_FLAGS -o "$OUTPUT" "${OBJECTS[@]}" $LDFLAGS $SKIA_LIBS

echo ""
echo "✅ Build complete!"
echo ""
echo "📦 Output:"
echo "   JavaScript: $OUTPUT"
echo "   WASM binary: ${OUTPUT%.js}.wasm"
echo ""
echo "📁 Files are in browser/ directory and ready for use"
