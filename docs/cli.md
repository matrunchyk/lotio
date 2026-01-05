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
# Build lotio (minimal build with zero bundled dependencies)
./scripts/build_minimal.sh
```

## Basic Usage

```bash
lotio [OPTIONS] <input.json> <output_dir> [fps]
```

### Arguments

- `input.json` - Path to Lottie animation JSON file (required)
- `output_dir` - Directory to save rendered frames (required)
- `fps` - Frames per second for output (optional, default: 25)

### Options

- `--png` - Output frames as PNG files
- `--webp` - Output frames as WebP files
- `--stream` - Stream frames to stdout (for piping to ffmpeg)
- `--debug` - Enable debug output
- `--text-config <config.json>` - Path to text configuration JSON (for auto-fit and dynamic text values)
- `--text-padding <0.0-1.0>` - Text padding factor (default: 0.97 = 3% padding)
- `--text-measurement-mode <fast|accurate|pixel-perfect>` - Text measurement accuracy mode (default: accurate)
- `--help` - Show help message

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
lotio --png animation.json frames/ 30
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

### Render to Both PNG and WebP

```bash
lotio --png --webp animation.json frames/ 30
```

### Stream to ffmpeg

```bash
lotio --png --stream animation.json - | ffmpeg -f image2pipe -i - output.mp4
```

This streams frames directly to ffmpeg for video encoding without creating intermediate files.

### With Text Configuration

```bash
lotio --png --text-config text_config.json animation.json frames/
```

The text configuration file allows you to:
- Replace text layer values dynamically
- Auto-fit font sizes to text boxes
- Customize text appearance

### With Custom Text Padding

```bash
lotio --png --text-padding 0.95 --text-config text_config.json animation.json frames/
```

Use 95% of text box width (5% padding) instead of the default 97% (3% padding).

### With Pixel-Perfect Text Measurement

```bash
lotio --png --text-measurement-mode pixel-perfect --text-config text_config.json animation.json frames/
```

Use the most accurate text measurement mode for precise text sizing.

Example `text_config.json`:
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
lotio --png --debug animation.json frames/ 30
```

Enables verbose logging for troubleshooting.

## Text Configuration

The text configuration JSON file supports:

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
  }
}
```

## Output Formats

### PNG
- Lossless compression
- Widely supported
- Larger file sizes

### WebP
- Better compression
- Smaller file sizes
- Modern format

### Streaming
- No intermediate files
- Direct to stdout
- Perfect for video encoding pipelines

## Performance Tips

1. **Use WebP for smaller files**: If file size matters, use `--webp`
2. **Stream for video**: Use `--stream` when piping to ffmpeg to avoid disk I/O
3. **Adjust FPS**: Lower FPS means fewer frames to render (faster)
4. **Multi-threading**: Lotio automatically uses multiple CPU cores

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
    lotio --png "$file" "output/$(basename "$file" .json)/" 30
done
```

### Integration with Video Encoding

```bash
# Render frames
lotio --png --stream animation.json - | \
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

