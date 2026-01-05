#!/bin/bash
set -e

################################################################################
# Unified WASM Build Script for Lotio
#
# This script builds both Skia and lotio for WebAssembly in a single command.
# It handles everything: dependencies, Skia build, and lotio compilation.
#
# MINIMAL DEPENDENCIES (bundled, not system):
# ============================================
# - freetype (with freetype-no-type1 config for WASM)
# - libpng
# - brotli
#
# All other dependencies are disabled (JPEG, WebP, Wuffs, ICU, HarfBuzz, etc.)
################################################################################

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
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

# Check if Skia directory exists - if not, set it up (similar to build_minimal.sh)
if [ ! -d "$SKIA_ROOT" ] || [ ! -f "$SKIA_ROOT/bin/gn" ]; then
    echo "📦 Setting up Skia structure for WASM build..."
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
    
    # Remove existing third_party if it exists (for consistency with build_minimal.sh)
    # This ensures clean setup - we're already inside the "if Skia missing" check
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
    
    # Copy only essential Skia files (same as build_minimal.sh)
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
    # Copy experimental directory (needed for BUILD.gn references)
    if [ -d "experimental" ]; then
        cp -r experimental "$THIRD_PARTY_DIR/skia/"
    fi
    
    # Essential files
    cp BUILD.gn "$THIRD_PARTY_DIR/skia/"
    cp LICENSE "$THIRD_PARTY_DIR/skia/"
    cp README "$THIRD_PARTY_DIR/skia/" 2>/dev/null || true
    cp .gn "$THIRD_PARTY_DIR/skia/" 2>/dev/null || true
    
    # Create third_party structure (but not externals yet - will be added for WASM deps)
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
    echo ""
    
    # Create minimal DEPS file (same as build_minimal.sh)
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

hooks = []
EOF
    
    echo "   ✅ Skia structure set up for WASM build"
    echo ""
fi

################################################################################
# Step 1: Build Skia for WASM (if not already built)
################################################################################

cd "$SKIA_ROOT"

# Check if Skia is already built for WASM
if [ -f "$SKIA_LIB_DIR/libskia.a" ]; then
    echo "✅ Skia already built for WebAssembly (skipping Skia build)"
    echo ""
else
    echo "📦 Step 1: Building Skia for WebAssembly..."
    echo ""
    
    # Ensure freetype config files are present FIRST (they're part of Skia, not the dependency)
    # For WASM builds, we need freetype-no-type1 (BUILD.gn checks target_cpu == "wasm")
    if [ ! -d "third_party/freetype2/include/freetype-no-type1" ]; then
        echo "   Copying freetype-no-type1 config files from Skia..."
        # Use system temp directory for temporary Skia clone
        SKIA_TEMP_DIR=$(mktemp -d -t skia_clone_XXXXXX)
        
        # Cleanup function for temp directory
        cleanup_skia_temp() {
            if [ -n "$SKIA_TEMP_DIR" ] && [ -d "$SKIA_TEMP_DIR" ]; then
                rm -rf "$SKIA_TEMP_DIR" 2>/dev/null || true
            fi
        }
        trap cleanup_skia_temp EXIT
        
        echo "   Fetching Skia temp clone for config files..."
        cd "$SKIA_TEMP_DIR"
        git clone --depth 1 https://skia.googlesource.com/skia.git
        cd "$SKIA_ROOT"
        
        if [ -d "$SKIA_TEMP_DIR/skia/third_party/freetype2/include/freetype-no-type1" ]; then
            mkdir -p third_party/freetype2/include
            cp -r "$SKIA_TEMP_DIR/skia/third_party/freetype2/include/freetype-no-type1" third_party/freetype2/include/
            if [ -d "third_party/freetype2/include/freetype-no-type1" ]; then
                echo "   ✅ Freetype-no-type1 config files copied"
            else
                echo "   ❌ ERROR: Failed to copy freetype-no-type1 config files"
                exit 1
            fi
        else
            echo "   ❌ ERROR: freetype-no-type1 config files not found in Skia temp clone"
            exit 1
        fi
    fi
    
    # For WASM builds, we need bundled dependencies (freetype, libpng, brotli, etc.) because WASM can't use system libraries
    # Check if we need to fetch dependencies
    if [ ! -d "third_party/externals/freetype" ] || [ ! -d "third_party/externals/libpng" ] || [ ! -d "third_party/externals/brotli" ]; then
        echo "   Fetching minimal WASM dependencies (freetype, libpng, brotli)..."
        echo "   WASM builds need bundled dependencies (can't use system libraries)"
        
        # Clone only the specific dependencies we need
        mkdir -p third_party/externals
        cd third_party/externals
        
        # Clone freetype (required for skia_use_freetype=true in WASM)
        if [ ! -d "freetype" ]; then
            echo "   Cloning freetype..."
            git clone --depth 1 https://gitlab.freedesktop.org/freetype/freetype.git freetype
            echo "   ✅ Freetype cloned"
        fi
        
        # Clone libpng (required for skia_use_libpng_encode/decode=true in WASM)
        if [ ! -d "libpng" ]; then
            echo "   Cloning libpng..."
            git clone --depth 1 https://github.com/glennrp/libpng.git libpng
            echo "   ✅ libpng cloned"
        fi
        
        # Clone brotli (required for PNG compression in WASM)
        if [ ! -d "brotli" ]; then
            echo "   Cloning brotli..."
            git clone --depth 1 https://github.com/google/brotli.git brotli
            echo "   ✅ brotli cloned"
        fi
        
        cd "$SKIA_ROOT"
        echo "   ✅ Minimal WASM dependencies fetched"
    fi
    
    # Patch brotli BUILD.gn to include prefix.c (required for kCmdLut symbol used by skottie)
    # This is needed because Skia's minimal brotli BUILD.gn doesn't include prefix.c
    BROTLI_BUILD_GN="$SKIA_ROOT/third_party/brotli/BUILD.gn"
    if [ -f "$BROTLI_BUILD_GN" ]; then
        if ! grep -q "dec/prefix.c" "$BROTLI_BUILD_GN"; then
            echo "   Patching brotli BUILD.gn to include prefix.c..."
            # Add prefix.c after huffman.c and before state.c
            if [[ "$OSTYPE" == "darwin"* ]]; then
                sed -i '' '/dec\/huffman\.c/a\
    "../externals/brotli/c/dec/prefix.c",
' "$BROTLI_BUILD_GN"
            else
                sed -i '/dec\/huffman\.c/a\    "../externals/brotli/c/dec/prefix.c",' "$BROTLI_BUILD_GN"
            fi
            echo "   ✅ Patched brotli BUILD.gn (added prefix.c)"
        fi
    fi
    
    # Fetch GN binary if needed (for correct platform)
    if [ ! -f "bin/gn" ] || ! file bin/gn 2>/dev/null | grep -q "$(uname -m)"; then
        echo "   Fetching GN binary for current platform..."
        python3 bin/fetch-gn
    fi
    
    # Set up symlinks to emscripten tools so Skia can find them
    # Skia expects emscripten at third_party/externals/emsdk/upstream/emscripten/
    # This works for both Homebrew installations and EMSDK installations
    if command -v emcc >/dev/null 2>&1; then
        echo "   Setting up symlinks to emscripten tools for Skia..."
        mkdir -p "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten"
        
        # Find the actual emscripten installation directory
        EMC_PATH=$(which emcc)
        # Resolve the real path (follow symlinks)
        EMC_REAL_PATH=$(readlink -f "$EMC_PATH" 2>/dev/null || realpath "$EMC_PATH" 2>/dev/null || echo "$EMC_PATH")
        
        # Determine emscripten directory
        if [ -n "$EMSDK" ] && [ -d "$EMSDK/upstream/emscripten" ]; then
            # EMSDK installation - symlink the entire emscripten directory
            EMSDK_EMSCRIPTEN_DIR="$EMSDK/upstream/emscripten"
            echo "   Using EMSDK emscripten directory: $EMSDK_EMSCRIPTEN_DIR"
            
            # Remove existing symlinks/directory and create symlink to entire emscripten dir
            rm -rf "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten" 2>/dev/null || true
            mkdir -p "$SKIA_ROOT/third_party/externals/emsdk/upstream"
            ln -sf "$EMSDK_EMSCRIPTEN_DIR" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten"
            echo "   ✅ Symlinked entire emscripten directory from EMSDK"
        else
            # Homebrew or other installation - create individual symlinks
            # Find the emscripten installation directory by following the emcc symlink
            # Homebrew: emcc -> /opt/homebrew/Cellar/emscripten/X.X.X/bin/emcc -> /opt/homebrew/Cellar/emscripten/X.X.X/libexec/emcc.py
            EMC_DIR=$(dirname "$EMC_REAL_PATH")
            
            # Try to find the emscripten directory (where emcc.py lives)
            if [ -f "$EMC_DIR/emcc.py" ]; then
                EMSDK_EMSCRIPTEN_DIR="$EMC_DIR"
            elif [ -f "$(dirname "$EMC_DIR")/libexec/emcc.py" ]; then
                EMSDK_EMSCRIPTEN_DIR="$(dirname "$EMC_DIR")/libexec"
            else
                # Fallback: create symlinks to wrappers and hope for the best
                EMSDK_EMSCRIPTEN_DIR=""
            fi
            
            if [ -n "$EMSDK_EMSCRIPTEN_DIR" ] && [ -d "$EMSDK_EMSCRIPTEN_DIR" ]; then
                # Symlink the entire directory if we found it
                rm -rf "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten" 2>/dev/null || true
                mkdir -p "$SKIA_ROOT/third_party/externals/emsdk/upstream"
                ln -sf "$EMSDK_EMSCRIPTEN_DIR" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten"
                echo "   ✅ Symlinked emscripten directory from: $EMSDK_EMSCRIPTEN_DIR"
                
                # Fix Node.js path in .emscripten config if it exists and is wrong
                EMSDK_CONFIG="$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/.emscripten"
                if [ -f "$EMSDK_CONFIG" ]; then
                    NODE_PATH=$(which node 2>/dev/null || echo "")
                    if [ -n "$NODE_PATH" ] && [ -f "$NODE_PATH" ]; then
                        # Update NODE_JS in the config file
                        if grep -q "^NODE_JS" "$EMSDK_CONFIG" 2>/dev/null; then
                            # Use sed to update the NODE_JS line, handling both ' and " quotes
                            sed -i.bak "s|^NODE_JS.*=.*|NODE_JS = ['$NODE_PATH']|" "$EMSDK_CONFIG" 2>/dev/null || \
                            sed -i '' "s|^NODE_JS.*=.*|NODE_JS = ['$NODE_PATH']|" "$EMSDK_CONFIG" 2>/dev/null || true
                            rm -f "$EMSDK_CONFIG.bak" 2>/dev/null || true
                            echo "   ✅ Updated Node.js path in .emscripten config to: $NODE_PATH"
                        fi
                    fi
                fi
            else
                # Fallback: create individual symlinks
                EMPP_PATH=$(which em++)
                EMAR_PATH=$(which emar)
                EMRANLIB_PATH=$(which emranlib 2>/dev/null || which emnm 2>/dev/null || echo "")
                
                ln -sf "$EMC_PATH" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/emcc" 2>/dev/null || true
                ln -sf "$EMPP_PATH" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/em++" 2>/dev/null || true
                ln -sf "$EMAR_PATH" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/emar" 2>/dev/null || true
                if [ -n "$EMRANLIB_PATH" ]; then
                    ln -sf "$EMRANLIB_PATH" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/emranlib" 2>/dev/null || true
                fi
                
                # Also try to symlink emcc.py if we can find it
                if [ -f "$(dirname "$EMC_REAL_PATH")/emcc.py" ]; then
                    ln -sf "$(dirname "$EMC_REAL_PATH")/emcc.py" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/emcc.py" 2>/dev/null || true
                fi
                
                echo "   ✅ Created individual symlinks for emscripten tools"
            fi
            
            # Don't symlink cache directory - let emcc use its own cache
            # The cache directory needs to be writable, so symlinking can cause issues
            # emcc will create its own cache if needed
            echo "   Note: Emscripten will use its own cache directory"
        fi
        
        echo "   ✅ Symlinks created for emscripten tools"
    fi
    
    # Set up environment to use system emcc
    export CC=$(which emcc)
    export CXX=$(which em++)
    export AR=$(which emar)
    export NM=$(which emnm)
    
    # GN args for WebAssembly build
    # Note: We use target_cpu="wasm" (not "wasm32") because BUILD.gn checks for target_cpu == "wasm"
    # This ensures freetype uses freetype-no-type1 config (minimal) instead of freetype-android
    GN_ARGS="target_cpu=\"wasm\" \
        target_os=\"wasm\" \
        is_official_build=true \
        is_debug=false \
        skia_enable_skottie=true \
        skia_use_freetype=true \
        skia_use_libpng_encode=true \
        skia_use_libpng_decode=true \
        skia_use_libjpeg_turbo_decode=false \
        skia_use_libjpeg_turbo_encode=false \
        skia_use_libwebp_decode=false \
        skia_use_libwebp_encode=false \
        skia_use_wuffs=false \
        skia_enable_pdf=false \
        skia_use_fontconfig=false \
        skia_use_icu=false \
        skia_use_harfbuzz=false \
        cc=\"$(which emcc)\" \
        cxx=\"$(which em++)\" \
        ar=\"$(which emar)\" \
        extra_cflags=[\"-O3\", \"-sUSE_LIBPNG=1\", \"-Wno-error\", \"-Wno-implicit-function-declaration\"]"
    
    echo "   Generating build files with GN for WebAssembly..."
    bin/gn gen out/Wasm --args="$GN_ARGS"
    
    echo "   Building Skia for WebAssembly (this may take a while)..."
    # Build only the libraries we need, not CanvasKit
    # Use GN target format (root target is :target, module targets are modules/path:target)
    # Build core libraries first (avoiding CanvasKit which requires features we don't have)
    ninja -C out/Wasm :skia modules/skottie:skottie modules/sksg:sksg modules/skshaper:skshaper modules/skunicode:skunicode modules/skresources:skresources modules/jsonreader:jsonreader
    
    # Try to build skparagraph (may fail if ICU/HarfBuzz disabled, that's OK)
    # Note: If ninja says "no work to do", it means everything is already built (incremental build)
    if ! ninja -C out/Wasm modules/skparagraph:skparagraph 2>/dev/null; then
        echo "   ⚠️  Warning: skparagraph failed to build (likely due to disabled ICU/HarfBuzz)"
        echo "   This is OK if lotio doesn't require paragraph features"
    fi
    
    echo "   ✅ Skia built for WebAssembly"
    echo ""
fi

################################################################################
# Step 2: Build lotio for WASM
################################################################################

cd "$PROJECT_ROOT"

echo "📝 Step 2: Building lotio for WebAssembly..."
echo ""

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
