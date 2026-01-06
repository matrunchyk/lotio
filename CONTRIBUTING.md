# Contributing to Lotio

## Development Setup

1. **Clone the repository:**
   ```bash
   git clone https://github.com/matrunchyk/lotio.git
   cd lotio
   ```

2. **Install dependencies:**
   ```bash
   brew install fontconfig freetype harfbuzz icu4c libpng ninja python@3.11
   ```

3. **Build:**
   ```bash
   # Build lotio (binary build with zero bundled dependencies)
   ./scripts/build_binary.sh
   ```

## Code Organization

- **`src/core/`** - Core application logic
  - `argument_parser.cpp` - Command-line argument parsing
  - `animation_setup.cpp` - Skottie animation initialization
  - `frame_encoder.cpp` - Frame encoding (PNG)
  - `renderer.cpp` - Multi-threaded frame rendering

- **`src/text/`** - Text processing
  - `layer_overrides.cpp` - Layer overrides parsing (text and image)
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
2. Update `scripts/build_binary.sh` if adding new source files
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
# Build (recommended: binary build)
./scripts/build_binary.sh

# Test binary
./lotio --help

# Test with sample animation
./lotio sample.json output/ 30
```

## Submitting Changes

1. Create a feature branch
2. Make your changes
3. Test thoroughly
4. Submit a pull request with a clear description

## Release Process

Releases are automatically created on push to `main`:
- Version format: Semantic versioning `v1.2.3` (e.g., `v1.0.0`, `v1.2.3`, `v2.0.0`)
- Automatically bumps patch version on each push to `main`
- Creates headers-only and full packages
- Updates Homebrew tap automatically

No manual release steps needed.

