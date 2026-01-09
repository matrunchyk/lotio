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

## Path Resolution

**Important:** Understanding how paths are resolved is crucial for correct file access.

### Command-Line Arguments (`input.json`, `--layer-overrides`)

- **Absolute paths**: Used as-is (e.g., `/path/to/file.json`)
- **Relative paths**: Resolved relative to the **current working directory (cwd)** where you execute lotio
  - Example: If you run `lotio animation.json` from `/home/user/project/`, it looks for `/home/user/project/animation.json`
  - The cwd is where you run the command, not where the lotio binary is located

### Image Paths in `imagePaths` (layer-overrides.json)

- **Absolute paths**: Used as-is (e.g., `/workspace/images/logo.png`)
- **Relative paths**: Resolved relative to the **layer-overrides.json file's directory** (NOT the current working directory)
  - Example: If `layer-overrides.json` is at `/workspace/config/overrides.json` and `imagePaths` has `"image_0": "images/logo.png"`, it resolves to `/workspace/config/images/logo.png`
  - This is different from command-line arguments, which use the current working directory

### Summary

| Path Type | Resolution Base |
|-----------|----------------|
| `input.json` (relative) | Current working directory (cwd) |
| `--layer-overrides` (relative) | Current working directory (cwd) |
| `imagePaths` in layer-overrides.json (relative) | layer-overrides.json file's directory |

### Arguments

- `input.json` - Path to Lottie animation JSON file (required)
  - **Absolute paths**: Used as-is (e.g., `/path/to/animation.json`)
  - **Relative paths**: Resolved relative to the **current working directory (cwd)** where lotio is executed
    - Example: If you run `lotio animation.json` from `/home/user/project/`, it resolves to `/home/user/project/animation.json`
  - The parent directory of this file is used as the base directory for resolving relative image paths in the Lottie JSON
- `output_dir` - Directory to save rendered frames (required in non-stream mode; optional in stream mode, defaults to `-`)
- `fps` - Frames per second for output (optional, default: animation fps or 30)

**Note on `output_dir` in stream mode:** When using `--stream`, the `output_dir` argument is optional. If omitted, it defaults to `-` (stdout). You can explicitly specify `-` for clarity, or omit it entirely:
- `lotio --stream input.json - | ffmpeg...` (explicit)
- `lotio --stream input.json | ffmpeg...` (implicit, defaults to `-`)

### Options

- `--stream` - Stream frames to stdout as PNG (for piping to ffmpeg)
- `--debug` - Enable debug output
- `--layer-overrides <config.json>` - Path to layer overrides JSON (for text auto-fit, dynamic text values, and image path overrides)
  - **Absolute paths**: Used as-is (e.g., `/path/to/layer-overrides.json`)
  - **Relative paths**: Resolved relative to the **current working directory (cwd)** where lotio is executed
    - Example: If you run `lotio --layer-overrides config/overrides.json` from `/home/user/project/`, it resolves to `/home/user/project/config/overrides.json`
  - The parent directory of this file is used as the base directory for resolving relative image paths in `imagePaths`
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
# Explicit form (recommended for clarity)
lotio --stream animation.json - | ffmpeg -f image2pipe -i - output.mp4

# Implicit form (output_dir defaults to - in stream mode)
lotio --stream animation.json | ffmpeg -f image2pipe -i - output.mp4
```

This streams frames directly to ffmpeg for video encoding without creating intermediate files. Both forms are equivalent when using `--stream`.

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
      "textBoxWidth": 500,
      "value": "John Doe"
    }
  }
}
```

### Debug Mode

```bash
lotio --debug animation.json frames/ 30
```

Enables verbose logging for troubleshooting.

## Layer Overrides

The layer overrides JSON file supports text and image customization. All fields are optional.

### Text Layers

Define text layer properties:

```json
{
  "textLayers": {
    "LayerName": {
      "minSize": 10,        // Optional: Minimum font size (no auto-fit if not specified)
      "maxSize": 200,       // Optional: Maximum font size (no auto-fit if not specified)
      "fallbackText": "...", // Optional: Fallback text if text doesn't fit (defaults to empty)
      "textBoxWidth": 500,  // Optional: Text box width for auto-fit (defaults to frame size)
      "value": "New Text"   // Optional: Text value to set (defaults to original text from data.json)
    }
  }
}
```

**Validation Rules:**
- If both `minSize` and `maxSize` are specified: `maxSize > minSize`
- `minSize > 0`, `maxSize > 0`, `textBoxWidth > 0` if specified
- If `minSize` and `maxSize` are not specified, no auto-fit is performed (original font size is used)
- `value` can be an empty string (uses original text from data.json)

**Examples:**

```json
{
  "textLayers": {
    "Text_1": {
      "minSize": 50,
      "maxSize": 222,
      "fallbackText": "FALLBACK",
      "textBoxWidth": 720,
      "value": "OV3"
    },
    "Text_2": {
      "value": "OV1"  // Only text value override, no auto-fit
    },
    "Text_3": {
      "minSize": 50,
      "maxSize": 222
      // No value override, uses original text from data.json
    }
  }
}
```

### Image Layers

Override image asset paths by asset ID:

```json
{
  "imageLayers": {
    "image_0": {
      "filePath": "images/",    // Optional: Directory path (defaults to assets[].u)
      "fileName": "img_0.png"    // Optional: Filename (defaults to assets[].p)
    }
  }
}
```

**Path Resolution:**
- **Absolute paths**: Used as-is (e.g., `/workspace/images/logo.png`)
- **Relative paths**: Resolved relative to the **layer-overrides.json file's directory** (NOT the current working directory)
  - Example: If `layer-overrides.json` is at `/workspace/config/layer-overrides.json` and `filePath` is `"images/"`, it resolves relative to `/workspace/config/`
- **URLs are NOT supported**: HTTP (`http://`) and HTTPS (`https://`) URLs are not supported
- **Empty `filePath`**: If `filePath` is an empty string, `fileName` must contain the full path

**Validation Rules:**
- If `fileName` is specified but `filePath` is an empty string: validates full path from `fileName`
- If both `filePath` and `fileName` are specified (non-empty): validates combined path exists
- If both `filePath` and `fileName` are empty: error (invalid configuration)
- Non-existent asset IDs: warning only (ignored)

**Examples:**

```json
{
  "imageLayers": {
    "image_0": {
      // Full path: /tmp/folder/img_0.png
      "filePath": "",
      "fileName": "/tmp/folder/img_0.png"
    },
    "image_1": {
      // Full path: /tmp/folder/my_image.png
      "filePath": "/tmp/folder/",
      "fileName": "my_image.png"
    },
    "image_2": {
      // Full path: <layer-overrides-dir>/folder/my_image22.png
      "filePath": "folder/",
      "fileName": "my_image22.png"
    }
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
      "textBoxWidth": 800,
      "value": "Welcome to Lotio"
    },
    "Subtitle": {
      "minSize": 12,
      "maxSize": 50,
      "textBoxWidth": 600,
      "value": "High-performance Lottie renderer"
    }
  },
  "imageLayers": {
    "image_0": {
      "filePath": "images/",
      "fileName": "custom_logo.png"
    },
    "image_1": {
      "filePath": "/path/to/",
      "fileName": "background.png"
    }
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
- **Relative paths are resolved relative to the current working directory (cwd)** where you run lotio
  - Example: If you're in `/home/user/project/` and run `lotio animation.json`, it looks for `/home/user/project/animation.json`
- Use absolute paths if relative paths don't work
- Verify the file exists and you have read permissions

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
# Render frames (explicit output_dir)
lotio --stream animation.json - | \
  ffmpeg -f image2pipe -r 30 -i - \
  -c:v libx264 -pix_fmt yuv420p \
  output.mp4

# Render frames (implicit output_dir, defaults to -)
lotio --stream animation.json | \
  ffmpeg -f image2pipe -r 30 -i - \
  -c:v libx264 -pix_fmt yuv420p \
  output.mp4
```

### Programmatic Integration (Lambda/Container)

For programmatic use in Lambda functions or containers, you can build the command with optional arguments:

```bash
# Basic command structure
lotio [OPTIONS] <input.json> [output_dir] [fps]

# With all options (streaming mode)
lotio --stream \
  --layer-overrides overrides.json \
  --text-padding 0.95 \
  --text-measurement-mode pixel-perfect \
  input.json \
  - \
  30

# Simplified (output_dir can be omitted in stream mode)
lotio --stream \
  --layer-overrides overrides.json \
  --text-padding 0.95 \
  --text-measurement-mode pixel-perfect \
  input.json \
  30
```

**Key points for integration:**
- `output_dir` is optional when using `--stream` (defaults to `-`)
- All options (`--layer-overrides`, `--text-padding`, `--text-measurement-mode`) can be combined
- Options can appear in any order before the positional arguments
- `fps` is optional (defaults to animation fps or 30)

### Custom Frame Range

Currently, Lotio renders all frames. For frame ranges, you can:
1. Render all frames
2. Use external tools to select specific frames
3. Or use the C++ library for programmatic control

## See Also

- [Overview](./overview.html) - General information about Lotio
- [JS Library](./js-library.html) - Browser/WebAssembly usage
- [C++ Library](./cpp-library.html) - Programmatic C++ usage

