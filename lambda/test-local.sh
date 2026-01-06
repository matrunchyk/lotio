#!/bin/bash
set -euo pipefail

# Usage: ./test-local.sh <function-name> <region> <s3-bucket> <local-json-file> [text-config-file]
# Example: ./test-local.sh lotio-lambda us-east-1 my-bucket ../shots/Your_Results_Positive_2.1/data.json
# Example with text config: ./test-local.sh lotio-lambda us-east-1 my-bucket ../shots/Your_Results_Positive_2.1/data.json ../shots/Your_Results_Positive_2.1/text-config.json

if [ $# -lt 4 ]; then
  echo "Usage: $0 <function-name> <region> <s3-bucket> <local-json-file> [text-config-file]"
  echo "Example: $0 lotio-lambda us-east-1 my-bucket ../shots/Your_Results_Positive_2.1/data.json"
  echo "Example with text config: $0 lotio-lambda us-east-1 my-bucket ../shots/Your_Results_Positive_2.1/data.json ../shots/Your_Results_Positive_2.1/text-config.json"
  exit 1
fi

FUNCTION_NAME=$1
REGION=$2
S3_BUCKET=$3
LOCAL_JSON=$4
TEXT_CONFIG_FILE=${5:-}

# Generate a unique S3 key for the input JSON
TIMESTAMP=$(date +%s)
JSON_KEY="inputs/test-${TIMESTAMP}.json"
OUTPUT_KEY="renders/test-${TIMESTAMP}.mov"

echo "=== Uploading JSON to S3 ==="
aws s3 cp "$LOCAL_JSON" "s3://${S3_BUCKET}/${JSON_KEY}" --region $REGION --profile 917630709045_AdministratorAccess 2>&1 || {
  echo "Failed to upload to S3. Trying to create bucket or check permissions..."
  aws s3 mb "s3://${S3_BUCKET}" --region $REGION --profile 917630709045_AdministratorAccess 2>/dev/null || true
  aws s3 cp "$LOCAL_JSON" "s3://${S3_BUCKET}/${JSON_KEY}" --region $REGION --profile 917630709045_AdministratorAccess
}
JSON_S3_URL="s3://${S3_BUCKET}/${JSON_KEY}"
JSON_HTTP_URL="https://${S3_BUCKET}.s3.${REGION}.amazonaws.com/${JSON_KEY}"

echo "JSON uploaded to: $JSON_S3_URL"
echo "JSON HTTP URL: $JSON_HTTP_URL"

# Upload text config file if provided
TEXT_CONFIG_S3_URL=""
if [ -n "$TEXT_CONFIG_FILE" ]; then
  echo ""
  echo "=== Uploading text configuration to S3 ==="
  TEXT_CONFIG_KEY="inputs/test-${TIMESTAMP}-text-config.json"
  aws s3 cp "$TEXT_CONFIG_FILE" "s3://${S3_BUCKET}/${TEXT_CONFIG_KEY}" --region $REGION --profile 917630709045_AdministratorAccess
  TEXT_CONFIG_S3_URL="s3://${S3_BUCKET}/${TEXT_CONFIG_KEY}"
  echo "Text config uploaded to: $TEXT_CONFIG_S3_URL"
fi
echo ""

echo "=== Invoking Lambda function ==="
echo "Function: $FUNCTION_NAME"
echo "JSON S3 URI: $JSON_S3_URL"
if [ -n "$TEXT_CONFIG_S3_URL" ]; then
  echo "Text Config S3 URI: $TEXT_CONFIG_S3_URL"
fi
echo "Output: s3://$S3_BUCKET/$OUTPUT_KEY"
echo ""

# Use S3 URI instead of HTTP URL to avoid public access issues
if [ -n "$TEXT_CONFIG_S3_URL" ]; then
  PAYLOAD=$(cat <<EOF
{
  "jsonUrl": "$JSON_S3_URL",
  "fps": 30,
  "textConfigUrl": "$TEXT_CONFIG_S3_URL",
  "outputS3Bucket": "$S3_BUCKET",
  "outputS3Key": "$OUTPUT_KEY"
}
EOF
)
else
  PAYLOAD=$(cat <<EOF
{
  "jsonUrl": "$JSON_S3_URL",
  "fps": 30,
  "outputS3Bucket": "$S3_BUCKET",
  "outputS3Key": "$OUTPUT_KEY"
}
EOF
)
fi

echo "Payload: $PAYLOAD"
echo ""

RESPONSE=$(aws lambda invoke \
  --function-name $FUNCTION_NAME \
  --region $REGION \
  --profile 917630709045_AdministratorAccess \
  --payload "$PAYLOAD" \
  --cli-binary-format raw-in-base64-out \
  /tmp/lambda-response.json 2>&1)

echo "=== Lambda Response ==="
cat /tmp/lambda-response.json | jq '.' || cat /tmp/lambda-response.json

# Extract S3 URL from response
STATUS_CODE=$(cat /tmp/lambda-response.json | jq -r '.statusCode // "unknown"' 2>/dev/null || echo "unknown")
if [ "$STATUS_CODE" = "200" ]; then
  S3_URL=$(cat /tmp/lambda-response.json | jq -r '.body | fromjson | .outputS3Url // empty' 2>/dev/null || echo "")
  if [ -n "$S3_URL" ]; then
    echo ""
    echo "=== Success ==="
    echo "Output MOV file: $S3_URL"
    echo ""
    echo "Download with:"
    echo "  aws s3 cp $S3_URL ./output.mov --profile 917630709045_AdministratorAccess"
  else
    echo "Response body:"
    cat /tmp/lambda-response.json | jq -r '.body' 2>/dev/null || cat /tmp/lambda-response.json
  fi
else
  echo ""
  echo "=== Error ==="
  echo "Lambda returned status: $STATUS_CODE"
  cat /tmp/lambda-response.json | jq -r '.body | fromjson | .error // .message // "Unknown error"' 2>/dev/null || cat /tmp/lambda-response.json
  echo ""
  echo "Check Lambda logs:"
  echo "  aws logs tail /aws/lambda/$FUNCTION_NAME --follow --region $REGION --profile 917630709045_AdministratorAccess"
  exit 1
fi

