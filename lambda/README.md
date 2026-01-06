# Lambda MOV Renderer

Lambda function for rendering Lottie animations to MOV (ProRes 4444) format.

## Overview

This Lambda function:
1. Accepts a JSON URL (HTTP or S3) pointing to a Lottie animation JSON
2. Downloads the JSON and all referenced assets/fonts (from HTTP or S3)
3. Renders the animation to ProRes 4444 MOV using a C++ binary (lotio) and ffmpeg
4. Uploads the MOV to S3 and returns the S3 URL

## Prerequisites

- Docker (for building the container image)
- AWS CLI (for deployment)
- Node.js 20+ (for local development)

## Quick Start

### 1. Deploy to AWS

```bash
./lambda/deploy.sh <account-id> <region> <function-name> <s3-bucket> [tag]
```

### 2. Test Lambda

```bash
./lambda/test-invoke.sh <function-name> <region> <s3-bucket> <s3-key>
```

## Event Format

```json
{
  "jsonUrl": "https://example.com/animation.json",
  "fps": 30,
  "layerOverridesUrl": "https://example.com/layer-overrides.json",
  "outputS3Bucket": "my-bucket",
  "outputS3Key": "renders/video.mov"
}
```

### Parameters

- **jsonUrl** (required): HTTP or S3 URL to the Lottie animation JSON file
- **fps** (optional): Frame rate for the output video (default: 30)
- **layerOverridesUrl** (optional): HTTP or S3 URL to layer overrides JSON file for text and image customization. See the [CLI documentation](../docs/cli.html) for format details.
- **outputS3Bucket** (required): S3 bucket for the output MOV file
- **outputS3Key** (required): S3 key for the output MOV file

## Response Format

```json
{
  "statusCode": 200,
  "body": {
    "message": "Render completed successfully",
    "outputS3Url": "s3://bucket/key.mov",
    "timings": {
      "totalInvocationMs": 12345,
      "jsonDownloadMs": 500,
      "renderingMs": 8000,
      "encodingMs": 1500
    }
  }
}
```

## Architecture

- **ARM64** architecture for cost savings
- **Node.js 22** runtime
- **Multi-stage Docker build**:
  - Stage 1: Build Skia
  - Stage 2: Compile lotio binary (includes text configuration support)
  - Stage 3: Build TypeScript
  - Stage 4: Lambda runtime with ffmpeg

## Rendering Approach

The Lambda function uses a streaming pipeline to render frames and encode video in one step:
- Frames are rendered using `lotio --stream` which outputs PNG data to stdout
- PNG data is piped directly to ffmpeg via `image2pipe` format (no temporary files written to disk)
- ffmpeg encodes the streamed frames to ProRes 4444 MOV
- This approach minimizes disk I/O, reduces disk space usage, and improves performance in the Lambda environment

## Layer Overrides

The Lambda function supports layer overrides files for text and image customization. When `layerOverridesUrl` is provided in the event:

1. The layer overrides file is downloaded from the provided URL (HTTP or S3)
2. It's passed to `lotio` with the `--layer-overrides` parameter
3. Text layers are dynamically updated and auto-fitted, and image paths are overridden according to the configuration

See the [CLI documentation](../docs/cli.html) for complete documentation on the layer overrides format.

## Files

- `index.ts` - Lambda handler
- `Dockerfile` - Main multi-stage build
- `deploy.sh` - Deployment script
- `test-invoke.sh` - Test invocation script
- `test-local.sh` - Test script with local file upload

