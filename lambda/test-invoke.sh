#!/bin/bash
set -euo pipefail

# Usage: ./test-invoke.sh <function-name> <region> <s3-bucket> <s3-key> [text-config-url]
# Example: ./test-invoke.sh lotio-lambda us-east-1 my-bucket renders/test.mov
# Example with text config: ./test-invoke.sh lotio-lambda us-east-1 my-bucket renders/test.mov https://example.com/text-config.json

if [ $# -lt 4 ]; then
  echo "Usage: $0 <function-name> <region> <s3-bucket> <s3-key> [text-config-url]"
  echo "Example: $0 lotio-lambda us-east-1 my-bucket renders/test.mov"
  echo "Example with text config: $0 lotio-lambda us-east-1 my-bucket renders/test.mov https://example.com/text-config.json"
  exit 1
fi

FUNCTION_NAME=$1
REGION=$2
S3_BUCKET=$3
S3_KEY=$4
TEXT_CONFIG_URL=${5:-}

TEST_JSON_URL="https://cache.video.igentify.in/templates/7219da37-43a0-4895-8b04-aa7ab6cc25ac/New_Format_Opening_1.1/data.json"

echo "=== Invoking Lambda function ==="
echo "Function: $FUNCTION_NAME"
echo "JSON URL: $TEST_JSON_URL"
if [ -n "$TEXT_CONFIG_URL" ]; then
  echo "Text Config URL: $TEXT_CONFIG_URL"
fi
echo "Output: s3://$S3_BUCKET/$S3_KEY"
echo ""

if [ -n "$TEXT_CONFIG_URL" ]; then
  PAYLOAD=$(cat <<EOF
{
  "jsonUrl": "$TEST_JSON_URL",
  "fps": 30,
  "textConfigUrl": "$TEXT_CONFIG_URL",
  "outputS3Bucket": "$S3_BUCKET",
  "outputS3Key": "$S3_KEY"
}
EOF
)
else
  PAYLOAD=$(cat <<EOF
{
  "jsonUrl": "$TEST_JSON_URL",
  "fps": 30,
  "outputS3Bucket": "$S3_BUCKET",
  "outputS3Key": "$S3_KEY"
}
EOF
)
fi

echo "Payload: $PAYLOAD"
echo ""

RESPONSE=$(aws lambda invoke \
  --function-name $FUNCTION_NAME \
  --region $REGION \
  --payload "$PAYLOAD" \
  --cli-binary-format raw-in-base64-out \
  /tmp/lambda-response.json)

echo "=== Lambda Response ==="
cat /tmp/lambda-response.json | jq '.'

# Extract S3 URL from response (body is a JSON string, need to parse it)
STATUS_CODE=$(cat /tmp/lambda-response.json | jq -r '.statusCode')
if [ "$STATUS_CODE" = "200" ]; then
  S3_URL=$(cat /tmp/lambda-response.json | jq -r '.body | fromjson | .outputS3Url // empty')
else
  S3_URL=""
  echo "Lambda returned error status: $STATUS_CODE"
  cat /tmp/lambda-response.json | jq -r '.body | fromjson | .error // .message // "Unknown error"'
fi

if [ -n "$S3_URL" ]; then
  echo ""
  echo "=== Success ==="
  echo "Output MOV file: $S3_URL"
  echo ""
  echo "Download with:"
  echo "  aws s3 cp $S3_URL ./output.mov"
else
  echo ""
  echo "=== Error ==="
  echo "No S3 URL in response. Check Lambda logs:"
  echo "  aws logs tail /aws/lambda/$FUNCTION_NAME --follow --region $REGION"
  exit 1
fi

