#!/bin/bash
set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKIA_ROOT="$SCRIPT_DIR/third_party/skia/skia"

if [ ! -d "$SKIA_ROOT" ]; then
    echo "Error: Skia directory not found at $SKIA_ROOT"
    exit 1
fi

cd "$SKIA_ROOT"

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
GN_ARGS="target_cpu=\"${TARGET_CPU}\" is_official_build=true is_debug=false skia_enable_skottie=true skia_enable_fontmgr_fontconfig=true skia_enable_fontmgr_custom_directory=true skia_use_freetype=true skia_use_libpng_encode=true skia_use_libpng_decode=true skia_use_libwebp_decode=true skia_use_wuffs=true skia_enable_pdf=false"

# Add CPU-specific optimizations for x64
# Note: Disable AVX/AVX2 for now to avoid ABI issues and header generation problems
# These can be re-enabled later if needed, but they're causing build failures
if [[ "$TARGET_CPU" == "x64" ]]; then
    GN_ARGS="$GN_ARGS skia_use_avx=false skia_use_avx2=false skia_use_avx512=false skia_use_sse41=true skia_use_sse42=true"
fi

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

# Debug: Check for GN warnings
echo ""
echo "Checking for GN warnings..."
cd out/Release
$SKIA_ROOT/bin/gn desc . --root="$SKIA_ROOT" --format=json * > /dev/null 2>&1 || {
    echo "⚠️  GN generated warnings. Running with verbose output:"
    $SKIA_ROOT/bin/gn desc . --root="$SKIA_ROOT" --format=json * 2>&1 | head -20 || true
}
cd "$SKIA_ROOT"

echo "Building Skia with Ninja..."
ninja -C out/Release
echo "Skia built"
