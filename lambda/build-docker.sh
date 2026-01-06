#!/bin/bash
set -euo pipefail

# Build script for Lambda Docker image with multi-platform support
# Handles architecture mapping: amd64 -> x86_64 for AWS Lambda images

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default values
PLATFORMS="${PLATFORMS:-linux/arm64,linux/amd64}"
IMAGE_TAG="${IMAGE_TAG:-lotio-lambda:local}"
LOTIO_FFMPEG_IMAGE="${LOTIO_FFMPEG_IMAGE:-matrunchyk/lotio-ffmpeg:latest}"
LOAD="${LOAD:-true}"
PUSH="${PUSH:-false}"

cd "$PROJECT_ROOT"

# Check if building for multiple platforms
IFS=',' read -ra PLATFORM_ARRAY <<< "$PLATFORMS"
PLATFORM_COUNT=${#PLATFORM_ARRAY[@]}

if [ "$PLATFORM_COUNT" -gt 1 ]; then
    echo "Building for multiple platforms: $PLATFORMS"
    echo "Note: AWS Lambda uses 'x86_64' for AMD64 architecture"
    echo ""
    
    # Build each platform separately and combine
    # This is necessary because AWS Lambda uses different image tags for each architecture
    echo "Building for each platform separately..."
    
    for platform in "${PLATFORM_ARRAY[@]}"; do
        # Extract architecture from platform (e.g., linux/arm64 -> arm64)
        arch=$(echo "$platform" | cut -d'/' -f2)
        
        # Map architecture to AWS Lambda format
        if [ "$arch" = "amd64" ]; then
            lambda_arch="x86_64"
        else
            lambda_arch="$arch"
        fi
        
        echo ""
        echo "Building for $platform (Lambda arch: $lambda_arch)..."
        
        docker buildx build \
            --platform "$platform" \
            -t "${IMAGE_TAG}-${arch}" \
            -f lambda/Dockerfile \
            --build-arg LOTIO_FFMPEG_IMAGE="$LOTIO_FFMPEG_IMAGE" \
            --build-arg LAMBDA_ARCH="$lambda_arch" \
            ${LOAD:+--load} \
            ${PUSH:+--push} \
            .
    done
    
    # If loading, create a manifest for multi-platform (requires buildx)
    if [ "$LOAD" = "true" ]; then
        echo ""
        echo "Note: For multi-platform images, use 'docker buildx build' with --load"
        echo "or push to a registry and pull the multi-platform manifest."
        echo ""
        echo "Built images:"
        for platform in "${PLATFORM_ARRAY[@]}"; do
            arch=$(echo "$platform" | cut -d'/' -f2)
            echo "  - ${IMAGE_TAG}-${arch}"
        done
    fi
else
    # Single platform build
    platform="$PLATFORMS"
    arch=$(echo "$platform" | cut -d'/' -f2)
    
    # Map architecture to AWS Lambda format
    if [ "$arch" = "amd64" ]; then
        lambda_arch="x86_64"
    else
        lambda_arch="$arch"
    fi
    
    echo "Building for $platform (Lambda arch: $lambda_arch)..."
    
    docker buildx build \
        --platform "$platform" \
        -t "$IMAGE_TAG" \
        -f lambda/Dockerfile \
        --build-arg LOTIO_FFMPEG_IMAGE="$LOTIO_FFMPEG_IMAGE" \
        --build-arg LAMBDA_ARCH="$lambda_arch" \
        ${LOAD:+--load} \
        ${PUSH:+--push} \
        .
fi

