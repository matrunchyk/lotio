#!/bin/bash
set -euo pipefail

# Usage: ./deploy.sh <region> <lambda-function-name> <s3-bucket-name> [tag]
# Example: ./deploy.sh us-east-1 skottie-render my-output-bucket latest

if [ $# -lt 3 ]; then
  echo "Usage: $0 <region> <lambda-function-name> <s3-bucket-name> [tag]"
  echo "Example: $0 us-east-1 skottie-render my-output-bucket latest"
  exit 1
fi

REGION=$1
FUNCTION_NAME=$2
S3_BUCKET=$3
TAG=${4:-latest}

# Get account ID from the main SSO profile (Lambda function will be in this account)
LAMBDA_ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text --profile 917630709045_AdministratorAccess 2>/dev/null || aws sts get-caller-identity --query Account --output text)
ECR_ACCOUNT_ID=$LAMBDA_ACCOUNT_ID
REPO_NAME="skottie-render"
ECR_URI="${ECR_ACCOUNT_ID}.dkr.ecr.${REGION}.amazonaws.com/${REPO_NAME}"
IMAGE_URI="${ECR_URI}:${TAG}"

echo "=== AWS SSO Login (Main Account) ==="
aws sso login --region $REGION --profile 917630709045_AdministratorAccess

echo "=== Creating ECR repository ==="
# Try to describe first, if it fails, try to create (may fail if no permissions, that's OK if repo exists)
aws ecr describe-repositories --repository-names $REPO_NAME --region $REGION --profile 917630709045_AdministratorAccess 2>/dev/null && \
  echo "Repository already exists" || \
  (aws ecr create-repository \
    --repository-name $REPO_NAME \
    --region $REGION \
    --image-tag-mutability MUTABLE \
    --profile 917630709045_AdministratorAccess 2>/dev/null && \
    echo "Repository created" || \
    echo "Note: Could not create repository (may already exist or need permissions)")

echo "=== Logging in to ECR ==="
aws ecr get-login-password --region $REGION --profile 917630709045_AdministratorAccess 2>/dev/null | \
  docker login --username AWS --password-stdin $ECR_URI || \
  aws ecr get-login-password --region $REGION | \
  docker login --username AWS --password-stdin $ECR_URI

echo "=== Building and pushing Docker image (ARM64) ==="
echo "ECR URI: ${IMAGE_URI}"
# Use buildx with --push to ensure Docker v2 manifest format for Lambda
docker buildx build --platform linux/arm64 \
  --progress=plain \
  -t ${IMAGE_URI} \
  -f lambda/Dockerfile \
  --push \
  --provenance=false \
  --sbom=false \
  .

echo "=== Image pushed to ECR ==="

echo "=== Creating/updating Lambda function ==="
# Check if function exists (using main SSO profile for Lambda operations)
if aws lambda get-function --function-name $FUNCTION_NAME --region $REGION --profile 917630709045_AdministratorAccess &>/dev/null; then
  echo "Updating existing Lambda function..."
  echo "Updating function code..."
  aws lambda update-function-code \
    --function-name $FUNCTION_NAME \
    --image-uri $IMAGE_URI \
    --region $REGION \
    --profile 917630709045_AdministratorAccess
  
  echo "Waiting for function code update to complete..."
  aws lambda wait function-updated \
    --function-name $FUNCTION_NAME \
    --region $REGION \
    --profile 917630709045_AdministratorAccess
  
  echo "Updating function configuration..."
  aws lambda update-function-configuration \
    --function-name $FUNCTION_NAME \
    --timeout 300 \
    --memory-size 10240 \
    --ephemeral-storage Size=1024 \
    --region $REGION \
    --profile 917630709045_AdministratorAccess
  
  echo "Waiting for function configuration update to complete..."
  aws lambda wait function-updated \
    --function-name $FUNCTION_NAME \
    --region $REGION \
    --profile 917630709045_AdministratorAccess
else
  echo "Creating new Lambda function..."
  # Create execution role (simplified - assumes role exists or create separately)
  # Lambda function is in the main SSO account (917630709045)
  ROLE_ARN="arn:aws:iam::${LAMBDA_ACCOUNT_ID}:role/lambda-execution-role"
  
  echo "Creating function with role: $ROLE_ARN"
  aws lambda create-function \
    --function-name $FUNCTION_NAME \
    --package-type Image \
    --code ImageUri=$IMAGE_URI \
    --role $ROLE_ARN \
    --timeout 300 \
    --memory-size 10240 \
    --ephemeral-storage Size=1024 \
    --architectures arm64 \
    --region $REGION \
    --profile 917630709045_AdministratorAccess
  
  echo "Waiting for function creation to complete..."
  aws lambda wait function-active \
    --function-name $FUNCTION_NAME \
    --region $REGION \
    --profile 917630709045_AdministratorAccess
fi

echo "=== Granting S3 permissions ==="
# Add S3 permissions to Lambda execution role
# Note: This assumes the role exists. You may need to create it separately.
# Using main SSO profile for IAM operations in Lambda account
aws iam attach-role-policy \
  --role-name lambda-execution-role \
  --policy-arn arn:aws:iam::aws:policy/AmazonS3FullAccess \
  --profile 917630709045_AdministratorAccess \
  2>/dev/null || echo "S3 permissions may already be attached"

echo "=== Deployment complete ==="
echo "Lambda function: $FUNCTION_NAME"
echo "Image URI: $IMAGE_URI"
echo ""
echo "Test with:"
echo "  ./lambda/test-invoke.sh $FUNCTION_NAME $REGION $S3_BUCKET renders/test.mov"

