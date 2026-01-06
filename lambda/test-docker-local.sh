#!/bin/bash
set -euo pipefail

# Usage: ./test-docker-local.sh <image-name> <json-url> [text-config-url] [output-dir]
# Example: ./test-docker-local.sh lotio-lambda:local https://example.com/test.json
# Example with text config: ./test-docker-local.sh lotio-lambda:local https://example.com/test.json https://example.com/text-config.json

if [ $# -lt 2 ]; then
  echo "Usage: $0 <image-name> <json-url> [text-config-url] [output-dir]"
  echo ""
  echo "Examples:"
  echo "  $0 lotio-lambda:local https://example.com/test.json"
  echo "  $0 lotio-lambda:local https://example.com/test.json https://example.com/text-config.json"
  echo ""
  echo "Note: The Lambda function expects HTTP or S3 URLs. For local files, you can:"
  echo "  1. Use a local HTTP server: python3 -m http.server 8000"
  echo "  2. Upload to S3 and use S3 URLs"
  echo "  3. Use the existing test-local.sh script which handles S3 uploads"
  exit 1
fi

IMAGE_NAME=$1
JSON_URL=$2
TEXT_CONFIG_URL=${3:-}
OUTPUT_DIR=${4:-./lambda-output}

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Create test event payload
if [ -n "$TEXT_CONFIG_URL" ]; then
  PAYLOAD=$(cat <<EOF
{
  "jsonUrl": "$JSON_URL",
  "fps": 30,
  "textConfigUrl": "$TEXT_CONFIG_URL",
  "outputS3Bucket": "local-test",
  "outputS3Key": "output.mov"
}
EOF
)
else
  PAYLOAD=$(cat <<EOF
{
  "jsonUrl": "$JSON_URL",
  "fps": 30,
  "outputS3Bucket": "local-test",
  "outputS3Key": "output.mov"
}
EOF
)
fi

echo "=== Testing Lambda Container Locally ==="
echo "Image: $IMAGE_NAME"
echo "JSON URL: $JSON_URL"
if [ -n "$TEXT_CONFIG_URL" ]; then
  echo "Text config URL: $TEXT_CONFIG_URL"
fi
echo "Output directory: $OUTPUT_DIR"
echo ""
echo "Payload:"
echo "$PAYLOAD" | jq '.'
echo ""

# The Lambda base image includes the Runtime Interface Emulator (RIE)
# It listens on port 9001 by default
echo "=== Starting container with Lambda RIE ==="
CONTAINER_ID=$(docker run -d \
  -p 9001:9001 \
  -v "$OUTPUT_DIR:/tmp/output" \
  "$IMAGE_NAME")

# Wait for container to start
echo "Waiting for container to be ready..."
sleep 3

# Check if container is running
if ! docker ps | grep -q "$CONTAINER_ID"; then
  echo "ERROR: Container failed to start"
  echo "Container logs:"
  docker logs "$CONTAINER_ID" 2>&1 | tail -20
  docker rm "$CONTAINER_ID" >/dev/null 2>&1 || true
  exit 1
fi

# Invoke the function using curl
echo "=== Invoking function ==="
RESPONSE=$(curl -s -X POST \
  http://localhost:9001/2015-03-31/functions/function/invocations \
  -H "Content-Type: application/json" \
  -d "$PAYLOAD" || echo '{"statusCode": 500, "body": "{\"error\": \"Invocation failed\"}"}')

echo "=== Response ==="
echo "$RESPONSE" | jq '.' 2>/dev/null || echo "$RESPONSE"

# Extract status code
STATUS_CODE=$(echo "$RESPONSE" | jq -r '.statusCode // 500' 2>/dev/null || echo "500")

if [ "$STATUS_CODE" = "200" ]; then
  echo ""
  echo "=== Success ==="
  OUTPUT_URL=$(echo "$RESPONSE" | jq -r '.body | fromjson | .outputS3Url // empty' 2>/dev/null || echo "")
  if [ -n "$OUTPUT_URL" ]; then
    echo "Output S3 URL: $OUTPUT_URL"
    echo ""
    echo "Note: In local testing, the file won't actually be uploaded to S3."
    echo "Check container logs to see if rendering completed successfully."
  fi
else
  echo ""
  echo "=== Error ==="
  echo "Status code: $STATUS_CODE"
  ERROR_MSG=$(echo "$RESPONSE" | jq -r '.body | fromjson | .error // .message // "Unknown error"' 2>/dev/null || echo "$RESPONSE")
  echo "Error: $ERROR_MSG"
fi

# Show container logs
echo ""
echo "=== Container Logs (last 50 lines) ==="
docker logs "$CONTAINER_ID" 2>&1 | tail -50

# Cleanup
echo ""
echo "=== Cleaning up ==="
docker stop "$CONTAINER_ID" >/dev/null 2>&1 || true
docker rm "$CONTAINER_ID" >/dev/null 2>&1 || true

echo "Done!"
echo ""
echo "Tip: For more detailed testing with S3, use:"
echo "  ./lambda/test-local.sh lotio-lambda us-east-1 my-bucket <local-json-file>"
