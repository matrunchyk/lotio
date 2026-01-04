#!/bin/bash
set -e

# Local build script - mimics Docker build process for best DX
# Works on macOS (and can be adapted for Linux)

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia/skia"
SKIA_LIB_DIR="$SKIA_ROOT/out/Release"
SRC_DIR="$PROJECT_ROOT/src"

echo "🔨 Building Lottie Frame Renderer (Local Build)"
echo "================================================"

# Clean up .a files on macOS (prevents mixing Linux/macOS libraries from Docker tests)
# macOS prefers .dylib files, so we clean .a files unconditionally
if [[ "$OSTYPE" == "darwin"* ]]; then
    if [ -d "$SKIA_LIB_DIR" ]; then
        rm -f "$SKIA_LIB_DIR"/*.a
    fi
fi

# Check if Skia is built
if [ ! -f "$SKIA_LIB_DIR/libskia.a" ] && [ ! -f "$SKIA_LIB_DIR/libskia.dylib" ]; then
    echo "❌ Skia library not found at $SKIA_LIB_DIR"
    echo "📦 Building Skia first (this may take a while)..."
    ./scripts/install_skia.sh
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
VERSION_DEFINE=""
if [ -n "$VERSION" ]; then
    VERSION_DEFINE="-DVERSION=\\\"${VERSION}\\\""
fi
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS - clang supports -march=native
    CXXFLAGS="-std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra $VERSION_DEFINE"
else
    # Linux - clang doesn't support -march=native, use -O3 only
    CXXFLAGS="-std=c++17 -O3 -DNDEBUG -Wall -Wextra $VERSION_DEFINE"
fi

# Add Homebrew include paths for macOS
# Note: We add $SKIA_ROOT for relative includes in .cpp files (e.g., "include/core/SkCanvas.h")
# We also create a temporary include structure that makes <skia/...> work during build
# This matches the installed structure: include/skia/core/SkCanvas.h
# Structure needed: temp_include/skia/core/SkCanvas.h and temp_include/skia/modules/skottie/include/Skottie.h
TEMP_INCLUDE_DIR=$(mktemp -d)
mkdir -p "$TEMP_INCLUDE_DIR/skia"
# Create symlinks to match installed structure:
# - skia/core/ -> include/core/ (for <skia/core/SkCanvas.h>)
# - skia/modules/ -> modules/ (for <skia/modules/skottie/include/Skottie.h>)
ln -sf "$SKIA_ROOT/include/core" "$TEMP_INCLUDE_DIR/skia/core" 2>/dev/null || true
ln -sf "$SKIA_ROOT/include" "$TEMP_INCLUDE_DIR/skia/include" 2>/dev/null || true
ln -sf "$SKIA_ROOT/modules" "$TEMP_INCLUDE_DIR/skia/modules" 2>/dev/null || true

if [[ "$OSTYPE" == "darwin"* ]]; then
    HOMEBREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/opt/homebrew")
    INCLUDES="-I$SKIA_ROOT -I$TEMP_INCLUDE_DIR -I$SRC_DIR -I$HOMEBREW_PREFIX/include -I$HOMEBREW_PREFIX/include/fontconfig -I$HOMEBREW_PREFIX/include/freetype2"
else
    INCLUDES="-I$SKIA_ROOT -I$TEMP_INCLUDE_DIR -I$SRC_DIR"
fi

# Cleanup function for temp directory
cleanup_temp_include() {
    rm -rf "$TEMP_INCLUDE_DIR" 2>/dev/null || true
}
trap cleanup_temp_include EXIT

# Library paths and flags
LDFLAGS="-L$SKIA_LIB_DIR $RPATH_FLAG"

# Skia libraries - find actual library files
# Link order matters: dependencies first, then main library
SKIA_LIBS=""
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: Prefer .dylib files, or verify .a files are macOS (mach-o) format
    # Use -Wl,-force_load only for main libskia.a to avoid duplicate symbols
    # Link dependencies first, then main library
    # IMPORTANT: On macOS, we need to check if .a files are actually macOS libraries
    # (not Linux libraries from Docker tests). We prefer .dylib files.
    
    # Helper function to check if .a file is macOS format
    is_macos_archive() {
        local archive="$1"
        # Try to extract first .o file (skip archive metadata like __.SYMDEF)
        local first_obj=$(ar -t "$archive" 2>/dev/null | grep '\.o$' | head -1)
        if [ -z "$first_obj" ]; then
            return 1  # No valid object files found
        fi
        # Extract and check file type - should be Mach-O on macOS
        ar -p "$archive" "$first_obj" 2>/dev/null | file - 2>/dev/null | grep -q "Mach-O" >/dev/null 2>&1
    }
    
    for lib in jsonreader skresources skunicode_core skunicode_icu skshaper skparagraph sksg skottie; do
        # Prefer .dylib files on macOS
        if [ -f "$SKIA_LIB_DIR/lib${lib}.dylib" ]; then
            SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.dylib"
        elif [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
            # Check if .a file is macOS format
            if is_macos_archive "$SKIA_LIB_DIR/lib${lib}.a"; then
                SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.a"
            else
                echo "⚠️  Warning: $SKIA_LIB_DIR/lib${lib}.a appears to be a Linux library (not Mach-O), skipping"
                echo "   💡 Tip: Rebuild Skia for macOS or use .dylib files"
            fi
        fi
    done
    # Main library with force_load
    if [ -f "$SKIA_LIB_DIR/libskia.dylib" ]; then
        SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/libskia.dylib"
    elif [ -f "$SKIA_LIB_DIR/libskia.a" ]; then
        # Verify it's macOS format before using force_load
        if is_macos_archive "$SKIA_LIB_DIR/libskia.a"; then
            SKIA_LIBS="$SKIA_LIBS -Wl,-force_load,$SKIA_LIB_DIR/libskia.a"
        else
            echo "❌ ERROR: $SKIA_LIB_DIR/libskia.a appears to be a Linux library (not Mach-O)"
            echo "   💡 Solution: Rebuild Skia for macOS: ./scripts/install_skia.sh"
            exit 1
        fi
    fi
else
    # Linux: Link static libraries directly
    # IMPORTANT: Link order matters for static libraries!
    # When linking static libraries, the linker processes them left-to-right.
    # It only includes symbols from a library if they're needed by already-processed code.
    # Dependencies must come AFTER the libraries that need them.
    # 
    # Dependency chain:
    # - skottie depends on: skshaper, skunicode_icu, sksg, jsonreader
    # - skparagraph depends on: skshaper, skunicode_icu
    # - skshaper depends on: skunicode_icu, skunicode_core
    # - skunicode_icu depends on: skunicode_core (circular dependency)
    # 
    # Use --start-group and --end-group for unicode libraries to handle circular dependencies
    # Order: base -> skparagraph -> skottie -> skshaper -> skunicode (group) -> sksg/jsonreader -> skia
    MISSING_LIBS=()
    SKIA_LIBS=""
    
    # Libraries before unicode group
    for lib in skresources skparagraph skottie skshaper; do
        if [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
            SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.a"
        elif [ -f "$SKIA_LIB_DIR/lib${lib}.so" ]; then
            SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.so"
        else
            MISSING_LIBS+=("lib${lib}.a")
        fi
    done
    
    # Unicode libraries with circular dependencies - use group
    SKIA_LIBS="$SKIA_LIBS -Wl,--start-group"
    for lib in skunicode_icu skunicode_core; do
        if [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
            SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.a"
        elif [ -f "$SKIA_LIB_DIR/lib${lib}.so" ]; then
            SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.so"
        else
            MISSING_LIBS+=("lib${lib}.a")
        fi
    done
    SKIA_LIBS="$SKIA_LIBS -Wl,--end-group"
    
    # Libraries after unicode group
    for lib in sksg jsonreader skia; do
        if [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
            SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.a"
        elif [ -f "$SKIA_LIB_DIR/lib${lib}.so" ]; then
            SKIA_LIBS="$SKIA_LIBS $SKIA_LIB_DIR/lib${lib}.so"
        else
            MISSING_LIBS+=("lib${lib}.a")
        fi
    done
    
    # Check for missing critical libraries
    if [ ${#MISSING_LIBS[@]} -gt 0 ]; then
        echo "⚠️  Warning: Some Skia libraries not found:"
        for lib in "${MISSING_LIBS[@]}"; do
            echo "   - $lib"
        done
        echo ""
        echo "Available libraries in $SKIA_LIB_DIR:"
        ls -1 "$SKIA_LIB_DIR"/*.a "$SKIA_LIB_DIR"/*.so 2>/dev/null | head -20 || echo "   (none found)"
        echo ""
        echo "This may cause linker errors. Make sure Skia was built with skia_enable_skottie=true"
    fi
fi

# System libraries
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS: Add Homebrew library paths
    ICU_PREFIX="${ICU_PREFIX:-$(brew --prefix icu4c 2>/dev/null || echo "$HOMEBREW_PREFIX/opt/icu4c@77")}"
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
          -lz -lfontconfig -lexpat -lm -lpthread"
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

echo "📝 Building liblotio library..."
echo "   Library sources: ${#LIBRARY_SOURCES[@]} files"
echo "   Include: $SKIA_ROOT"
echo ""

# Compile library source files
LIBRARY_OBJECTS=()
for src in "${LIBRARY_SOURCES[@]}"; do
    if [ ! -f "$src" ]; then
        echo "⚠️  Warning: Source file not found: $src"
        continue
    fi
    obj="${src%.cpp}.o"
    echo "   Compiling: $(basename $src)"
    $CXX $CXXFLAGS $INCLUDES -c "$src" -o "$obj"
    LIBRARY_OBJECTS+=("$obj")
done

# Create static library
echo ""
echo "📚 Creating liblotio.a..."
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

echo "✅ Created: $LIBRARY_TARGET"

# Compile main.cpp separately
echo ""
echo "📝 Compiling main entry point..."
if [ ! -f "$MAIN_SOURCE" ]; then
    echo "❌ Error: Main source file not found: $MAIN_SOURCE"
    exit 1
fi
MAIN_OBJECT="${MAIN_SOURCE%.cpp}.o"
echo "   Compiling: $(basename $MAIN_SOURCE)"
$CXX $CXXFLAGS $INCLUDES -c "$MAIN_SOURCE" -o "$MAIN_OBJECT"

# Link binary: main.o + liblotio.a + Skia libraries
echo ""
echo "🔗 Linking binary..."
$CXX $CXXFLAGS -o "$TARGET" "$MAIN_OBJECT" "$LIBRARY_TARGET" $LDFLAGS $LIBS

echo ""
echo "🧹 Cleaning up object files..."
rm -f "${LIBRARY_OBJECTS[@]}"
rm -f "$MAIN_OBJECT"

echo ""
echo "✅ Build complete!"
echo "   Binary: $TARGET"
echo "   Library: $LIBRARY_TARGET"
echo ""
echo "📋 Usage:"
echo "   $TARGET --png --webp input.json output_dir [fps]"
echo "   $TARGET --help"

