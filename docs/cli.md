# CLI Usage

The Lotio command-line interface provides a powerful way to render Lottie animations to image frames.

## Installation

### Homebrew (macOS)
```bash
brew tap matrunchyk/lotio
brew install lotio
```

### From Source
```bash
# Build lotio (binary build with zero bundled dependencies)
./scripts/build_binary.sh
```

## Basic Usage

```bash
lotio [OPTIONS] <input.json> <output_dir> [fps]
```

### Arguments

- `input.json` - Path to Lottie animation JSON file (required)
- `output_dir` - Directory to save rendered frames (required)
- `fps` - Frames per second for output (optional, default: animation fps or 30)

### Options

- `--stream` - Stream frames to stdout as PNG (for piping to ffmpeg)
- `--debug` - Enable debug output
- `--layer-overrides <config.json>` - Path to layer overrides JSON (for text auto-fit, dynamic text values, and image path overrides)
- `--text-padding <0.0-1.0>` - Text padding factor (default: 0.97 = 3% padding)
- `--text-measurement-mode <fast|accurate|pixel-perfect>` - Text measurement accuracy mode (default: accurate)
- `--version` - Print version information and exit
- `--help, -h` - Show help message

**Note:** Frames are output as PNG by default. No format selection is needed.

#### Text Padding

The `--text-padding` option controls how much of the target text box width is used for text sizing. A value of `0.97` means 97% of the target width is used, leaving 3% padding (1.5% per side). Lower values provide more padding, higher values allow text to use more of the available space.

- Range: `0.0` to `1.0`
- Default: `0.97` (3% padding)
- Example: `--text-padding 0.95` uses 95% of width (5% padding)

#### Text Measurement Mode

The `--text-measurement-mode` option controls the accuracy vs performance trade-off for measuring text width:

- **`fast`**: Fastest measurement using basic font metrics. Good for most cases but may underestimate width for some fonts.
- **`accurate`** (default): Good balance of accuracy and performance. Uses SkTextBlob bounds which accounts for kerning and glyph metrics. Recommended for most use cases.
- **`pixel-perfect`**: Most accurate measurement by rendering text and scanning actual pixels. Accounts for anti-aliasing and subpixel rendering. Slower but most precise.

Example: `--text-measurement-mode pixel-perfect`

## Examples

### Render to PNG

```bash
lotio animation.json frames/ 30
```

This will create PNG files in the `frames/` directory:
```
frames/
|
├── frame_0000.png
|
├── frame_0001.png
|
├── frame_0002.png
|
└── ...
```

### Stream to ffmpeg

```bash
lotio --stream animation.json - | ffmpeg -f image2pipe -i - output.mp4
```

This streams frames directly to ffmpeg for video encoding without creating intermediate files.

### With Layer Overrides

```bash
lotio --layer-overrides layer-overrides.json animation.json frames/
```

The layer overrides file allows you to:
- Replace text layer values dynamically
- Auto-fit font sizes to text boxes
- Override image paths by asset ID
- Customize text and image appearance

### With Custom Text Padding

```bash
lotio --text-padding 0.95 --layer-overrides layer-overrides.json animation.json frames/
```

Use 95% of text box width (5% padding) instead of the default 97% (3% padding).

### With Pixel-Perfect Text Measurement

```bash
lotio --text-measurement-mode pixel-perfect --layer-overrides layer-overrides.json animation.json frames/
```

Use the most accurate text measurement mode for precise text sizing.

Example `layer-overrides.json`:
```json
{
  "textLayers": {
    "Patient_Name": {
      "minSize": 20,
      "maxSize": 100,
      "textBoxWidth": 500
    }
  },
  "textValues": {
    "Patient_Name": "John Doe"
  }
}
```

### Debug Mode

```bash
lotio --debug animation.json frames/ 30
```

Enables verbose logging for troubleshooting.

## Layer Overrides

The layer overrides JSON file supports text and image customization:

### Text Layers

Define text layer properties:

```json
{
  "textLayers": {
    "LayerName": {
      "minSize": 10,        // Minimum font size
      "maxSize": 200,       // Maximum font size
      "textBoxWidth": 500,  // Text box width for auto-fit
      "textBoxHeight": 100  // Text box height (optional)
    }
  }
}
```

### Text Values

Replace text layer content:

```json
{
  "textValues": {
    "LayerName": "New Text Content"
  }
}
```

### Image Paths

Override image asset paths by asset ID. If no override is provided, the original path from data.json is used (fallback):

```json
{
  "imagePaths": {
    "image_0": "images/custom_image.png",
    "image_1": "/absolute/path/to/image.png"
  }
}
```

- Keys are asset IDs (e.g., `"image_0"`, `"image_1"`)
- Values are combined paths (directory + filename)
- Paths can be relative or absolute
- If an asset ID is not in `imagePaths`, the original `u` and `p` from data.json are used

### Complete Example

```json
{
  "textLayers": {
    "Title": {
      "minSize": 20,
      "maxSize": 100,
      "textBoxWidth": 800
    },
    "Subtitle": {
      "minSize": 12,
      "maxSize": 50,
      "textBoxWidth": 600
    }
  },
  "textValues": {
    "Title": "Welcome to Lotio",
    "Subtitle": "High-performance Lottie renderer"
  },
  "imagePaths": {
    "image_0": "images/custom_logo.png",
    "image_1": "/path/to/background.png"
  }
}
```

## Output Format

Frames are output as PNG files by default. PNG provides:
- Lossless compression
- Widely supported format
- Perfect for video encoding pipelines

### Streaming
- No intermediate files
- Direct to stdout as PNG
- Perfect for video encoding pipelines

## Performance Tips

1. **Stream for video**: Use `--stream` when piping to ffmpeg to avoid disk I/O
2. **Adjust FPS**: Lower FPS means fewer frames to render (faster)
3. **Multi-threading**: Lotio automatically uses multiple CPU cores

## Troubleshooting

### "Animation file not found"
- Check the path to your JSON file
- Use absolute paths if relative paths don't work

### "Output directory cannot be created"
- Ensure you have write permissions
- Create the directory first: `mkdir -p output_dir`

### "Text layer not found"
- Verify the layer name matches exactly (case-sensitive)
- Check the Lottie JSON structure

### "Font loading failed"
- Ensure fonts are available in the system font directories
- Check font file paths in the Lottie JSON

## Advanced Usage

### Batch Processing

```bash
for file in animations/*.json; do
    lotio "$file" "output/$(basename "$file" .json)/" 30
done
```

### Integration with Video Encoding

```bash
# Render frames
lotio --stream animation.json - | \
  ffmpeg -f image2pipe -r 30 -i - \
  -c:v libx264 -pix_fmt yuv420p \
  output.mp4
```

### Custom Frame Range

Currently, Lotio renders all frames. For frame ranges, you can:
1. Render all frames
2. Use external tools to select specific frames
3. Or use the C++ library for programmatic control

## See Also

- [Overview](./overview.html) - General information about Lotio
- [JS Library](./js-library.html) - Browser/WebAssembly usage
- [C++ Library](./cpp-library.html) - Programmatic C++ usage

