# Lotio

High-performance Lottie animation frame renderer using Skia. Renders animations to PNG/WebP frames for video encoding.

## Installation

### Homebrew (Recommended)

```bash
brew tap matrunchyk/lotio
brew install lotio
```

This installs:
- Binary: `lotio`
- Headers: `/opt/homebrew/include/lotio/` (or `/usr/local/include/lotio/`)
- Libraries: `/opt/homebrew/lib/` (Skia static libraries)

### From Source

**Prerequisites (macOS):**
```bash
brew install fontconfig freetype harfbuzz icu4c libpng jpeg-turbo libwebp ninja python@3.11
xcode-select --install
```

**Build:**
```bash
# Build Skia (first time only, takes a while)
./install_skia.sh

# Build lotio
./build_local.sh
# or
make
```

## Usage

```bash
lotio [--png] [--webp] [--stream] [--debug] [--text-config <config.json>] <input.json> <output_dir> [fps]
```

**Options:**
- `--png` - Output frames as PNG files
- `--webp` - Output frames as WebP files
- `--stream` - Stream frames to stdout (for piping to ffmpeg)
- `--debug` - Enable debug output
- `--text-config` - Path to text configuration JSON (for auto-fit and dynamic text values)
- `fps` - Frames per second for output (default: 25)

**Examples:**
```bash
# Render to PNG and WebP
lotio --png --webp animation.json frames/ 30

# Stream to ffmpeg
lotio --png --stream animation.json - | ffmpeg -f image2pipe -i - output.mp4

# With text configuration
lotio --png --text-config text_config.json animation.json frames/
```

## Samples

The `samples/` directory contains example Lottie animations and configurations:

- **`samples/sample1/`** - Basic animation with text configuration
  - `data.json` - Lottie animation file
  - `text-config.json` - Text customization configuration
  - `output/` - Rendered frames (run lotio to generate)

- **`samples/sample2/`** - Animation with external images
  - `data.json` - Lottie animation file with image references
  - `images/` - External image assets referenced by the animation
  - `output/` - Rendered frames (run lotio to generate)

**Try the samples:**
```bash
# Sample 1: Basic animation with text customization
cd samples/sample1
lotio --png --webp --text-config text-config.json data.json output/ 30

# Sample 2: Animation with external images
cd samples/sample2
lotio --png --webp data.json output/ 30
```

## Using as a Library

### Headers

Headers are installed at `/opt/homebrew/include/lotio/` (or `/usr/local/include/lotio/`):

```cpp
#include <lotio/core/animation_setup.h>
#include <lotio/text/text_processor.h>
#include <lotio/utils/logging.h>
```

### Linking

Link with Skia libraries:

```bash
g++ -I/opt/homebrew/include -L/opt/homebrew/lib \
    -llotio -lskottie -lskia -lskparagraph -lsksg -lskshaper \
    -lskunicode_icu -lskunicode_core -lskresources -ljsonreader \
    your_app.cpp -o your_app
```

Or use pkg-config:

```bash
g++ $(pkg-config --cflags --libs lotio) your_app.cpp -o your_app
```

## Project Structure

```
src/
├── core/          # Core functionality (argument parsing, animation setup, rendering)
├── text/          # Text processing (configuration, font handling, sizing)
└── utils/         # Utilities (logging, string utils, crash handling)
```

## IDE Setup

The project includes IDE configuration for Cursor/VS Code:
- `.vscode/c_cpp_properties.json` - C/C++ extension settings
- `.clangd` - clangd language server settings

Reload Cursor/VS Code after cloning: `Cmd+Shift+P` → "Reload Window"

## Troubleshooting

**Skia build fails:**
- Ensure all dependencies are installed
- Check sufficient disk space (Skia build is large)
- Review error messages in `install_skia.sh` output

**Linker errors:**
- Verify Skia libraries exist in `third_party/skia/skia/out/Release/`
- Check library paths in build script

**IDE include errors:**
- Reload Cursor/VS Code
- Verify `.vscode/c_cpp_properties.json` has correct paths

## License

See individual component licenses:
- Skia: `third_party/skia/skia/LICENSE`
