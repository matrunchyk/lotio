# Lottie Frame Renderer

Render Lottie animations frame-by-frame to PNG and/or WebP files for video encoding with ffmpeg.

## Quick Start

```bash
# 1. Build Skia (if not already built)
./install_skia.sh

# 2. Build the project
./build_local.sh

# Or use Makefile
make

# 3. Run
./lotio --png --webp input.json output_dir [fps]
```

## Project Structure

```
cpp/
├── src/                    # Source code
│   ├── main.cpp           # Main entry point
│   ├── core/              # Core functionality
│   │   ├── argument_parser.cpp
│   │   ├── animation_setup.cpp
│   │   ├── frame_encoder.cpp
│   │   └── renderer.cpp
│   ├── text/             # Text processing
│   │   ├── text_config.cpp
│   │   ├── text_processor.cpp
│   │   ├── font_utils.cpp
│   │   ├── text_sizing.cpp
│   │   └── json_manipulation.cpp
│   └── utils/             # Utilities
│       ├── logging.cpp
│       ├── string_utils.cpp
│       └── crash_handler.cpp
├── third_party/           # Third-party dependencies
│   └── skia/             # Skia graphics library
│       └── skia/         # Skia root (include/, modules/)
├── build_local.sh         # Local build script
├── Makefile               # Makefile build system
├── install_skia.sh        # Skia build script
└── Dockerfile             # Docker build (for Linux/CI)
```

## Building

### Prerequisites

**macOS:**

1. **Install Homebrew** (if not already installed):
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. **Install Xcode Command Line Tools** (required for compilation):
   ```bash
   xcode-select --install
   ```

3. **Install build dependencies:**
   ```bash
   brew install fontconfig freetype harfbuzz icu4c libpng jpeg libwebp ninja python3
   ```

4. **Verify installation:**
   ```bash
   brew list fontconfig freetype harfbuzz icu4c libpng jpeg libwebp ninja python3
   ```

   **Note:** The `install_skia.sh` script automatically detects macOS and uses Homebrew paths for fontconfig and freetype headers.

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential clang ninja-build python3 \
    libfontconfig1-dev libfreetype6-dev libharfbuzz-dev \
    libicu-dev libpng-dev libjpeg-dev libwebp-dev
```

### Build Steps

1. **Build Skia** (first time only, takes a while):
   ```bash
   ./install_skia.sh
   ```

2. **Build the application:**
   ```bash
   # Option 1: Build script (recommended)
   ./build_local.sh
   
   # Option 2: Makefile
   make
   ```

   Both methods produce the same result - use whichever you prefer.

The build script will:
- Check if Skia is built
- Compile all source files
- Link with Skia libraries
- Create `lotio` binary

## Usage

```bash
./lotio [--png] [--webp] [--stream] [--debug] [--text-config <config.json>] <input.json> <output_dir> [fps]
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
./lotio --png --webp animation.json frames/ 30

# Stream to ffmpeg
./lotio --png --stream animation.json - | ffmpeg -f image2pipe -i - output.mp4

# With text configuration
./lotio --png --text-config text_config.json animation.json frames/
```

## Third-Party Dependencies

### Skia Graphics Library

- **Location**: `third_party/skia/skia/`
- **Source**: https://skia.org/
- **License**: See `third_party/skia/skia/LICENSE`

**Build Configuration:**
- Include path: `-I third_party/skia/skia`
- Libraries: Located in `third_party/skia/skia/out/Release/`

**Includes work as:**
- `#include "include/core/SkCanvas.h"`
- `#include "modules/skottie/include/Skottie.h"`

## IDE Configuration

The project includes IDE configuration for Cursor/VS Code:

- **`.vscode/c_cpp_properties.json`** - C/C++ extension settings
- **`.clangd`** - clangd language server settings

Both are configured with the correct include paths. **Reload Cursor/VS Code** after setup:
- Press `Cmd+Shift+P` → "Reload Window"

## Troubleshooting

### Skia Build Fails
- Check that all dependencies are installed
- Ensure sufficient disk space (Skia build is large)
- Review error messages in `install_skia.sh` output

### Linker Errors
- Verify Skia libraries exist in `third_party/skia/skia/out/Release/`
- Check library paths in build script
- On macOS, ensure frameworks are linked correctly

### Include Path Errors (IDE)
- Verify `.vscode/c_cpp_properties.json` has correct paths
- Reload Cursor/VS Code
- Check that `third_party/skia/skia/include` exists

### Compilation Errors
- Ensure Skia is built: `./install_skia.sh`
- Check that include paths are correct: `-I third_party/skia/skia`
- Verify all source files exist

## Docker Build

For Linux builds or CI/CD, use the Dockerfile:

```bash
docker build -t lottie-renderer .
docker run -v $(pwd):/workspace lottie-renderer [args]
```

The Docker build targets Linux ARM64 and includes FFmpeg.

## Development

### Code Organization

- **`src/core/`** - Core application logic (argument parsing, animation setup, rendering)
- **`src/text/`** - Text processing (configuration, font handling, sizing, JSON manipulation)
- **`src/utils/`** - General utilities (logging, string utils, crash handling)

### Adding New Features

1. Add source files to appropriate module directory
2. Update `build_local.sh` and `Makefile` if adding new source files
3. Follow existing code structure and patterns

## License

See individual component licenses:
- Skia: `third_party/skia/skia/LICENSE`
- Your code: (specify your license)


## Monitoring Build Progress

To check if Skia is still building:

```bash
# Quick status check
./check_build.sh

# Monitor in real-time (updates every 2 seconds)
watch -n 2 './check_build.sh'

# Or watch the build log directly
tail -f third_party/skia/skia/out/Release/.ninja_log
```

**If build stopped:**
```bash
# Restart the build
cd third_party/skia/skia
ninja -C out/Release
```
