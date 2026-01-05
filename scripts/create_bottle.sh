#!/bin/bash
set -e

# Create Homebrew bottle
# This script creates a Homebrew bottle with the correct directory structure
# Usage: ./scripts/create_bottle.sh <VERSION> [BOTTLE_ARCH] [HOMEBREW_PREFIX]
#   VERSION: Version string in semver format (e.g., "v1.2.3" or "1.2.3")
#   BOTTLE_ARCH: Architecture (e.g., "arm64_big_sur", "x86_64_big_sur") - auto-detected if not provided
#   HOMEBREW_PREFIX: Homebrew prefix (e.g., "/opt/homebrew", "/usr/local") - auto-detected if not provided
#
# Outputs:
#   - Bottle tarball: lotio-<VERSION_NUMBER>.<BOTTLE_ARCH>.bottle.tar.gz
#   - SHA256 checksum (printed to stdout)
#   - Sets BOTTLE_FILENAME and BOTTLE_SHA256 environment variables (if sourced)

VERSION="$1"
VERSION_NUMBER="${VERSION#v}"  # Remove 'v' prefix if present

# Auto-detect architecture and prefix if not provided
if [ -z "$2" ] || [ -z "$3" ]; then
    ARCH=$(uname -m)
    if [ "$ARCH" = "arm64" ]; then
        BOTTLE_ARCH="${2:-arm64_big_sur}"
        HOMEBREW_PREFIX="${3:-/opt/homebrew}"
    elif [ "$ARCH" = "x86_64" ]; then
        BOTTLE_ARCH="${2:-x86_64_big_sur}"
        HOMEBREW_PREFIX="${3:-/usr/local}"
    else
        BOTTLE_ARCH="${2:-x86_64_linux}"
        HOMEBREW_PREFIX="${3:-/usr/local}"
    fi
else
    BOTTLE_ARCH="$2"
    HOMEBREW_PREFIX="$3"
fi

if [ -z "$VERSION" ]; then
    echo "❌ Error: VERSION is required"
    echo "Usage: $0 <VERSION> [BOTTLE_ARCH] [HOMEBREW_PREFIX]"
    exit 1
fi

# Verify binary exists
if [ ! -f "lotio" ]; then
    echo "❌ Error: lotio binary not found!"
    exit 1
fi

# Create bottle directory structure WITHOUT prefix
# Homebrew bottles should NOT include the prefix in the path
# Structure: lotio/<version>/bin, lotio/<version>/lib, etc.
# Homebrew will automatically relocate to the correct prefix during installation
# Note: Create in temp location first to avoid conflict with 'lotio' binary
TEMP_BOTTLE_DIR=".bottle_temp/lotio/${VERSION_NUMBER}"
mkdir -p "$TEMP_BOTTLE_DIR/bin"
mkdir -p "$TEMP_BOTTLE_DIR/include/lotio/core"
mkdir -p "$TEMP_BOTTLE_DIR/include/lotio/text"
mkdir -p "$TEMP_BOTTLE_DIR/include/lotio/utils"
mkdir -p "$TEMP_BOTTLE_DIR/lib/pkgconfig"

# Copy binary
echo "📦 Copying binary..."
cp lotio "$TEMP_BOTTLE_DIR/bin/" || { echo "❌ Failed to copy binary"; exit 1; }

# Copy lotio headers
echo "📦 Copying lotio headers..."
if ! cp src/core/*.h "$TEMP_BOTTLE_DIR/include/lotio/core/" 2>/dev/null; then
    echo "⚠️  Warning: No core headers found"
fi
if ! cp src/text/*.h "$TEMP_BOTTLE_DIR/include/lotio/text/" 2>/dev/null; then
    echo "⚠️  Warning: No text headers found"
fi
if ! cp src/utils/*.h "$TEMP_BOTTLE_DIR/include/lotio/utils/" 2>/dev/null; then
    echo "⚠️  Warning: No utils headers found"
fi

# Copy Skia headers (REQUIRED for programmatic use)
echo "📦 Copying Skia headers..."
SKIA_ROOT="third_party/skia/skia"
SKIA_INCLUDE_DIR="$SKIA_ROOT/include"
SKIA_MODULES_DIR="$SKIA_ROOT/modules"
SKIA_GEN_DIR="$SKIA_ROOT/out/Release/gen"

# Copy main Skia headers (REQUIRED)
if [ ! -d "$SKIA_INCLUDE_DIR" ]; then
    echo "❌ ERROR: Skia include directory not found at $SKIA_INCLUDE_DIR"
    echo "   Cannot create bottle without Skia headers - developers need them!"
    exit 1
fi

mkdir -p "$TEMP_BOTTLE_DIR/include/skia"
if ! cp -r "$SKIA_INCLUDE_DIR"/* "$TEMP_BOTTLE_DIR/include/skia/" 2>/dev/null; then
    echo "❌ ERROR: Failed to copy Skia core headers"
    echo "   This will prevent developers from using lotio programmatically"
    exit 1
fi

# Verify critical headers were copied
if [ ! -d "$TEMP_BOTTLE_DIR/include/skia/core" ]; then
    echo "❌ ERROR: Skia core headers not found after copy"
    exit 1
fi
echo "✅ Copied Skia core headers"

# Copy module headers (REQUIRED)
if [ ! -d "$SKIA_MODULES_DIR" ]; then
    echo "❌ ERROR: Skia modules directory not found at $SKIA_MODULES_DIR"
    exit 1
fi

MODULE_COUNT=0
CRITICAL_MODULES=("skottie" "skresources")  # Critical for lotio
for module in skottie skparagraph sksg skshaper skunicode skresources jsonreader; do
    # jsonreader has a different structure (no include/ subdirectory)
    if [ "$module" = "jsonreader" ]; then
        # jsonreader has headers directly in the module directory
        if [ -f "$SKIA_MODULES_DIR/$module/SkJSONReader.h" ]; then
            mkdir -p "$TEMP_BOTTLE_DIR/include/skia/modules/$module"
            if cp "$SKIA_MODULES_DIR/$module/SkJSONReader.h" "$TEMP_BOTTLE_DIR/include/skia/modules/$module/" 2>/dev/null; then
                MODULE_COUNT=$((MODULE_COUNT + 1))
            fi
        fi
    elif [ -d "$SKIA_MODULES_DIR/$module/include" ]; then
        # Copy the include directory itself to preserve the structure
        # Source: modules/skottie/include/Skottie.h
        # Dest: include/skia/modules/skottie/include/Skottie.h
        mkdir -p "$TEMP_BOTTLE_DIR/include/skia/modules/$module"
        if ! cp -r "$SKIA_MODULES_DIR/$module/include" "$TEMP_BOTTLE_DIR/include/skia/modules/$module/" 2>/dev/null; then
            # Check if this is a critical module
            is_critical=0
            for critical in "${CRITICAL_MODULES[@]}"; do
                if [ "$module" = "$critical" ]; then
                    is_critical=1
                    break
                fi
            done
            if [ $is_critical -eq 1 ]; then
                echo "❌ ERROR: Failed to copy critical module $module headers"
                exit 1
            else
                echo "⚠️  Warning: Failed to copy $module headers"
            fi
        else
            MODULE_COUNT=$((MODULE_COUNT + 1))
        fi
    else
        # Check if this is a critical module
        is_critical=0
        for critical in "${CRITICAL_MODULES[@]}"; do
            if [ "$module" = "$critical" ]; then
                is_critical=1
                break
            fi
        done
        if [ $is_critical -eq 1 ]; then
            echo "❌ ERROR: Critical module $module headers not found at $SKIA_MODULES_DIR/$module/include"
            exit 1
        else
            echo "⚠️  Warning: $module/include directory not found"
        fi
    fi
done

if [ $MODULE_COUNT -eq 0 ]; then
    echo "❌ ERROR: No Skia module headers were copied"
    exit 1
fi

# Verify skottie headers (critical for lotio)
if [ ! -f "$TEMP_BOTTLE_DIR/include/skia/modules/skottie/include/Skottie.h" ]; then
    echo "❌ ERROR: Skottie.h not found - lotio headers require this!"
    echo "   Expected: $TEMP_BOTTLE_DIR/include/skia/modules/skottie/include/Skottie.h"
    exit 1
fi

# Verify skresources headers (critical for lotio)
if [ ! -f "$TEMP_BOTTLE_DIR/include/skia/modules/skresources/include/SkResources.h" ]; then
    echo "❌ ERROR: SkResources.h not found - lotio headers require this!"
    echo "   Expected: $TEMP_BOTTLE_DIR/include/skia/modules/skresources/include/SkResources.h"
    exit 1
fi

echo "✅ Copied Skia module headers ($MODULE_COUNT modules)"

# Copy generated header if it exists (optional but recommended)
if [ -f "$SKIA_GEN_DIR/skia.h" ]; then
    mkdir -p "$TEMP_BOTTLE_DIR/include/skia/gen"
    if cp "$SKIA_GEN_DIR/skia.h" "$TEMP_BOTTLE_DIR/include/skia/gen/" 2>/dev/null; then
        echo "✅ Copied generated skia.h"
    fi
elif [ -d "$SKIA_GEN_DIR" ]; then
    # Copy all generated headers if the directory exists
    mkdir -p "$TEMP_BOTTLE_DIR/include/skia/gen"
    if cp -r "$SKIA_GEN_DIR"/*.h "$TEMP_BOTTLE_DIR/include/skia/gen/" 2>/dev/null; then
        echo "✅ Copied generated headers"
    fi
fi

# Copy liblotio.a library
echo "📦 Copying liblotio.a..."
if [ -f "liblotio.a" ]; then
    cp liblotio.a "$TEMP_BOTTLE_DIR/lib/" || { echo "❌ Failed to copy liblotio.a"; exit 1; }
    echo "✅ Copied liblotio.a"
else
    echo "⚠️  Warning: liblotio.a not found (build may not have created it)"
fi

# Copy Skia static libraries
echo "📦 Copying Skia libraries..."
SKIA_LIB_DIR="third_party/skia/skia/out/Release"
LIB_COUNT=0
for lib in skottie skia skparagraph sksg skshaper skunicode_icu skunicode_core skresources jsonreader; do
    if [ -f "$SKIA_LIB_DIR/lib${lib}.a" ]; then
        cp "$SKIA_LIB_DIR/lib${lib}.a" "$TEMP_BOTTLE_DIR/lib/" || { echo "❌ Failed to copy lib${lib}.a"; exit 1; }
        LIB_COUNT=$((LIB_COUNT + 1))
    else
        echo "⚠️  Warning: $SKIA_LIB_DIR/lib${lib}.a not found"
    fi
done
echo "✅ Copied $LIB_COUNT Skia libraries"

# Create pkg-config file with relocatable prefix
echo "📦 Creating pkg-config file..."
cat > "$TEMP_BOTTLE_DIR/lib/pkgconfig/lotio.pc" << EOF
prefix=\${pcfiledir}/../..
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${exec_prefix}/include

Name: lotio
Description: High-performance Lottie animation frame renderer using Skia
Version: ${VERSION_NUMBER}
Libs: -L\${libdir} -llotio -lskottie -lskia -lskparagraph -lsksg -lskshaper -lskunicode_icu -lskunicode_core -lskresources -ljsonreader
Cflags: -I\${includedir} -I\${includedir}/skia -I\${includedir}/skia/gen
EOF

# Move temp directory to final location (avoiding conflict with 'lotio' binary)
echo "📁 Moving bottle structure to final location..."
if [ -d ".bottle_temp/lotio" ]; then
    # Remove the binary file first (we already copied it into the bottle structure)
    if [ -f "lotio" ] && [ ! -d "lotio" ]; then
        rm -f "lotio" || { echo "⚠️  Warning: Could not remove lotio binary"; }
    fi
    # Remove any existing lotio directory (shouldn't exist, but be safe)
    [ -d "lotio" ] && rm -rf "lotio"
    mv ".bottle_temp/lotio" "lotio" || { 
        echo "❌ Failed to move bottle directory"; 
        exit 1; 
    }
    rm -rf ".bottle_temp"
else
    echo "❌ Error: .bottle_temp/lotio directory not found!"
    exit 1
fi

# Verify directory structure before creating tarball
if [ ! -d "lotio" ]; then
    echo "❌ Error: lotio directory not found!"
    exit 1
fi

# Create tarball (Homebrew bottle format)
# The tarball should contain: lotio/<version>/bin, lotio/<version>/lib, etc.
# Homebrew will automatically relocate to the correct prefix during installation
echo "📦 Creating tarball..."
BOTTLE_FILENAME="lotio-${VERSION_NUMBER}.${BOTTLE_ARCH}.bottle.tar.gz"
tar -czf "$BOTTLE_FILENAME" "lotio" || { echo "❌ Failed to create tarball"; exit 1; }

# Verify tarball was created
if [ ! -f "$BOTTLE_FILENAME" ]; then
    echo "❌ Error: Tarball was not created!"
    exit 1
fi

# Calculate SHA256
SHA256=$(shasum -a 256 "$BOTTLE_FILENAME" | cut -d' ' -f1)
if [ -z "$SHA256" ]; then
    echo "❌ Error: Failed to calculate SHA256"
    exit 1
fi

echo "✅ Bottle created successfully: $BOTTLE_FILENAME"
echo "✅ SHA256: $SHA256"

# Export variables for use in CI/workflows (if script is sourced)
export BOTTLE_FILENAME
export BOTTLE_SHA256="$SHA256"
export BOTTLE_ARCH

# Output for GitHub Actions (if running in CI)
if [ -n "$GITHUB_OUTPUT" ]; then
    echo "SHA256=$SHA256" >> $GITHUB_OUTPUT
    echo "FILENAME=$BOTTLE_FILENAME" >> $GITHUB_OUTPUT
    echo "ARCH=$BOTTLE_ARCH" >> $GITHUB_OUTPUT
fi

