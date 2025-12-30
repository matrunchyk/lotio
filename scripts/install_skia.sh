#!/bin/bash
set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SKIA_ROOT="$PROJECT_ROOT/third_party/skia/skia"

if [ ! -d "$SKIA_ROOT" ]; then
    echo "Error: Skia directory not found at $SKIA_ROOT"
    exit 1
fi

cd "$SKIA_ROOT"

# Clean build directory if it exists (in case of interrupted builds)
if [ -d "out/Release" ]; then
    echo "Cleaning previous build directory (handling interrupted builds)..."
    rm -rf out/Release
fi

echo "Fetching GN binary..."
python3 bin/fetch-gn
echo "GN binary fetched"

# Detect macOS and set Homebrew paths
if [[ "$OSTYPE" == "darwin"* ]]; then
    HOMEBREW_PREFIX=$(brew --prefix)
    FONTCONFIG_INCLUDE="$HOMEBREW_PREFIX/include/fontconfig"
    FREETYPE_INCLUDE="$HOMEBREW_PREFIX/include/freetype2"
    # ICU is in a versioned directory
    ICU_PREFIX=$(brew --prefix icu4c 2>/dev/null || echo "$HOMEBREW_PREFIX/opt/icu4c@77")
    ICU_INCLUDE="$ICU_PREFIX/include"
    
    echo "Detected macOS - using Homebrew paths:"
    echo "  Fontconfig: $FONTCONFIG_INCLUDE"
    echo "  Freetype: $FREETYPE_INCLUDE"
    echo "  ICU: $ICU_INCLUDE"
    
    EXTRA_CFLAGS="-O3 -march=native -I$HOMEBREW_PREFIX/include -I$FREETYPE_INCLUDE -I$ICU_INCLUDE"
else
    EXTRA_CFLAGS="-O3 -march=native"
fi

echo "Generating build files..."

# Detect architecture
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS - detect architecture
    ARCH=$(uname -m)
    if [[ "$ARCH" == "arm64" ]]; then
        TARGET_CPU="arm64"
    else
        TARGET_CPU="x64"
    fi
else
    # Linux - detect architecture
    ARCH=$(uname -m)
    if [[ "$ARCH" == "aarch64" ]] || [[ "$ARCH" == "arm64" ]]; then
        TARGET_CPU="arm64"
    else
        TARGET_CPU="x64"
    fi
fi

# Build GN args - use single line format for better compatibility
# Note: skia_use_avx, skia_use_avx2, etc. are not valid GN arguments in Skia
# CPU optimizations are handled automatically by the compiler
GN_ARGS="target_cpu=\"${TARGET_CPU}\" is_official_build=true is_debug=false skia_enable_skottie=true skia_enable_fontmgr_fontconfig=true skia_enable_fontmgr_custom_directory=true skia_use_freetype=true skia_use_libpng_encode=true skia_use_libpng_decode=true skia_use_libwebp_decode=true skia_use_wuffs=true skia_enable_pdf=false"

# Build extra_cflags array
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS - use Homebrew paths
    HARFBUZZ_INCLUDE="$HOMEBREW_PREFIX/include/harfbuzz"
    GN_ARGS="$GN_ARGS extra_cflags=[\"-O3\", \"-I$HOMEBREW_PREFIX/include\", \"-I$FREETYPE_INCLUDE\", \"-I$ICU_INCLUDE\", \"-I$HARFBUZZ_INCLUDE\"]"
else
    # Linux - use system paths and suppress AVX warnings
    GN_ARGS="$GN_ARGS extra_cflags=[\"-O3\", \"-Wno-psabi\", \"-I/usr/include\", \"-I/usr/include/freetype2\", \"-I/usr/include/harfbuzz\", \"-I/usr/include/fontconfig\"]"
fi

echo "Generating build files with GN..."
echo "GN args: $GN_ARGS"
bin/gn gen out/Release --args="$GN_ARGS"

# Build gen/skia.h explicitly before full build (required for compilation)
# This prevents failures during parallel ninja builds where gen/skia.h might be built too late
echo ""
echo "Building gen/skia.h explicitly (required before compilation)..."
cd out/Release
ninja gen/skia.h || {
    echo "❌ ERROR: Failed to generate gen/skia.h"
    echo "This indicates a GN configuration issue."
    echo "Checking GN args..."
    bin/gn args --list . 2>&1 | grep -E "(target_cpu|skia_enable)" | head -10 || true
    echo ""
    echo "Attempting to see GN warning..."
    bin/gn desc . --root="$SKIA_ROOT" --format=json "*" 2>&1 | head -30 || true
    cd "$SKIA_ROOT"
    exit 1
}
cd "$SKIA_ROOT"

# Verify gen/skia.h was created
if [ ! -f "out/Release/gen/skia.h" ]; then
    echo "❌ ERROR: gen/skia.h was not created after explicit build"
    exit 1
fi
echo "✅ gen/skia.h built successfully"

echo "Building Skia with Ninja..."
ninja -C out/Release
echo "Skia built"
