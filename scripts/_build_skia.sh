#!/bin/bash
set -e

################################################################################
# Shared Skia Build Script
#
# This script builds Skia for either native (binary) or WASM targets.
# It handles Skia setup, dependency fetching, and compilation.
#
# Usage:
#   scripts/_build_skia.sh [--target=binary|wasm] [--target-cpu=arm64|x64] [--skip-setup]
#
# Parameters:
#   --target=binary|wasm    Build target (default: binary)
#   --target-cpu=arm64|x64  Target CPU for native builds (default: auto-detect)
#   --skip-setup            Skip Skia structure setup (assumes it exists)
################################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia"

# Default values
TARGET="binary"
TARGET_CPU=""
SKIP_SETUP=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --target=*)
            TARGET="${1#*=}"
            shift
            ;;
        --target-cpu=*)
            TARGET_CPU="${1#*=}"
            shift
            ;;
        --skip-setup)
            SKIP_SETUP=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Auto-detect target CPU if not specified (for native builds)
if [[ "$TARGET" == "binary" ]] && [[ -z "$TARGET_CPU" ]]; then
    if [[ "$OSTYPE" == "darwin"* ]]; then
        ARCH=$(uname -m)
        if [[ "$ARCH" == "arm64" ]]; then
            TARGET_CPU="arm64"
        else
            TARGET_CPU="x64"
        fi
    else
        ARCH=$(uname -m)
        if [[ "$ARCH" == "aarch64" ]] || [[ "$ARCH" == "arm64" ]]; then
            TARGET_CPU="arm64"
        else
            TARGET_CPU="x64"
        fi
    fi
fi

# Set output directory based on target
if [[ "$TARGET" == "wasm" ]]; then
    SKIA_LIB_DIR="$SKIA_ROOT/out/Wasm"
    BUILD_DIR="out/Wasm"
else
    SKIA_LIB_DIR="$SKIA_ROOT/out/Release"
    BUILD_DIR="out/Release"
fi

echo "🔨 Building Skia (target: $TARGET"
if [[ "$TARGET" == "binary" ]]; then
    echo "   CPU: $TARGET_CPU"
fi
echo "   Output: $BUILD_DIR)"
echo ""

################################################################################
# Step 0: Set up Skia structure (if needed)
################################################################################

if [[ "$SKIP_SETUP" == "false" ]]; then
    echo "🗑️  Step 0: Setting up Skia structure..."
    echo ""
    
    THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
    SKIA_TEMP_DIR=$(mktemp -d -t skia_clone_XXXXXX)
    
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
    
    # Copy only essential Skia files
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
    if [ -d "tools" ]; then
        cp -r tools "$THIRD_PARTY_DIR/skia/"
    fi
    if [ -d "experimental" ]; then
        cp -r experimental "$THIRD_PARTY_DIR/skia/"
    fi
    
    # Essential files
    cp BUILD.gn "$THIRD_PARTY_DIR/skia/"
    cp LICENSE "$THIRD_PARTY_DIR/skia/"
    cp README "$THIRD_PARTY_DIR/skia/" 2>/dev/null || true
    cp .gn "$THIRD_PARTY_DIR/skia/" 2>/dev/null || true
    
    # Create third_party structure
    mkdir -p "$THIRD_PARTY_DIR/skia/third_party/externals"
    
    # Copy third_party config files
    if [ -d "third_party" ]; then
        find third_party -type f -name "*.gn" -o -name "*.gni" | while read -r file; do
            dir=$(dirname "$file")
            mkdir -p "$THIRD_PARTY_DIR/skia/$dir"
            cp "$file" "$THIRD_PARTY_DIR/skia/$file"
        done
    fi
    
    echo "   ✅ Essential Skia files copied"
    
    # Create DEPS file
    echo "   Creating DEPS file..."
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
    
    echo "   ✅ DEPS file created"
    echo ""
fi

cd "$SKIA_ROOT"

# Apply Skottie patches if patch file exists
PATCH_FILE="$PROJECT_ROOT/scripts/patches/skottie.patch"
if [ -f "$PATCH_FILE" ]; then
    echo "   Applying Skottie patches..."
    
    # Check if patch is already applied by checking for a key change
    PATCHED_FILES=(
        "modules/skottie/src/text/TextAnimator.h"
        "modules/skottie/src/text/TextAnimator.cpp"
        "modules/skottie/src/text/TextAdapter.cpp"
    )
    
    PATCH_ALREADY_APPLIED=false
    for file in "${PATCHED_FILES[@]}"; do
        if [ -f "$file" ] && grep -q "skew_axis" "$file" 2>/dev/null; then
            PATCH_ALREADY_APPLIED=true
            break
        fi
    done
    
    if [ "$PATCH_ALREADY_APPLIED" = true ]; then
        echo "   ⚠️  Patch appears to be already applied, re-downloading affected files..."
        
        # Re-download only the files that need patching
        SKIA_TEMP_DIR=$(mktemp -d -t skia_fresh_XXXXXX)
        cleanup_fresh_skia() {
            if [ -n "$SKIA_TEMP_DIR" ] && [ -d "$SKIA_TEMP_DIR" ]; then
                rm -rf "$SKIA_TEMP_DIR" 2>/dev/null || true
            fi
        }
        trap cleanup_fresh_skia EXIT
        
        cd "$SKIA_TEMP_DIR"
        echo "   Cloning fresh Skia to get unpatched files..."
        git clone --depth 1 https://skia.googlesource.com/skia.git
        
        # Re-copy only the text files that need patching
        echo "   Replacing patched files with fresh copies..."
        cp skia/modules/skottie/src/text/TextAnimator.h "$SKIA_ROOT/modules/skottie/src/text/"
        cp skia/modules/skottie/src/text/TextAnimator.cpp "$SKIA_ROOT/modules/skottie/src/text/"
        cp skia/modules/skottie/src/text/TextAdapter.cpp "$SKIA_ROOT/modules/skottie/src/text/"
        
        cd "$SKIA_ROOT"
        cleanup_fresh_skia
        trap - EXIT
        echo "   ✅ Fresh files restored"
    fi
    
    # Now apply the patch
    if git apply --check "$PATCH_FILE" 2>/dev/null || patch -p1 --dry-run -s < "$PATCH_FILE" 2>/dev/null; then
        if command -v git >/dev/null 2>&1 && [ -d ".git" ]; then
            git apply "$PATCH_FILE" || {
                echo "   ⚠️  Warning: git apply failed, trying patch command..."
                patch -p1 < "$PATCH_FILE" || {
                    echo "   ❌ ERROR: Failed to apply patch"
                    exit 1
                }
            }
        else
            patch -p1 < "$PATCH_FILE" || {
                echo "   ❌ ERROR: Failed to apply patch"
                exit 1
            }
        fi
        echo "   ✅ Skottie patches applied"
    else
        echo "   ❌ ERROR: Patch cannot be applied (files may be in unexpected state)"
        exit 1
    fi
    echo ""
fi

################################################################################
# Step 1: Fetch dependencies (WASM only)
################################################################################

if [[ "$TARGET" == "wasm" ]]; then
    # Ensure freetype config files are present
    if [ ! -d "third_party/freetype2/include/freetype-no-type1" ]; then
        echo "   Copying freetype-no-type1 config files from Skia..."
        SKIA_TEMP_DIR=$(mktemp -d -t skia_clone_XXXXXX)
        
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
            echo "   ✅ Freetype-no-type1 config files copied"
        else
            echo "   ❌ ERROR: freetype-no-type1 config files not found"
            exit 1
        fi
    fi
    
    # Fetch WASM dependencies
    if [ ! -d "third_party/externals/freetype" ] || [ ! -d "third_party/externals/libpng" ] || [ ! -d "third_party/externals/brotli" ]; then
        echo "   Fetching WASM dependencies (freetype, libpng, brotli)..."
        
        mkdir -p third_party/externals
        cd third_party/externals
        
        if [ ! -d "freetype" ]; then
            echo "   Cloning freetype..."
            git clone --depth 1 https://gitlab.freedesktop.org/freetype/freetype.git freetype
            echo "   ✅ Freetype cloned"
        fi
        
        if [ ! -d "libpng" ]; then
            echo "   Cloning libpng..."
            git clone --depth 1 https://github.com/glennrp/libpng.git libpng
            echo "   ✅ libpng cloned"
        fi
        
        if [ ! -d "brotli" ]; then
            echo "   Cloning brotli..."
            git clone --depth 1 https://github.com/google/brotli.git brotli
            echo "   ✅ brotli cloned"
        fi
        
        cd "$SKIA_ROOT"
        echo "   ✅ WASM dependencies fetched"
    fi
    
    # Patch brotli BUILD.gn
    BROTLI_BUILD_GN="$SKIA_ROOT/third_party/brotli/BUILD.gn"
    if [ -f "$BROTLI_BUILD_GN" ]; then
        if ! grep -q "dec/prefix.c" "$BROTLI_BUILD_GN"; then
            echo "   Patching brotli BUILD.gn to include prefix.c..."
            if [[ "$OSTYPE" == "darwin"* ]]; then
                sed -i '' '/dec\/huffman\.c/a\
    "../externals/brotli/c/dec/prefix.c",
' "$BROTLI_BUILD_GN"
            else
                sed -i '/dec\/huffman\.c/a\    "../externals/brotli/c/dec/prefix.c",' "$BROTLI_BUILD_GN"
            fi
            echo "   ✅ Patched brotli BUILD.gn"
        fi
    fi
fi

################################################################################
# Step 2: Set up build tools
################################################################################

# Fetch GN if needed
if [ ! -f "bin/gn" ] || ([[ "$TARGET" == "wasm" ]] && ! file bin/gn 2>/dev/null | grep -q "$(uname -m)"); then
    echo "   Fetching GN binary..."
    python3 bin/fetch-gn
fi

# Set up emscripten for WASM builds
if [[ "$TARGET" == "wasm" ]]; then
    if command -v emcc >/dev/null 2>&1; then
        echo "   Setting up symlinks to emscripten tools for Skia..."
        mkdir -p "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten"
        
        EMC_PATH=$(which emcc)
        EMC_REAL_PATH=$(readlink -f "$EMC_PATH" 2>/dev/null || realpath "$EMC_PATH" 2>/dev/null || echo "$EMC_PATH")
        
        if [ -n "$EMSDK" ] && [ -d "$EMSDK/upstream/emscripten" ]; then
            EMSDK_EMSCRIPTEN_DIR="$EMSDK/upstream/emscripten"
            rm -rf "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten" 2>/dev/null || true
            mkdir -p "$SKIA_ROOT/third_party/externals/emsdk/upstream"
            ln -sf "$EMSDK_EMSCRIPTEN_DIR" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten"
            echo "   ✅ Symlinked entire emscripten directory from EMSDK"
        else
            EMC_DIR=$(dirname "$EMC_REAL_PATH")
            if [ -f "$EMC_DIR/emcc.py" ]; then
                EMSDK_EMSCRIPTEN_DIR="$EMC_DIR"
            elif [ -f "$(dirname "$EMC_DIR")/libexec/emcc.py" ]; then
                EMSDK_EMSCRIPTEN_DIR="$(dirname "$EMC_DIR")/libexec"
            else
                EMSDK_EMSCRIPTEN_DIR=""
            fi
            
            if [ -n "$EMSDK_EMSCRIPTEN_DIR" ] && [ -d "$EMSDK_EMSCRIPTEN_DIR" ]; then
                rm -rf "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten" 2>/dev/null || true
                mkdir -p "$SKIA_ROOT/third_party/externals/emsdk/upstream"
                ln -sf "$EMSDK_EMSCRIPTEN_DIR" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten"
                echo "   ✅ Symlinked emscripten directory from: $EMSDK_EMSCRIPTEN_DIR"
                
                EMSDK_CONFIG="$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/.emscripten"
                if [ -f "$EMSDK_CONFIG" ]; then
                    NODE_PATH=$(which node 2>/dev/null || echo "")
                    if [ -n "$NODE_PATH" ] && [ -f "$NODE_PATH" ]; then
                        if grep -q "^NODE_JS" "$EMSDK_CONFIG" 2>/dev/null; then
                            sed -i.bak "s|^NODE_JS.*=.*|NODE_JS = ['$NODE_PATH']|" "$EMSDK_CONFIG" 2>/dev/null || \
                            sed -i '' "s|^NODE_JS.*=.*|NODE_JS = ['$NODE_PATH']|" "$EMSDK_CONFIG" 2>/dev/null || true
                            rm -f "$EMSDK_CONFIG.bak" 2>/dev/null || true
                        fi
                    fi
                fi
            else
                EMPP_PATH=$(which em++)
                EMAR_PATH=$(which emar)
                EMRANLIB_PATH=$(which emranlib 2>/dev/null || which emnm 2>/dev/null || echo "")
                
                ln -sf "$EMC_PATH" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/emcc" 2>/dev/null || true
                ln -sf "$EMPP_PATH" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/em++" 2>/dev/null || true
                ln -sf "$EMAR_PATH" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/emar" 2>/dev/null || true
                if [ -n "$EMRANLIB_PATH" ]; then
                    ln -sf "$EMRANLIB_PATH" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/emranlib" 2>/dev/null || true
                fi
                if [ -f "$(dirname "$EMC_REAL_PATH")/emcc.py" ]; then
                    ln -sf "$(dirname "$EMC_REAL_PATH")/emcc.py" "$SKIA_ROOT/third_party/externals/emsdk/upstream/emscripten/emcc.py" 2>/dev/null || true
                fi
                echo "   ✅ Created individual symlinks for emscripten tools"
            fi
        fi
    fi
    
    export CC=$(which emcc)
    export CXX=$(which em++)
    export AR=$(which emar)
    export NM=$(which emnm)
fi

################################################################################
# Step 3: Generate build files and build Skia
################################################################################

echo "📦 Step 1: Building Skia..."
echo ""

# Build GN args based on target
if [[ "$TARGET" == "wasm" ]]; then
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
        skia_enable_ganesh=false \
        cc=\"$(which emcc)\" \
        cxx=\"$(which em++)\" \
        ar=\"$(which emar)\" \
        extra_cflags=[\"-O3\", \"-sUSE_LIBPNG=1\", \"-Wno-error\", \"-Wno-implicit-function-declaration\"]"
else
    # Binary build - use system libraries
    if [[ "$OSTYPE" == "darwin"* ]]; then
        HOMEBREW_PREFIX=$(brew --prefix 2>/dev/null || echo "/opt/homebrew")
        FREETYPE_INCLUDE="$HOMEBREW_PREFIX/include/freetype2"
        HARFBUZZ_INCLUDE="$HOMEBREW_PREFIX/include/harfbuzz"
        
        # Auto-detect ICU
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
        ICU_INCLUDE="$ICU_CELLAR/include"
        
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
fi

echo "   Generating build files..."
./bin/gn gen "$BUILD_DIR" --args="$GN_ARGS"

# Build gen/skia.h first (required)
echo "   Building gen/skia.h..."
cd "$BUILD_DIR"
ninja gen/skia.h
cd ../..

# Build Skia libraries
echo "   Building Skia libraries (this may take a while)..."
NINJA_JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)
echo "   Using $NINJA_JOBS parallel jobs..."

if [[ "$TARGET" == "wasm" ]]; then
    ninja -C "$BUILD_DIR" :skia \
        modules/skottie:skottie \
        modules/sksg:sksg \
        modules/skshaper:skshaper \
        modules/skunicode:skunicode \
        modules/skresources:skresources \
        modules/jsonreader:jsonreader
    
    # Try to build skparagraph (may fail if ICU/HarfBuzz disabled)
    if ! ninja -C "$BUILD_DIR" modules/skparagraph:skparagraph 2>/dev/null; then
        echo "   ⚠️  Warning: skparagraph failed to build (likely due to disabled ICU/HarfBuzz)"
    fi
else
    ninja -C "$BUILD_DIR" -j$NINJA_JOBS :skia \
        modules/skottie:skottie \
        modules/skparagraph:skparagraph \
        modules/sksg:sksg \
        modules/skshaper:skshaper \
        modules/skunicode:skunicode \
        modules/skresources:skresources \
        modules/jsonreader:jsonreader
fi

echo "✅ Skia built successfully"
echo ""

