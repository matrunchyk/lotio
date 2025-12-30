# Contributing to Lotio

## Development Setup

1. **Clone the repository:**
   ```bash
   git clone https://github.com/matrunchyk/lotio.git
   cd lotio
   ```

2. **Install dependencies:**
   ```bash
   brew install fontconfig freetype harfbuzz icu4c libpng jpeg-turbo libwebp ninja python@3.11
   ```

3. **Build:**
   ```bash
   ./scripts/install_skia.sh  # First time only
   ./scripts/build_local.sh
   ```

## Code Organization

- **`src/core/`** - Core application logic
  - `argument_parser.cpp` - Command-line argument parsing
  - `animation_setup.cpp` - Skottie animation initialization
  - `frame_encoder.cpp` - Frame encoding (PNG/WebP)
  - `renderer.cpp` - Multi-threaded frame rendering

- **`src/text/`** - Text processing
  - `text_config.cpp` - Text configuration parsing
  - `text_processor.cpp` - Text layer modification
  - `font_utils.cpp` - Font utilities
  - `text_sizing.cpp` - Optimal font sizing
  - `json_manipulation.cpp` - JSON manipulation utilities

- **`src/utils/`** - General utilities
  - `logging.cpp` - Logging utilities
  - `string_utils.cpp` - String utilities
  - `crash_handler.cpp` - Crash and exception handlers

## Adding New Features

1. Add source files to the appropriate module directory
2. Update `scripts/build_local.sh` and `Makefile` if adding new source files
3. Follow existing code structure and patterns
4. Update headers in the same directory as implementation files

## Code Style

- Use C++17 standard
- Follow existing naming conventions
- Add comments for complex logic
- Keep functions focused and modular

## Testing

Test your changes locally:

```bash
# Build
./scripts/build_local.sh

# Test binary
./lotio --help

# Test with sample animation
./lotio --png sample.json output/ 30
```

## Submitting Changes

1. Create a feature branch
2. Make your changes
3. Test thoroughly
4. Submit a pull request with a clear description

## Release Process

Releases are automatically created on push to `main`:
- Version format: `vYYYYMMDD-SHA` (e.g., `v20241228-a1b2c3d`)
- Creates headers-only and full packages
- Updates Homebrew tap automatically

No manual release steps needed.

