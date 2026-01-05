#!/bin/bash
set -e

################################################################################
# Build and Push Multi-Arch Skia Docker Image
#
# This script builds Skia Docker image for both arm64 and x86_64 platforms
# and pushes it to Docker Hub as matrunchyk/skia:latest
#
# Usage:
#   scripts/build_skia_docker_multi.sh [--no-cache] [--tag=TAG]
#
# Prerequisites:
#   - Docker must be installed and running
#   - You must be logged in to Docker Hub: docker login
#   - BuildKit must be enabled (default in Docker 20.10+)
#
# Environment Variables:
#   - DOCKER_USERNAME: Docker Hub username (default: matrunchyk)
#   - DOCKER_TAG: Tag to use (default: latest)
################################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Default values
DOCKER_USERNAME="${DOCKER_USERNAME:-matrunchyk}"
DOCKER_TAG="${DOCKER_TAG:-latest}"
IMAGE_NAME="${DOCKER_USERNAME}/skia:${DOCKER_TAG}"
NO_CACHE=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --no-cache)
            NO_CACHE=true
            shift
            ;;
        --tag=*)
            DOCKER_TAG="${1#*=}"
            IMAGE_NAME="${DOCKER_USERNAME}/skia:${DOCKER_TAG}"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--no-cache] [--tag=TAG]"
            exit 1
            ;;
    esac
done

echo "🐳 Building and Pushing Multi-Arch Skia Docker Image"
echo "====================================================="
echo "Image: $IMAGE_NAME"
echo "Platforms: linux/arm64, linux/amd64"
echo ""

# Check if Docker is running
if ! docker info >/dev/null 2>&1; then
    echo "❌ ERROR: Docker is not running"
    echo "   Please start Docker and try again"
    exit 1
fi

# Check if logged in to Docker Hub
# Check Docker config file for authentication
DOCKER_CONFIG="${DOCKER_CONFIG:-$HOME/.docker/config.json}"
if [ ! -f "$DOCKER_CONFIG" ]; then
    echo "❌ ERROR: Not logged in to Docker Hub"
    echo "   Docker config file not found: $DOCKER_CONFIG"
    echo "   Please run: docker login"
    echo "   This script requires authentication to push images"
    exit 1
fi

# Check if config contains authentication (either auth token or credsStore/credHelpers)
if ! grep -q '"auths"' "$DOCKER_CONFIG" 2>/dev/null && ! grep -q '"credsStore"' "$DOCKER_CONFIG" 2>/dev/null && ! grep -q '"credHelpers"' "$DOCKER_CONFIG" 2>/dev/null; then
    echo "❌ ERROR: Not logged in to Docker Hub"
    echo "   No authentication found in Docker config"
    echo "   Please run: docker login"
    echo "   This script requires authentication to push images"
    exit 1
fi

echo "✅ Docker Hub authentication verified"
echo ""

# Set up buildx builder
echo "🔧 Setting up buildx builder..."
BUILDER_NAME="multiarch-skia"

# Check if builder exists
if ! docker buildx ls | grep -q "$BUILDER_NAME"; then
    echo "   Creating new buildx builder: $BUILDER_NAME"
    docker buildx create --name "$BUILDER_NAME" --use --driver docker-container
    echo "   ✅ Builder created"
else
    echo "   Using existing builder: $BUILDER_NAME"
    docker buildx use "$BUILDER_NAME"
fi

# Bootstrap the builder (needed for multi-platform builds)
echo "   Bootstrapping builder for multi-platform support..."
docker buildx inspect --bootstrap >/dev/null 2>&1 || true
echo "   ✅ Builder ready"
echo ""

# Build command
echo ""
echo "🔨 Building multi-arch image..."
echo "   This may take a while (building for both platforms)..."
echo ""

# Construct docker buildx command as array
DOCKER_CMD=(
    docker
    buildx
    build
    --platform
    "linux/arm64,linux/amd64"
    --file
    "$PROJECT_ROOT/Dockerfile.skia"
    --tag
    "$IMAGE_NAME"
)

# Add cache options if not using --no-cache
# Use separate cache ref to avoid conflicts with image manifest
CACHE_REF="${DOCKER_USERNAME}/skia:cache"
if [[ "$NO_CACHE" == "false" ]]; then
    DOCKER_CMD+=(
        --cache-from
        "type=registry,ref=${CACHE_REF}"
        --cache-to
        "type=registry,ref=${CACHE_REF},mode=max"
    )
    echo "📦 Using cache from registry (separate cache ref: ${CACHE_REF})"
    echo "   Note: Cache import may fail on first build (cache doesn't exist yet) - this is normal"
else
    DOCKER_CMD+=(--no-cache)
    echo "🔄 Building without cache"
fi

# Add push flag and context
DOCKER_CMD+=(--push "$PROJECT_ROOT")

# Execute the command
"${DOCKER_CMD[@]}"

echo ""
echo "✅ Build and push complete!"
echo ""
echo "📦 Image pushed: $IMAGE_NAME"
echo ""
echo "🧪 Verify the manifest:"
echo "   docker manifest inspect $IMAGE_NAME"
echo ""
echo "🧪 Test a specific platform:"
echo "   docker run --platform linux/arm64 --rm $IMAGE_NAME"
echo "   docker run --platform linux/amd64 --rm $IMAGE_NAME"
echo ""

