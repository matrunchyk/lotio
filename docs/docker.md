# Docker Usage

Lotio provides a Docker image for easy deployment and consistent rendering across different environments. The Docker image includes lotio, Skia libraries, and ffmpeg for complete video encoding capabilities.

## Quick Start

```bash
docker run --rm \
  -v "$(pwd)/input:/workspace/input:ro" \
  -v "$(pwd)/output:/workspace/output" \
  lotio:latest \
  input.json - 30 --output /workspace/output/video.mov
```

## Building the Image

### From Source

```bash
docker build -t lotio:latest -f Dockerfile .
```

The Dockerfile uses a multi-stage build process:
1. **Skia Builder**: Compiles Skia with Skottie support
2. **Render Builder**: Compiles lotio binary
3. **FFmpeg Builder**: Builds ffmpeg with ProRes support
4. **Runtime**: Final image with all tools

### Image Details

- **Base Image**: Ubuntu 22.04
- **Architecture**: ARM64 (Linux)
- **Includes**:
  - lotio binary
  - Skia libraries (with Skottie, SkParagraph, etc.)
  - FFmpeg with ProRes 4444 support
  - Font configuration support

## Basic Usage

### Render Animation to Video

```bash
docker run --rm \
  -v "$(pwd)/animation.json:/workspace/input.json:ro" \
  -v "$(pwd):/workspace/output" \
  lotio:latest \
  /workspace/input.json - 30 --output /workspace/output/output.mov
```

### With Input Directory

```bash
docker run --rm \
  -v "$(pwd)/samples:/workspace/input:ro" \
  -v "$(pwd):/workspace/output" \
  lotio:latest \
  /workspace/input/data.json - 30 --output /workspace/output/video.mov
```

## Command-Line Arguments

The Docker container passes through all lotio command-line arguments. The entrypoint script automatically adds `--png --stream` if not present (required for video encoding).

### Lotio Arguments

All standard lotio arguments are supported:

- `--png` - Output frames as PNG (automatically added if missing)
- `--webp` - Also output WebP frames
- `--stream` - Stream frames to stdout (automatically added if missing)
- `--debug` - Enable debug output
- `--text-config <file>` - Path to text configuration JSON
- `--text-padding <0.0-1.0>` - Text padding factor (default: 0.97 = 3% padding)
- `--text-measurement-mode <fast|accurate|pixel-perfect>` - Text measurement mode (default: accurate)
- `<input.json>` - Input Lottie animation file (required)
- `<output_dir>` - Output directory (use `-` for streaming)
- `[fps]` - Frames per second (default: 25)

### Docker-Specific Arguments

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
  -v "$(pwd)/animation.json:/workspace/input.json:ro" \
  -v "$(pwd):/workspace/output" \
  lotio:latest \
  /workspace/input.json - 30 --output /workspace/output/video.mov
```

### With Text Configuration

```bash
docker run --rm \
  -v "$(pwd)/samples:/workspace/input:ro" \
  -v "$(pwd):/workspace/output" \
  lotio:latest \
  --text-config /workspace/input/text-config.json \
  /workspace/input/data.json - 30 \
  --output /workspace/output/video.mov
```

### With Custom Text Padding and Measurement Mode

```bash
docker run --rm \
  -v "$(pwd)/samples:/workspace/input:ro" \
  -v "$(pwd):/workspace/output" \
  lotio:latest \
  --text-config /workspace/input/text-config.json \
  --text-padding 0.95 \
  --text-measurement-mode pixel-perfect \
  /workspace/input/data.json - 30 \
  --output /workspace/output/video.mov
```

### With Debug Output

```bash
docker run --rm \
  -v "$(pwd)/animation.json:/workspace/input.json:ro" \
  -v "$(pwd):/workspace/output" \
  lotio:latest \
  --debug \
  /workspace/input.json - 30 \
  --output /workspace/output/video.mov
```

### Multiple Output Formats

```bash
docker run --rm \
  -v "$(pwd)/animation.json:/workspace/input.json:ro" \
  -v "$(pwd):/workspace/output" \
  lotio:latest \
  --png --webp --debug \
  /workspace/input.json - 30 \
  --output /workspace/output/video.mov
```

## Output Format

The Docker container outputs **ProRes 4444 MOV files** with alpha channel support by default. This format:

- Supports transparency (alpha channel)
- Is widely compatible with video editing software
- Maintains high quality
- Uses `yuva444p10le` pixel format (10-bit YUV with alpha)

### Video Specifications

- **Codec**: ProRes 4444 (`ap4h`)
- **Pixel Format**: `yuva444p12le` (12-bit YUV with alpha)
- **Container**: MOV (QuickTime)
- **Transparency**: Full alpha channel support

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

## Text Configuration

Text configuration works the same as the CLI. Mount your text config file and reference it:

```bash
docker run --rm \
  -v "$(pwd)/samples:/workspace/input:ro" \
  -v "$(pwd):/workspace/output" \
  lotio:latest \
  --text-config /workspace/input/text-config.json \
  /workspace/input/data.json - 30 \
  --output /workspace/output/video.mov
```

Example `text-config.json`:

```json
{
  "textLayers": {
    "Text_1": {
      "minSize": 50,
      "maxSize": 222,
      "fallbackText": "Default text",
      "textBoxWidth": 720
    }
  },
  "textValues": {
    "Text_1": "Custom text value",
    "Text_2": "Another text"
  }
}
```

## Fonts

Fonts can be mounted into the container. The container uses fontconfig to locate fonts:

```bash
docker run --rm \
  -v "$(pwd)/fonts:/usr/local/share/fonts:ro" \
  -v "$(pwd)/animation.json:/workspace/input.json:ro" \
  -v "$(pwd):/workspace/output" \
  lotio:latest \
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
- Output directory is writable
- Sufficient disk space is available

### Text Not Replacing

Verify:
- Text configuration file is mounted and accessible
- Layer names in `text-config.json` match layer names in Lottie JSON
- Text config file is valid JSON

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
  lotio:latest \
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
    docker run --rm \
      -v "${{ github.workspace }}/animation.json:/workspace/input.json:ro" \
      -v "${{ github.workspace }}/output:/workspace/output" \
      lotio:latest \
      /workspace/input.json - 30 \
      --output /workspace/output/video.mov
```

## Limitations

- **Architecture**: Currently built for ARM64 Linux
- **Platform**: Designed for Linux containers (may work on macOS/Windows with proper Docker setup)
- **Fonts**: Requires fonts to be mounted; system fonts not included

## See Also

- [CLI Usage](cli.html) - Command-line interface documentation
- [Overview](overview.html) - General overview of Lotio
- [C++ Library](cpp-library.html) - Programmatic usage

