#!/bin/bash
set -e

# Quick local test script
# Tests macOS build locally before pushing

echo "🧪 Local Build Test"
echo "==================="
echo ""

# Test macOS build
echo "📱 Testing macOS build..."
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "✅ Running on macOS"
    echo ""
    
    # Check dependencies
    echo "🔍 Checking dependencies..."
    for dep in fontconfig freetype harfbuzz icu4c libpng jpeg-turbo webp ninja python3; do
        if brew list "$dep" &>/dev/null; then
            echo "  ✅ $dep"
        else
            echo "  ❌ $dep (missing)"
        fi
    done
    echo ""
    
    # Build Skia if needed
    if [ ! -f "third_party/skia/skia/out/Release/libskia.a" ]; then
        echo "📦 Building Skia (this will take a while)..."
        ./install_skia.sh
    else
        echo "✅ Skia already built"
    fi
    echo ""
    
    # Build project
    echo "🔨 Building lotio..."
    ./build_local.sh
    echo ""
    
    # Test binary
    echo "🧪 Testing binary..."
    ./lotio --help
    echo ""
    
    echo "✅ macOS build test passed!"
else
    echo "⚠️  Not running on macOS, skipping macOS-specific tests"
    echo "💡 Use test_linux_build.sh for Linux testing"
fi

