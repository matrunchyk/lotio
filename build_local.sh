#!/bin/bash
set -e

# Local build script - mimics Docker build process for best DX
# Works on macOS (and can be adapted for Linux)

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia/skia"
SKIA_LIB_DIR="$SKIA_ROOT/out/Release"
SRC_DIR="$PROJECT_ROOT/src"

echo "🔨 Building Lottie Frame Renderer (Local Build)"
echo "================================================"

# Check if Skia is built
if [ ! -f "$SKIA_LIB_DIR/libskia.a" ] && [ ! -f "$SKIA_LIB_DIR/libskia.dylib" ]; then
    echo "❌ Skia library not found at $SKIA_LIB_DIR"
    echo "📦 Building Skia first (this may take a while)..."
    ./install_skia.sh
    echo ""
fi

# Detect OS and set library extensions
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    HOMEBREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/opt/homebrew")
    LIB_EXT=".dylib"
    FRAMEWORKS="-framework CoreFoundation -framework CoreGraphics -framework CoreText -framework CoreServices -framework AppKit"
    RPATH_FLAG="-Wl,-rpath,$SKIA_LIB_DIR"
else
    # Linux
    HOMEBREW_PREFIX=""
    LIB_EXT=".so"
    FRAMEWORKS=""
    RPATH_FLAG="-Wl,-rpath,$SKIA_LIB_DIR"
fi

# Compiler settings
CXX=clang++
CXXFLAGS="-std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra"
INCLUDES="-I$SKIA_ROOT -I$SRC_DIR"

# Library paths and flags
LDFLAGS="-L$SKIA_LIB_DIR $RPATH_FLAG"

# Skia libraries - find actual library files
SKIA_LIBS=""
for lib in skottie skia skparagraph sksg skshaper skunicode_icu skunicode_core skresources jsonreader; do
    if [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
        SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.a"
    elif [ -f "$SKIA_LIB_DIR/lib${lib}.dylib" ]; then
        SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.dylib"
    fi
done

# System libraries
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: Add Homebrew library paths
    ICU_PREFIX=$(brew --prefix icu4c 2>/dev/null || echo "$HOMEBREW_PREFIX/opt/icu4c@77")
    ICU_LIB_DIR="$ICU_PREFIX/lib"
    LDFLAGS="$LDFLAGS -L$ICU_LIB_DIR"
    # Add Homebrew library paths
    PNG_PREFIX=$(brew --prefix libpng 2>/dev/null || echo "$HOMEBREW_PREFIX")
    JPEG_PREFIX=$(brew --prefix jpeg-turbo 2>/dev/null || brew --prefix jpeg 2>/dev/null || echo "$HOMEBREW_PREFIX")
    WEBP_PREFIX=$(brew --prefix libwebp 2>/dev/null || echo "$HOMEBREW_PREFIX")
    HARFBUZZ_PREFIX=$(brew --prefix harfbuzz 2>/dev/null || echo "$HOMEBREW_PREFIX")
    FREETYPE_PREFIX=$(brew --prefix freetype 2>/dev/null || echo "$HOMEBREW_PREFIX")
    FONTCONFIG_PREFIX=$(brew --prefix fontconfig 2>/dev/null || echo "$HOMEBREW_PREFIX")
    
    LDFLAGS="$LDFLAGS -L$PNG_PREFIX/lib -L$JPEG_PREFIX/lib -L$WEBP_PREFIX/lib -L$HARFBUZZ_PREFIX/lib -L$FREETYPE_PREFIX/lib -L$FONTCONFIG_PREFIX/lib"
    
    # Use jpeg-turbo if available (has jpeg_skip_scanlines)
    if [ -f "$JPEG_PREFIX/lib/libjpeg.a" ] || [ -f "$JPEG_PREFIX/lib/libjpeg.dylib" ]; then
        JPEG_LIB="-ljpeg"
    else
        JPEG_LIB="-lturbojpeg"
    fi
    
    LIBS="$SKIA_LIBS -lwebp -lwebpdemux -lwebpmux -lpiex \
          -lfreetype -lpng16 $JPEG_LIB -lharfbuzz -licuuc -licui18n -licudata \
          -lz -lfontconfig -lm -lpthread"
else
    LIBS="$SKIA_LIBS -lwebp -lwebpdemux -lwebpmux -lpiex \
          -lfreetype -lpng -ljpeg -lharfbuzz -licuuc -licui18n -licudata \
          -lz -lfontconfig -lm -lpthread"
fi

# macOS-specific libraries
if [[ "$OSTYPE" == "darwin"* ]]; then
    LIBS="$LIBS $FRAMEWORKS"
else
    # Linux X11/GL libraries
    LIBS="$LIBS -lX11 -lGL -lGLU"
fi

# Source files (matching your modular structure)
SOURCES=(
    "$SRC_DIR/main.cpp"
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

# Output binary
TARGET="$PROJECT_ROOT/lotio"

echo "📝 Compiling source files..."
echo "   Sources: ${#SOURCES[@]} files"
echo "   Include: $SKIA_ROOT"
echo ""

# Compile all source files
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
$CXX $CXXFLAGS -o "$TARGET" "${OBJECTS[@]}" $LDFLAGS $LIBS

echo ""
echo "🧹 Cleaning up object files..."
rm -f "${OBJECTS[@]}"

echo ""
echo "✅ Build complete!"
echo "   Binary: $TARGET"
echo ""
echo "📋 Usage:"
echo "   $TARGET --png --webp input.json output_dir [fps]"
echo "   $TARGET --help"

