# Docker Usage

Lotio provides Docker images for easy deployment and consistent rendering across different environments. There are two main images:

1. **`matrunchyk/lotio:latest`** - Contains lotio binary with Skia libraries (for programmatic use or manual ffmpeg integration)
2. **`matrunchyk/lotio-ffmpeg:latest`** - Contains lotio + FFmpeg with automatic video encoding (recommended for video output)

## Quick Start

### With Automatic Video Encoding (Recommended)

```bash
docker pull matrunchyk/lotio-ffmpeg:latest

docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio-ffmpeg:latest \
  data.json - 30 --layer-overrides layer-overrides.json --output video.mov
```

### With lotio Binary Only

```bash
docker pull matrunchyk/lotio:latest

docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio:latest \
  data.json - 30
```

## Pulling from Docker Hub

Pre-built Docker images are available on Docker Hub:

```bash
# Pull lotio-ffmpeg (recommended for video encoding)
docker pull matrunchyk/lotio-ffmpeg:latest
docker pull matrunchyk/lotio-ffmpeg:v1.2.3

# Pull lotio only (for programmatic use)
docker pull matrunchyk/lotio:latest
docker pull matrunchyk/lotio:v1.2.3
```

The images are automatically built and pushed for each release. Available tags:
- `latest` - Always points to the most recent release (multi-platform manifest)
- `v1.2.3` - Specific version tag (multi-platform manifest)
- `v1.2.3-arm64` - Architecture-specific tag for ARM64
- `v1.2.3-amd64` - Architecture-specific tag for x86_64

**Multi-platform support:** Both images support `linux/arm64` and `linux/amd64`. Docker automatically selects the correct platform.

## Building the Images

### Build Chain

The Docker images use a multi-stage build chain for optimized builds:

```bash
Dockerfile.skia → matrunchyk/skia:latest
    ↓
Dockerfile.lotio → matrunchyk/lotio:latest
    ↓
Dockerfile.lotio-ffmpeg → matrunchyk/lotio-ffmpeg:latest
```

### Building from Source

**Build lotio image:**
```bash
docker buildx build \
  --platform linux/arm64,linux/amd64 \
  -t matrunchyk/lotio:test \
  -f Dockerfile.lotio \
  --build-arg SKIA_IMAGE=matrunchyk/skia:latest \
  --push .
```

**Build lotio-ffmpeg image:**
```bash
docker buildx build \
  --platform linux/arm64,linux/amd64 \
  -t matrunchyk/lotio-ffmpeg:test \
  -f Dockerfile.lotio-ffmpeg \
  --build-arg LOTIO_IMAGE=matrunchyk/lotio:test \
  --push .
```

**Note:** The lotio image uses `matrunchyk/skia:latest` as base (built separately using `build_skia_docker_multi.sh`), and lotio-ffmpeg uses `matrunchyk/lotio:latest` as base.

### Image Details

**matrunchyk/lotio:latest:**
- **Base Image**: `matrunchyk/skia:latest` (contains pre-built Skia)
- **Architecture**: Multi-platform (`linux/arm64`, `linux/amd64`)
- **Includes**:
  - lotio binary (`/usr/local/bin/lotio`)
  - lotio static library (`/usr/local/lib/liblotio.a`)
  - lotio headers (`/usr/local/include/lotio/`)
  - Skia libraries and headers (from base image)
  - Runtime dependencies

**matrunchyk/lotio-ffmpeg:latest:**
- **Base Image**: `matrunchyk/lotio:latest`
- **Architecture**: Multi-platform (`linux/arm64`, `linux/amd64`)
- **Includes**:
  - Everything from `matrunchyk/lotio:latest`
  - FFmpeg 8.0 with minimal build (optimized for lotio):
    - PNG decoder (for `image2pipe` input)
    - `image2`/`image2pipe` demuxer (supports both pipe and file input)
    - ProRes encoder (`prores_ks`) - ProRes 4444 with alpha channel
    - MOV muxer
    - Format filter
  - Entrypoint script for automatic video encoding

## Basic Usage

### Render Animation to Video (Recommended)

Using `lotio-ffmpeg` image with automatic video encoding:

```bash
docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio-ffmpeg:latest \
  data.json - 30 --layer-overrides layer-overrides.json --output video.mov
```

The entrypoint script automatically:
- Adds `--stream` if not present (required for piping to ffmpeg)
- Pipes lotio PNG output to ffmpeg
- Encodes to ProRes 4444 MOV with alpha channel support

### Direct Command Execution

The `lotio-ffmpeg` image supports direct command execution:

```bash
# Execute ffmpeg directly
docker run --rm matrunchyk/lotio-ffmpeg:latest ffmpeg -version

# Execute lotio directly
docker run --rm matrunchyk/lotio-ffmpeg:latest lotio --version

# Render to video (automatic encoding)
docker run --rm -v $(pwd):/workspace matrunchyk/lotio-ffmpeg:latest \
  data.json - 30 --output video.mov
```

### With Input Directory

```bash
docker run --rm \
  -v "$(pwd)/samples:/workspace/input:ro" \
  -v "$(pwd):/workspace/output" \
  matrunchyk/lotio:latest \
  /workspace/input/data.json - 30 --output /workspace/output/video.mov
```

## Command-Line Arguments

### lotio-ffmpeg Image

The `lotio-ffmpeg` image has a smart entrypoint that:
- **If first argument is a command** (like `ffmpeg` or `lotio`): Executes it directly
- **Otherwise**: Treats arguments as lotio commands and automatically:
  - Adds `--stream` if not present (required for piping to ffmpeg)
  - Pipes lotio PNG output to ffmpeg
  - Encodes to ProRes 4444 MOV with alpha channel

### lotio Image

The `lotio` image runs lotio directly - no automatic video encoding.

### Lotio Arguments

All standard lotio arguments are supported:

- `--stream` - Stream frames to stdout as PNG (automatically added if missing for video encoding)
- `--debug` - Enable debug output
- `--layer-overrides <file>` - Path to layer overrides JSON
  - **Absolute paths**: Used as-is (e.g., `/workspace/input/layer-overrides.json`)
  - **Relative paths**: Resolved relative to the **current working directory (cwd)** inside the container
    - Example: If the container's cwd is `/workspace` and you use `--layer-overrides config/overrides.json`, it resolves to `/workspace/config/overrides.json`
  - The parent directory of this file is used as the base directory for resolving relative image paths in `imageLayers.filePath`
- `--text-padding <0.0-1.0>` - Text padding factor (default: 0.97 = 3% padding)
- `--text-measurement-mode <fast|accurate|pixel-perfect>` - Text measurement mode (default: accurate)
- `--version` - Print version information and exit
- `--help, -h` - Show help message
- `<input.json>` - Input Lottie animation file (required)
  - **Absolute paths**: Used as-is (e.g., `/workspace/input/data.json`)
  - **Relative paths**: Resolved relative to the **current working directory (cwd)** inside the container
    - Example: If the container's cwd is `/workspace` and you use `data.json`, it resolves to `/workspace/data.json`
  - The parent directory of this file is used as the base directory for resolving relative image paths in the Lottie JSON
- `<output_dir>` - Output directory (required in non-stream mode; optional in stream mode, defaults to `-` when using `--stream`)
- `[fps]` - Frames per second (default: animation fps or 30)

**Note on `output_dir` in stream mode:** When using `--stream` (automatically added by the entrypoint script), the `output_dir` argument is optional. If omitted, it defaults to `-` (stdout). You can explicitly specify `-` for clarity, or omit it entirely:
- `data.json - 30 --output video.mov` (explicit)
- `data.json 30 --output video.mov` (implicit, defaults to `-`)

**Note:** Frames are output as PNG by default. No format selection is needed.

### Docker-Specific Arguments (lotio-ffmpeg image only)

- `--output, -o <file>` - Output video file path (default: `output.mov`)
- `--text-padding, -p <value>` - Text padding factor (0.0-1.0, default: 0.97)
- `--text-measurement-mode, -m <mode>` - Text measurement mode: `fast` | `accurate` | `pixel-perfect` (default: `accurate`)

#### Text Padding

The `--text-padding` option controls how much of the target text box width is used for text sizing. A value of `0.97` means 97% of the target width is used, leaving 3% padding (1.5% per side).

#### Text Measurement Mode

The `--text-measurement-mode` option controls the accuracy vs performance trade-off:

- **`fast`**: Fastest measurement using basic font metrics
- **`accurate`** (default): Good balance, accounts for kerning and glyph metrics
- **`pixel-perfect`**: Most accurate, accounts for anti-aliasing and subpixel rendering

## Examples

### Basic Video Generation

```bash
docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio-ffmpeg:latest \
  data.json - 30 --output video.mov
```

### With Layer Overrides

```bash
docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio-ffmpeg:latest \
  --layer-overrides layer-overrides.json \
  data.json - 30 \
  --output video.mov
```

### With Custom Text Padding and Measurement Mode

```bash
# Explicit output_dir (recommended for clarity)
docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio-ffmpeg:latest \
  --layer-overrides layer-overrides.json \
  --text-padding 0.95 \
  --text-measurement-mode pixel-perfect \
  data.json - 30 \
  --output video.mov

# Implicit output_dir (defaults to - in stream mode)
docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio-ffmpeg:latest \
  --layer-overrides layer-overrides.json \
  --text-padding 0.95 \
  --text-measurement-mode pixel-perfect \
  data.json 30 \
  --output video.mov
```

### With Debug Output

```bash
docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio-ffmpeg:latest \
  --debug \
  data.json - 30 \
  --output video.mov
```

### Using lotio Image (Manual FFmpeg Integration)

If you want to use lotio image and handle ffmpeg yourself:

```bash
# Render frames to disk
docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio:latest \
  data.json frames/ 30

# Then encode with ffmpeg manually
docker run --rm \
  -v "$(pwd):/workspace" \
  matrunchyk/lotio-ffmpeg:latest \
  ffmpeg -i frames/frame_%05d.png -c:v prores_ks -profile:v 4444 output.mov
```

## Output Format

The `lotio-ffmpeg` image outputs **ProRes 4444 MOV files** with alpha channel support by default. This format:

- Supports transparency (alpha channel)
- Is widely compatible with video editing software
- Maintains high quality
- Uses `yuva444p10le` pixel format (10-bit YUV with alpha)

### Video Specifications

- **Codec**: ProRes 4444 (`prores_ks` with profile `4444`)
- **Pixel Format**: `yuva444p10le` (10-bit YUV with alpha)
- **Container**: MOV (QuickTime)
- **Transparency**: Full alpha channel support
- **Input Format**: `image2pipe` (PNG frames streamed via stdin)

## Volume Mounts

### Input Files

Mount your Lottie JSON files and any dependencies:

```bash
-v "$(pwd)/samples:/workspace/input:ro"
```

- Use `:ro` (read-only) for input files
- Include text configuration files in the same mount
- Include any image assets referenced in the Lottie JSON

### Output Directory

Mount a directory for output:

```bash
-v "$(pwd):/workspace/output"
```

The output video will be written to the path specified by `--output`.

## Layer Overrides

Layer overrides work the same as the CLI. Mount your layer overrides file and reference it:

```bash
docker run --rm \
  -v "$(pwd)/samples:/workspace/input:ro" \
  -v "$(pwd):/workspace/output" \
  matrunchyk/lotio:latest \
  --layer-overrides /workspace/input/layer-overrides.json \
  /workspace/input/data.json - 30 \
  --output /workspace/output/video.mov
```

Example `layer-overrides.json`:

```json
{
  "textLayers": {
    "Text_1": {
      "minSize": 50,
      "maxSize": 222,
      "fallbackText": "Default text",
      "textBoxWidth": 720,
      "value": "Custom text value"
    },
    "Text_2": {
      "value": "Another text"
    }
  },
  "imageLayers": {
    "image_0": {
      "filePath": "images/",
      "fileName": "logo.png"
    },
    "image_1": {
      "filePath": "/workspace/input/",
      "fileName": "bg.png"
    }
  }
}
```

### Image Layers in Layer Overrides

**Path Resolution:**
- **Absolute paths**: Used as-is (e.g., `/workspace/input/images/logo.png`)
- **Relative paths**: Resolved relative to the **layer-overrides.json file's directory** (NOT the current working directory)
  - Example: If `layer-overrides.json` is at `/workspace/input/layer-overrides.json` and `filePath` is `"images/"`, it resolves relative to `/workspace/input/`
  - Important: This is different from `input.json` and `--layer-overrides` paths, which are resolved relative to the current working directory
- **URLs are NOT supported**: HTTP (`http://`) and HTTPS (`https://`) URLs are not supported
- **Empty `filePath`**: If `filePath` is an empty string, `fileName` must contain the full path

**Notes:**
- Image paths in the original Lottie JSON (`data.json`) are resolved relative to `data.json`'s parent directory
- If an asset ID is not in `imageLayers`, the original `u` and `p` from data.json are used
- Both `filePath` and `fileName` are optional - if not specified, defaults from `assets[].u` and `assets[].p` are used

## Fonts

Fonts can be mounted into the container. The container uses fontconfig to locate fonts:

```bash
docker run --rm \
  -v "$(pwd)/fonts:/usr/local/share/fonts:ro" \
  -v "$(pwd)/animation.json:/workspace/input.json:ro" \
  -v "$(pwd):/workspace/output" \
  matrunchyk/lotio:latest \
  /workspace/input.json - 30 \
  --output /workspace/output/video.mov
```

Fonts should be in TTF or OTF format and will be automatically discovered by fontconfig.

## Environment Variables

The container sets up the following environment variables:

- `PATH`: Includes `/opt/ffmpeg/bin` and `/usr/local/bin`
- `LD_LIBRARY_PATH`: Includes Skia and FFmpeg library paths

## Troubleshooting

### Video Not Generated

Check that:
- Input file path is correct and mounted
  - **Relative paths are resolved relative to the current working directory (cwd)** inside the container
  - Use absolute paths (e.g., `/workspace/input/data.json`) for clarity
- Output directory is writable
- Sufficient disk space is available
- Image paths in `imageLayers` are correct
  - **Relative paths in `imageLayers.filePath` are resolved relative to the layer-overrides.json file's directory** (NOT the current working directory)

### Text Not Replacing

Verify:
- Layer overrides file is mounted and accessible (supports both absolute and relative paths)
- Layer names in `layer-overrides.json` match layer names in Lottie JSON
- Layer overrides file is valid JSON

### Image Path Issues

If images aren't loading:
- Verify image paths in `imageLayers` are correct
- **Relative paths in `imageLayers.filePath` are resolved relative to the layer-overrides.json file's directory** (NOT the current working directory)
  - Example: If `layer-overrides.json` is at `/workspace/config/overrides.json` and `imageLayers` has `"image_0": { "filePath": "images/", "fileName": "logo.png" }`, it resolves to `/workspace/config/images/logo.png`
- Absolute paths in `imageLayers.filePath` are used as-is
- URLs (`http://`, `https://`) and data URIs (`data:`) are NOT supported in `imageLayers`
- Ensure image files exist and are accessible from within the container

### Font Issues

If fonts aren't loading:
- Ensure fonts are mounted correctly
- Check font file format (TTF/OTF)
- Verify font names match those in Lottie JSON

### Debug Mode

Enable debug output to see detailed information:

```bash
docker run --rm \
  -v "$(pwd)/animation.json:/workspace/input.json:ro" \
  -v "$(pwd):/workspace/output" \
  matrunchyk/lotio:latest \
  --debug \
  /workspace/input.json - 30 \
  --output /workspace/output/video.mov
```

## Performance

The Docker container uses:
- Multi-threaded rendering (one thread per CPU core)
- Optimized Skia build with official release settings
- Hardware-accelerated encoding where available

For best performance:
- Use SSD storage for input/output
- Allocate sufficient CPU cores to the container
- Use local volume mounts (not network filesystems)

## CI/CD Integration

The Docker image can be used in CI/CD pipelines:

```yaml
- name: Render animation
  run: |
    docker pull matrunchyk/lotio:latest
    docker run --rm \
      -v "${{ github.workspace }}/animation.json:/workspace/input.json:ro" \
      -v "${{ github.workspace }}/output:/workspace/output" \
      matrunchyk/lotio:latest \
      /workspace/input.json - 30 \
      --output /workspace/output/video.mov
```

## Build Optimizations

### FFmpeg Minimal Build

The `lotio-ffmpeg` image uses a minimal FFmpeg build optimized for lotio's use case:

**Included:**
- PNG decoder (for `image2pipe` input)
- `image2`/`image2pipe` demuxer (supports both pipe and file input)
- ProRes encoder (`prores_ks`) - ProRes 4444 with alpha channel
- MOV muxer
- Format filter (for pixel format conversion)
- Pipe and file protocols

**Excluded (not needed):**
- x264/x265 libraries (lotio uses ProRes, not H.264/H.265)
- Unnecessary codecs, filters, or protocols

**Benefits:**
- Faster FFmpeg build (no x264/x265 compilation)
- Smaller image size
- Still supports both pipe input (`image2pipe`) and disk-based input (`image2`)

### Docker Build Chain

The build chain is optimized for speed:

1. **Dockerfile.skia** - Builds Skia once (takes 15-20 minutes, but cached)
2. **Dockerfile.lotio** - Uses pre-built Skia (takes 2-3 minutes)
3. **Dockerfile.lotio-ffmpeg** - Uses pre-built lotio + builds minimal FFmpeg (takes 5-10 minutes)

**Total build time:** ~20-30 minutes (first time), but subsequent builds are much faster due to caching.

## Lambda/Container Integration

When using the `lotio-ffmpeg` image in Lambda functions or containers, the entrypoint script (`render_entrypoint.sh`) automatically handles video encoding. Here's how to structure your commands:

### Command Format

```bash
render_entrypoint.sh [LOTIO_OPTIONS] <input.json> [output_dir] [fps] --output <video.mov>
```

### Key Points

1. **Streaming is automatic**: The entrypoint script automatically adds `--stream` if not present
2. **`output_dir` is optional**: In stream mode, `output_dir` defaults to `-` if omitted
3. **All lotio options supported**: `--layer-overrides`, `--text-padding`, `--text-measurement-mode`, `--debug` all work
4. **Docker-specific options**: `--output` (or `-o`) specifies the final video file path

### Complete Example

```bash
render_entrypoint.sh \
  --layer-overrides /tmp/overrides.json \
  --text-padding 0.95 \
  --text-measurement-mode pixel-perfect \
  /tmp/input.json \
  --output /tmp/output.mov
```

**Note:** In the example above, `output_dir` is omitted because:
- The entrypoint script adds `--stream` automatically
- In stream mode, `output_dir` defaults to `-` (stdout)
- The `--output` flag specifies where the final video file is saved

### With Explicit Output Directory

```bash
render_entrypoint.sh \
  --layer-overrides /tmp/overrides.json \
  --text-padding 0.95 \
  /tmp/input.json \
  - \
  30 \
  --output /tmp/output.mov
```

Both forms are equivalent when using the entrypoint script.

### TypeScript/Node.js Integration Example

```typescript
import { exec } from "child_process";
import { promisify } from "util";

const execAsync = promisify(exec);

// Build command arguments
const args = [
  inputJsonPath,  // input.json (required)
  // output_dir omitted - defaults to "-" in stream mode
  // fps omitted - defaults to 30
  "--output", outputVideoPath
];

// Add optional arguments
if (layerOverridesPath) {
  args.unshift("--layer-overrides", layerOverridesPath);
}
if (textPadding !== undefined) {
  args.unshift("--text-padding", textPadding.toString());
}
if (textMeasurementMode) {
  args.unshift("--text-measurement-mode", textMeasurementMode);
}

// Execute
await execAsync(`render_entrypoint.sh ${args.map(a => `"${a}"`).join(" ")}`);
```

## Limitations

- **Architecture**: Multi-platform support (`linux/arm64`, `linux/amd64`)
- **Platform**: Designed for Linux containers (may work on macOS/Windows with proper Docker setup)
- **Fonts**: Requires fonts to be mounted; system fonts not included

## See Also

- [CLI Usage](cli.html) - Command-line interface documentation
- [Overview](overview.html) - General overview of Lotio
- [C++ Library](cpp-library.html) - Programmatic usage

