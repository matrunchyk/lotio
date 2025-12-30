#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia/skia"

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

cd "$SKIA_ROOT"

# Fetch GN binary if needed (for correct platform)
if [ ! -f "bin/gn" ] || ! file bin/gn | grep -q "$(uname -m)"; then
    echo "Fetching GN binary for current platform..."
    python3 bin/fetch-gn
fi

# Set up symlinks to system emscripten (if using Homebrew)
if [ -z "$EMSDK" ] && command -v emcc >/dev/null 2>&1; then
    echo "Setting up symlinks to system emscripten..."
    mkdir -p "$PROJECT_ROOT/third_party/externals/emsdk/upstream/emscripten"
    # Symlink emscripten tools
    ln -sf $(which emcc) "$PROJECT_ROOT/third_party/externals/emsdk/upstream/emscripten/emcc" 2>/dev/null || true
    ln -sf $(which em++) "$PROJECT_ROOT/third_party/externals/emsdk/upstream/emscripten/em++" 2>/dev/null || true
    ln -sf $(which emar) "$PROJECT_ROOT/third_party/externals/emsdk/upstream/emscripten/emar" 2>/dev/null || true
    ln -sf $(which emranlib) "$PROJECT_ROOT/third_party/externals/emsdk/upstream/emscripten/emranlib" 2>/dev/null || true
    # Symlink cache directory (for sysroot)
    if [ -d "/opt/homebrew/Cellar/emscripten" ]; then
        EMSDK_VERSION=$(ls -1 /opt/homebrew/Cellar/emscripten | head -1)
        CACHE_DIR="/opt/homebrew/Cellar/emscripten/$EMSDK_VERSION/libexec/cache"
        if [ -d "$CACHE_DIR" ]; then
            rm -rf "$PROJECT_ROOT/third_party/externals/emsdk/upstream/emscripten/cache"
            ln -sf "$CACHE_DIR" "$PROJECT_ROOT/third_party/externals/emsdk/upstream/emscripten/cache"
            echo "Symlinked emscripten cache from Homebrew"
        fi
    fi
fi

# Set up environment to use system emcc
export CC=$(which emcc)
export CXX=$(which em++)
export AR=$(which emar)
export NM=$(which emnm)

# GN args for WebAssembly build
# Note: We use target_cpu="wasm32" and target_os="wasm" to build for WebAssembly
# But we need to tell GN to use our system emcc instead of bundled one
GN_ARGS="target_cpu=\"wasm32\" \
    target_os=\"wasm\" \
    is_official_build=true \
    is_debug=false \
    skia_enable_skottie=true \
    skia_use_freetype=true \
    skia_use_libpng_encode=true \
    skia_use_libpng_decode=true \
    skia_use_libwebp_decode=true \
    skia_use_wuffs=true \
    skia_enable_pdf=false \
    skia_use_fontconfig=false \
    skia_use_icu=false \
    skia_use_harfbuzz=false \
    cc=\"$(which emcc)\" \
    cxx=\"$(which em++)\" \
    ar=\"$(which emar)\" \
    extra_cflags=[\"-O3\", \"-sUSE_LIBPNG=1\", \"-Wno-error\", \"-Wno-implicit-function-declaration\"]"

echo "Generating build files with GN for WebAssembly..."
bin/gn gen out/Wasm --args="$GN_ARGS"

echo "Building Skia for WebAssembly (this will take a while)..."
ninja -C out/Wasm

echo "✅ Skia built for WebAssembly"

