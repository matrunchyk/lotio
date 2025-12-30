#!/bin/bash
set -e

# Debug script to see the actual GN warning that's causing the build to fail
# This mimics what the find_headers.py script does

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia/skia"

if [ ! -d "$SKIA_ROOT" ]; then
    echo "Error: Skia directory not found at $SKIA_ROOT"
    echo "Run ./scripts/install_skia.sh first to fetch Skia"
    exit 1
fi

cd "$SKIA_ROOT"

# Check if GN is available
if [ ! -f "bin/gn" ]; then
    echo "Fetching GN..."
    python3 bin/fetch-gn
fi

# Check if out/Release exists
if [ ! -d "out/Release" ]; then
    echo "Error: out/Release directory not found"
    echo "Run ./scripts/install_skia.sh first to generate build files"
    exit 1
fi

echo "🔍 Checking GN configuration..."
echo "=================================="
echo ""

# Try to see the warning
echo "Running: bin/gn desc . --root=$SKIA_ROOT --format=json *"
echo ""
cd out/Release
$SKIA_ROOT/bin/gn desc . --root="$SKIA_ROOT" --format=json * 2>&1 || true
echo ""

echo "=================================="
echo "If you see warnings above, those are what's causing the build to fail."

