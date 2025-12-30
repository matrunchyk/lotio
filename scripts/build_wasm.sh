#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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
CXXFLAGS="-std=c++17 -O3 -DNDEBUG -Wall -Wextra \
    -sUSE_FREETYPE=1 \
    -sUSE_LIBPNG=1 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_FUNCTIONS=_lotio_init,_lotio_get_info,_lotio_render_frame,_lotio_render_frame_png,_lotio_cleanup,_lotio_register_font,_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,HEAP8,HEAPU8,HEAP32,HEAPF32,HEAPF64 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createLotioModule \
    --bind"

INCLUDES="-I$SKIA_ROOT -I$SRC_DIR"
LDFLAGS="-L$SKIA_LIB_DIR"

# Skia libraries (static) - only include what exists
SKIA_LIBS=""
for lib in skottie skia skparagraph sksg skshaper skunicode_core skresources jsonreader; do
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
    $CXX $CXXFLAGS $INCLUDES -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done

echo ""
echo "🔗 Linking..."
$CXX $CXXFLAGS -o "$OUTPUT" "${OBJECTS[@]}" $LDFLAGS $SKIA_LIBS

echo ""
echo "🧹 Cleaning up object files..."
rm -f "${OBJECTS[@]}"

echo ""
echo "✅ Build complete!"
echo "   Output: $OUTPUT"
echo "   Also generated: ${OUTPUT%.js}.wasm (WASM binary)"

