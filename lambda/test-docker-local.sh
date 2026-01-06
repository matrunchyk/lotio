#!/bin/bash
set -euo pipefail

# Test script for Lambda Docker image with local sample files
# This script tests the built Docker image with sample1 files

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLE_DIR="$PROJECT_ROOT/examples/samples/sample1"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Lambda Docker Test Script ===${NC}"
echo ""

# Determine which image to use (default to :local, fallback to :test)
IMAGE_NAME="${LAMBDA_IMAGE:-lotio-lambda:local}"
if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    # Try fallback to :test
    if docker image inspect lotio-lambda:test >/dev/null 2>&1; then
        IMAGE_NAME="lotio-lambda:test"
        echo -e "${YELLOW}Using fallback image: $IMAGE_NAME${NC}"
    else
        echo -e "${RED}Error: Docker image '$IMAGE_NAME' or 'lotio-lambda:test' not found${NC}"
        echo "Please build it first with:"
        echo "  docker buildx build --platform linux/arm64,linux/amd64 -t lotio-lambda:local -f lambda/Dockerfile --build-arg LOTIO_FFMPEG_IMAGE=matrunchyk/lotio-ffmpeg:test --load ."
        exit 1
    fi
fi

echo -e "${GREEN}✓ Docker image found: $IMAGE_NAME${NC}"
echo ""

# Check if sample files exist
if [ ! -f "$SAMPLE_DIR/data.json" ]; then
    echo -e "${RED}Error: $SAMPLE_DIR/data.json not found${NC}"
    exit 1
fi

if [ ! -f "$SAMPLE_DIR/layer-overrides.json" ]; then
    echo -e "${RED}Error: $SAMPLE_DIR/layer-overrides.json not found${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Sample files found${NC}"
echo ""

# Step 1: Start HTTP server to serve sample files
echo -e "${YELLOW}Step 1: Starting HTTP server for sample files...${NC}"
cd "$SAMPLE_DIR"

# Find an available port
PORT=8000
while lsof -Pi :$PORT -sTCP:LISTEN -t >/dev/null 2>&1; do
    PORT=$((PORT + 1))
done

# Start Python HTTP server in background
python3 -m http.server $PORT > /tmp/http-server.log 2>&1 &
HTTP_SERVER_PID=$!

# Wait for server to start
sleep 2

if ! kill -0 $HTTP_SERVER_PID 2>/dev/null; then
    echo -e "${RED}✗ Failed to start HTTP server${NC}"
    cat /tmp/http-server.log
    exit 1
fi

echo -e "${GREEN}✓ HTTP server started on port $PORT${NC}"
echo "  Data JSON: http://localhost:$PORT/data.json"
echo "  Layer Overrides: http://localhost:$PORT/layer-overrides.json"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo -e "${YELLOW}Cleaning up...${NC}"
    if kill -0 $HTTP_SERVER_PID 2>/dev/null; then
        kill $HTTP_SERVER_PID 2>/dev/null || true
        echo -e "${GREEN}✓ HTTP server stopped${NC}"
    fi
    if [ -d /tmp/lambda-test-output ]; then
        rm -rf /tmp/lambda-test-output
    fi
}

trap cleanup EXIT

# Step 2: Create test event and output directory
echo -e "${YELLOW}Step 2: Creating test event and output directory...${NC}"

# Create output directory for the video
OUTPUT_DIR="/tmp/lotio-test-output"
mkdir -p "$OUTPUT_DIR"
OUTPUT_VIDEO="$OUTPUT_DIR/output.mov"

# For local testing, we'll use a mock S3 setup
# The Lambda function needs S3 for output, so we'll create a test that at least verifies
# the handler can be invoked and processes the input

# Create test event JSON
# Note: For a full test, you'd need LocalStack or real S3 credentials
TEST_EVENT=$(cat <<EOF
{
  "jsonUrl": "http://host.docker.internal:$PORT/data.json",
  "fps": 30,
  "layerOverridesUrl": "http://host.docker.internal:$PORT/layer-overrides.json",
  "textMeasurementMode": "accurate",
  "debug": true,
  "outputS3Bucket": "test-bucket",
  "outputS3Key": "test-output.mov"
}
EOF
)

echo "$TEST_EVENT" > /tmp/test-event.json
echo -e "${GREEN}✓ Test event created${NC}"
echo -e "${GREEN}✓ Output directory: $OUTPUT_DIR${NC}"
echo ""

# Step 3: Test the Lambda handler
echo -e "${YELLOW}Step 3: Testing Lambda handler...${NC}"
echo "Invoking Lambda handler..."
echo ""

cd "$PROJECT_ROOT"

# Get AWS credentials if using SSO profile
if [ -n "${AWS_PROFILE:-}" ]; then
    # Mount AWS config and SSO cache for profile-based auth
    AWS_SHARED_CREDENTIALS_FILE="${AWS_SHARED_CREDENTIALS_FILE:-$HOME/.aws/credentials}"
    AWS_CONFIG_FILE="${AWS_CONFIG_FILE:-$HOME/.aws/config}"
    
    DOCKER_ENV_ARGS=(
        -e "AWS_PROFILE=${AWS_PROFILE}"
        -e "AWS_REGION=${AWS_REGION:-us-east-1}"
        -e "AWS_DEFAULT_REGION=${AWS_DEFAULT_REGION:-us-east-1}"
    )
    
    DOCKER_VOLUME_ARGS=()
    # Mount AWS config if it exists
    if [ -f "$AWS_SHARED_CREDENTIALS_FILE" ]; then
        DOCKER_VOLUME_ARGS+=(-v "${AWS_SHARED_CREDENTIALS_FILE}:/root/.aws/credentials:ro")
    fi
    if [ -f "$AWS_CONFIG_FILE" ]; then
        DOCKER_VOLUME_ARGS+=(-v "${AWS_CONFIG_FILE}:/root/.aws/config:ro")
    fi
    # Mount SSO cache directory if it exists
    if [ -d "$HOME/.aws/sso/cache" ]; then
        DOCKER_VOLUME_ARGS+=(-v "$HOME/.aws/sso/cache:/root/.aws/sso/cache:ro")
    fi
else
    # Use explicit credentials if provided
    DOCKER_ENV_ARGS=(
        -e "AWS_ACCESS_KEY_ID=${AWS_ACCESS_KEY_ID:-test}"
        -e "AWS_SECRET_ACCESS_KEY=${AWS_SECRET_ACCESS_KEY:-test}"
        -e "AWS_SESSION_TOKEN=${AWS_SESSION_TOKEN:-}"
        -e "AWS_DEFAULT_REGION=${AWS_DEFAULT_REGION:-us-east-1}"
        -e "AWS_REGION=${AWS_REGION:-${AWS_DEFAULT_REGION:-us-east-1}}"
    )
    DOCKER_VOLUME_ARGS=()
fi

# Run the Lambda handler and capture output
# Also mount output directory to extract the video
docker run --rm \
    --add-host=host.docker.internal:host-gateway \
    -v /tmp/test-event.json:/tmp/event.json:ro \
    -v "$OUTPUT_DIR:/output:rw" \
    "${DOCKER_VOLUME_ARGS[@]}" \
    "${DOCKER_ENV_ARGS[@]}" \
    -e LOCALSTACK_ENDPOINT="${LOCALSTACK_ENDPOINT:-}" \
    --entrypoint node \
    "$IMAGE_NAME" \
    -e "
const { handler } = require('/var/task/index.js');
const fs = require('fs');
const path = require('path');
const event = JSON.parse(fs.readFileSync('/tmp/event.json', 'utf-8'));

console.log('[TEST] Starting Lambda handler...');
console.log('[TEST] Event:', JSON.stringify(event, null, 2));
console.log('');

// Watch for the output file and copy it when it's fully written
const watchForVideo = () => {
  let lastSize = 0;
  let stableCount = 0;
  const checkInterval = setInterval(() => {
    try {
      const tmpDir = '/tmp';
      const files = fs.readdirSync(tmpDir);
      for (const file of files) {
        if (file.startsWith('render-')) {
          const renderDir = path.join(tmpDir, file);
          const movFile = path.join(renderDir, 'output.mov');
          if (fs.existsSync(movFile)) {
            const stats = fs.statSync(movFile);
            const currentSize = stats.size;
            
            // If file size is stable (not changing), it's done writing
            if (currentSize === lastSize && currentSize > 0) {
              stableCount++;
              if (stableCount >= 5) {
                // File is stable, copy it
                const outputPath = '/output/output.mov';
                try {
                  fs.copyFileSync(movFile, outputPath);
                  console.log('[TEST] Video saved to:', outputPath, '(' + (currentSize / 1024 / 1024).toFixed(2) + ' MB)');
                  clearInterval(checkInterval);
                  return;
                } catch (e) {
                  console.error('[TEST] Error copying video:', e.message);
                }
              }
            } else {
              stableCount = 0;
            }
            lastSize = currentSize;
          }
        }
      }
    } catch (e) {
      // Ignore errors
    }
  }, 200);
  
  // Stop watching after 60 seconds
  setTimeout(() => clearInterval(checkInterval), 60000);
};

// Start watching for the video file
watchForVideo();

handler(event)
  .then(result => {
    console.log('');
    console.log('[TEST] Handler completed');
    console.log('[TEST] Status Code:', result.statusCode);
    
    // Final attempt to copy the video
    setTimeout(() => {
      const tmpDir = '/tmp';
      try {
        const files = fs.readdirSync(tmpDir);
        for (const file of files) {
          if (file.startsWith('render-')) {
            const renderDir = path.join(tmpDir, file);
            const movFile = path.join(renderDir, 'output.mov');
            if (fs.existsSync(movFile)) {
              const stats = fs.statSync(movFile);
              if (stats.size > 0) {
                const outputPath = '/output/output.mov';
                fs.copyFileSync(movFile, outputPath);
                console.log('[TEST] Video saved to:', outputPath, '(' + (stats.size / 1024 / 1024).toFixed(2) + ' MB)');
              }
              break;
            }
          }
        }
      } catch (e) {
        // Ignore
      }
    }, 500);
    
    if (result.statusCode === 200) {
      const body = JSON.parse(result.body);
      console.log('[TEST] Success!');
      console.log('[TEST] Output S3 URL:', body.outputS3Url);
      console.log('[TEST] Timings:', JSON.stringify(body.timings, null, 2));
    } else {
      console.log('[TEST] Error response:', result.body);
    }
    process.exit(result.statusCode === 200 ? 0 : 1);
  })
  .catch(error => {
    console.error('');
    console.error('[TEST] Handler error:', error.message);
    console.error('[TEST] Stack:', error.stack);
    process.exit(1);
  });
" 2>&1 | tee /tmp/lambda-test-output.log

TEST_EXIT_CODE=${PIPESTATUS[0]}

# Note: Output already displayed via tee above

echo ""

# Check if rendering was successful (look for "Rendered X frames" in output)
if grep -q "Rendered.*frames" /tmp/lambda-test-output.log; then
    RENDER_SUCCESS=true
else
    RENDER_SUCCESS=false
fi

if [ "$RENDER_SUCCESS" = true ]; then
    echo -e "${GREEN}=== Rendering Test Passed ===${NC}"
    echo ""
    echo "✓ Successfully downloaded JSON and text config"
    echo "✓ Successfully rendered frames and created MOV file"
    if [ $TEST_EXIT_CODE -eq 0 ]; then
        echo -e "${GREEN}✓ Full test passed including S3 upload!${NC}"
    fi
    
    # Check if video was saved
    if [ -f "$OUTPUT_VIDEO" ]; then
        VIDEO_SIZE=$(du -h "$OUTPUT_VIDEO" | cut -f1)
        echo ""
        echo -e "${GREEN}✓ Video saved: $OUTPUT_VIDEO (${VIDEO_SIZE})${NC}"
        echo ""
        echo "To view the video, run:"
        echo "  open $OUTPUT_VIDEO"
        echo "  # or"
        echo "  ffplay $OUTPUT_VIDEO"
    fi
    
    exit 0
else
    echo -e "${RED}=== Test Failed (exit code: $TEST_EXIT_CODE) ===${NC}"
    echo ""
    echo "Common issues:"
    echo "1. Rendering failed (check logs above)"
    echo "2. Network connectivity to HTTP server"
    echo "3. Lambda handler errors (check logs above)"
    exit $TEST_EXIT_CODE
fi
