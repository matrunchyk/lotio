#!/bin/bash
set -e

# Test script for local WASM build
# This helps verify the WASM build works before pushing to CI

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "🧪 Testing WASM build locally..."
echo ""

# Check for Emscripten
if [ -z "$EMSDK" ]; then
    # Try Homebrew installation
    if command -v emcc >/dev/null 2>&1; then
        echo "✅ Emscripten found (Homebrew installation)"
        echo "   emcc: $(which emcc)"
    else
        echo "❌ Emscripten not found. Please install via:"
        echo "   Option 1 (Recommended): brew install emscripten"
        echo "   Option 2: git clone https://github.com/emscripten-core/emsdk.git"
        echo "            cd emsdk && ./emsdk install latest && ./emsdk activate latest"
        echo "            source ./emsdk_env.sh"
        exit 1
    fi
else
    echo "✅ Emscripten found (Emscripten SDK)"
    echo "   EMSDK: $EMSDK"
fi
echo ""

# Check if Skia is built for WASM
SKIA_LIB_DIR="$PROJECT_ROOT/third_party/skia/skia/out/Wasm"
if [ ! -f "$SKIA_LIB_DIR/libskia.a" ]; then
    echo "📦 Building Skia for WASM (this will take a while)..."
    cd "$PROJECT_ROOT"
    ./scripts/build_skia_wasm.sh
    echo ""
fi

echo "✅ Skia built for WASM"
echo ""

# Build lotio WASM
echo "🔨 Building lotio WASM..."
cd "$PROJECT_ROOT"
./scripts/build_wasm.sh

echo ""
echo "✅ WASM build complete!"
echo ""
echo "📦 Generated files:"
ls -lh lotio.wasm lotio.js wasm/lotio_wasm.js 2>/dev/null || echo "   (Some files may not exist yet)"
