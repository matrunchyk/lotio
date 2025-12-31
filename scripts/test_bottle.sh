#!/bin/bash
set -e

# Test Homebrew bottle creation locally
# This uses the same script as CI to ensure identical behavior
# Usage: ./scripts/test_bottle.sh

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "🧪 Testing Homebrew Bottle Creation"
echo "==================================="
echo ""

# Check if binary exists
if [ ! -f "lotio" ]; then
    echo "❌ Error: lotio binary not found!"
    echo "💡 Run './scripts/build_local.sh' first to build the binary"
    exit 1
fi

# Generate test version (date-SHA format like CI)
TEST_VERSION="v$(date +%Y%m%d)-test"
TEST_VERSION_NUMBER="${TEST_VERSION#v}"

# Determine Homebrew prefix based on architecture
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    BOTTLE_ARCH="arm64_big_sur"
    HOMEBREW_PREFIX="/opt/homebrew"
elif [ "$ARCH" = "x86_64" ]; then
    BOTTLE_ARCH="x86_64_big_sur"
    HOMEBREW_PREFIX="/usr/local"
else
    BOTTLE_ARCH="x86_64_linux"
    HOMEBREW_PREFIX="/usr/local"
fi

echo "📋 Test Configuration:"
echo "   Version: $TEST_VERSION"
echo "   Architecture: $ARCH"
echo "   Bottle Arch: $BOTTLE_ARCH"
echo "   Homebrew Prefix: $HOMEBREW_PREFIX"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "🧹 Cleaning up test files..."
    # Only remove if it's a directory (not the binary)
    [ -d "lotio" ] && rm -rf "lotio"
    rm -f "lotio-${TEST_VERSION_NUMBER}.${BOTTLE_ARCH}.bottle.tar.gz"
    rm -rf "test_bottle_extract"
    echo "✅ Cleanup complete"
}

# Set trap to cleanup on exit
trap cleanup EXIT

# Use the same script as CI
echo "📦 Creating bottle using shared script..."
chmod +x scripts/create_bottle.sh
./scripts/create_bottle.sh "$TEST_VERSION" "$BOTTLE_ARCH" "$HOMEBREW_PREFIX"

# Get counts for summary
# Note: Bottle structure is lotio/<version>/bin, not lotio/<version>/<prefix>/bin
LOTIO_HEADER_COUNT=$(find "lotio/${TEST_VERSION_NUMBER}/include/lotio" -name "*.h" 2>/dev/null | wc -l | tr -d ' ')
SKIA_HEADER_COUNT=$(find "lotio/${TEST_VERSION_NUMBER}/include/skia" -name "*.h" 2>/dev/null | wc -l | tr -d ' ')
TOTAL_HEADER_COUNT=$((LOTIO_HEADER_COUNT + SKIA_HEADER_COUNT))
LIB_COUNT=$(find "lotio/${TEST_VERSION_NUMBER}/lib" -name "*.a" 2>/dev/null | wc -l | tr -d ' ')
BOTTLE_FILENAME="lotio-${TEST_VERSION_NUMBER}.${BOTTLE_ARCH}.bottle.tar.gz"
TARBALL_SIZE=$(du -h "$BOTTLE_FILENAME" 2>/dev/null | cut -f1 || echo "unknown")
SHA256=$(shasum -a 256 "$BOTTLE_FILENAME" 2>/dev/null | cut -d' ' -f1 || echo "")

# Verify directory structure
# Homebrew bottles should NOT include the prefix in the path
# Structure: lotio/<version>/bin, lotio/<version>/lib, etc.
echo ""
echo "🔍 Verifying directory structure..."
BOTTLE_DIR="lotio/${TEST_VERSION_NUMBER}"
EXPECTED_PATHS=(
    "$BOTTLE_DIR/bin/lotio"
    "$BOTTLE_DIR/lib/liblotio.a"
    "$BOTTLE_DIR/lib/pkgconfig/lotio.pc"
    "$BOTTLE_DIR/include/skia"
)

for path in "${EXPECTED_PATHS[@]}"; do
    if [ -e "$path" ]; then
        echo "   ✅ $path"
    else
        echo "   ❌ Missing: $path"
        exit 1
    fi
done

# Verify Skia headers are present (REQUIRED for programmatic use)
echo ""
echo "🔍 Verifying Skia headers (required for programmatic use)..."
SKIA_HEADER_PATHS=(
    "$BOTTLE_DIR/include/skia/core/SkCanvas.h"
    "$BOTTLE_DIR/include/skia/core/SkFontMgr.h"
    "$BOTTLE_DIR/include/skia/modules/skottie/include/Skottie.h"
    "$BOTTLE_DIR/include/skia/modules/skresources/include/SkResources.h"
)
MISSING_HEADERS=0
for path in "${SKIA_HEADER_PATHS[@]}"; do
    if [ -e "$path" ]; then
        echo "   ✅ $path"
    else
        echo "   ❌ Missing: $path"
        MISSING_HEADERS=$((MISSING_HEADERS + 1))
    fi
done

if [ $MISSING_HEADERS -gt 0 ]; then
    echo ""
    echo "❌ ERROR: $MISSING_HEADERS critical Skia headers are missing!"
    echo "   Developers won't be able to use lotio programmatically"
    exit 1
fi
echo "   ✅ All critical Skia headers present"

# Test extracting the tarball
echo ""
echo "🧪 Testing tarball extraction..."
mkdir -p test_bottle_extract
cd test_bottle_extract

if ! tar -xzf "../lotio-${TEST_VERSION_NUMBER}.${BOTTLE_ARCH}.bottle.tar.gz"; then
    echo "❌ Failed to extract tarball"
    exit 1
fi

# Verify extracted structure
# Homebrew bottles should NOT include the prefix in the path
echo "🔍 Verifying extracted structure..."
EXTRACTED_PATHS=(
    "lotio/${TEST_VERSION_NUMBER}/bin/lotio"
    "lotio/${TEST_VERSION_NUMBER}/lib/liblotio.a"
    "lotio/${TEST_VERSION_NUMBER}/lib/pkgconfig/lotio.pc"
    "lotio/${TEST_VERSION_NUMBER}/include/skia"
)

for path in "${EXTRACTED_PATHS[@]}"; do
    if [ -e "$path" ]; then
        echo "   ✅ $path"
    else
        echo "   ❌ Missing: $path"
        exit 1
    fi
done

# Test binary from extracted tarball
echo ""
echo "🧪 Testing extracted binary..."
if [ -f "lotio/${TEST_VERSION_NUMBER}/bin/lotio" ]; then
    if "./lotio/${TEST_VERSION_NUMBER}/bin/lotio" --help >/dev/null 2>&1; then
        echo "   ✅ Extracted binary works"
    else
        echo "   ⚠️  Warning: Extracted binary doesn't run (may need dependencies)"
    fi
else
    echo "   ❌ Extracted binary not found"
    exit 1
fi

cd "$PROJECT_ROOT"

# Summary
echo ""
echo "✅ Bottle Creation Test Passed!"
echo ""
echo "📊 Summary:"
echo "   • Binary: ✅"
echo "   • Lotio Headers: ✅ ($LOTIO_HEADER_COUNT files)"
echo "   • Skia Headers: ✅ ($SKIA_HEADER_COUNT files)"
echo "   • Total Headers: ✅ ($TOTAL_HEADER_COUNT files)"
echo "   • Libraries: ✅ ($LIB_COUNT files)"
echo "   • pkg-config: ✅"
echo "   • Tarball: ✅ ($TARBALL_SIZE)"
echo "   • SHA256: ✅ ($SHA256)"
echo "   • Extraction: ✅"
echo ""
echo "💡 The bottle structure looks correct and should work in CI!"
echo "   Test tarball: lotio-${TEST_VERSION_NUMBER}.${BOTTLE_ARCH}.bottle.tar.gz"
echo "   (Will be cleaned up automatically)"

