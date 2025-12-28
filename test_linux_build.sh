#!/bin/bash
set -e

# Test Linux build locally using Docker
# This mimics the GitHub Actions Linux build environment

echo "🐳 Testing Linux build locally with Docker..."
echo "=============================================="

# Build and test in Docker
docker build -t lotio-test -f Dockerfile .

# Or run a quick test in a container
echo ""
echo "📦 Running build test in Docker container..."
docker run --rm -v "$(pwd):/workspace" -w /workspace \
    ubuntu:22.04 bash -c "
        set -e
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -qq \
            build-essential clang ninja-build python3 \
            libfontconfig1-dev libfreetype6-dev \
            libx11-dev libxext-dev libxrender-dev \
            mesa-common-dev libgl1-mesa-dev libglu1-mesa-dev \
            libpng-dev libjpeg-dev libicu-dev \
            libharfbuzz-dev libwebp-dev > /dev/null 2>&1
        
        echo '✅ Dependencies installed'
        echo '🔨 Building Skia...'
        chmod +x install_skia.sh
        ./install_skia.sh
        
        echo '🔨 Building lotio...'
        chmod +x build_local.sh
        ./build_local.sh
        
        echo '✅ Build successful!'
        ./lotio --help
    "

echo ""
echo "✅ Linux build test complete!"

