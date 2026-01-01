#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia/skia"
SKIA_LIB_DIR="$SKIA_ROOT/out/Wasm"
SRC_DIR="$PROJECT_ROOT/src"

# Check for Emscripten
if [ -z "$EMSDK" ]; then
    # Try Homebrew installation
    if command -v emcc >/dev/null 2>&1; then
        echo "✅ Using Homebrew's emscripten (emcc found in PATH)"
    else
        echo "❌ Emscripten not found. Please install via:"
        echo "   Option 1 (Recommended): brew install emscripten"
        echo "   Option 2: source emsdk/emsdk_env.sh"
        exit 1
    fi
else
    # Emscripten SDK installation - activate it
    echo "✅ Using Emscripten SDK at: $EMSDK"
    source "$EMSDK/emsdk_env.sh"
fi

# Check if Skia is built for WASM
if [ ! -f "$SKIA_LIB_DIR/libskia.a" ]; then
    echo "❌ Skia not built for WebAssembly. Building now..."
    ./scripts/build_skia_wasm.sh
fi

# Emscripten compiler
CXX=em++

# Compilation flags (used during -c compilation phase)
COMPILE_FLAGS="-std=c++17 -O3 -DNDEBUG -Wall -Wextra"

# Linker flags (used during linking phase only)
# Note: -sUSE_FREETYPE and -sUSE_LIBPNG are linker flags but can be specified during compilation
LINK_FLAGS="-sUSE_FREETYPE=1 \
    -sUSE_LIBPNG=1 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_FUNCTIONS=_lotio_init,_lotio_get_info,_lotio_render_frame,_lotio_render_frame_png,_lotio_cleanup,_lotio_register_font,_malloc,_free \
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

# Skia libraries (static) - only include what exists
# Note: skunicode outputs as libskunicode.a (not libskunicode_core.a) for WASM builds
SKIA_LIBS=""
for lib in skottie skia skparagraph sksg skshaper skunicode skresources jsonreader; do
    if [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
        SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.a"
    else
        echo "⚠️  Warning: $SKIA_LIB_DIR/lib${lib}.a not found, skipping"
    fi
done

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

OUTPUT="$PROJECT_ROOT/lotio.js"

echo "🔨 Building lotio for WebAssembly..."
echo ""

# Compile
OBJECTS=()
for src in "${SOURCES[@]}"; do
    if [ ! -f "$src" ]; then
        echo "⚠️  Warning: Source file not found: $src"
        continue
    fi
    obj="${src%.cpp}.o"
    echo "   Compiling: $(basename $src)"
    $CXX $COMPILE_FLAGS $INCLUDES -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done

echo ""
echo "🔗 Linking..."
$CXX $COMPILE_FLAGS $LINK_FLAGS -o "$OUTPUT" "${OBJECTS[@]}" $LDFLAGS $SKIA_LIBS

echo ""
echo "🧹 Cleaning up object files..."
rm -f "${OBJECTS[@]}"

echo ""
echo "✅ Build complete!"
echo "   Output: $OUTPUT"
echo "   Also generated: ${OUTPUT%.js}.wasm (WASM binary)"

