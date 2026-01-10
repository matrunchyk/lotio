# Scripts Directory

This directory contains build, test, and utility scripts for the `lotio` project.

## Core Build Scripts

### `build_binary.sh` ⭐ **RECOMMENDED**
**Purpose:** Builds lotio with zero bundled dependencies (all use system/Homebrew libraries)

**Usage:**
```bash
./scripts/build_binary.sh
```

**When to use:**
- **Primary build method** - recommended for all local development
- Building with zero bundled dependencies for faster builds
- Reducing build size and complexity
- CI/CD optimization

**Key Features:**
- Zero bundled dependencies (all use system/Homebrew libraries)
- Faster builds (no JPEG codecs, no GPU backends)
- Auto-detects ICU version (supports ICU 44-100)
- Uses ccache if available for faster rebuilds
- Handles everything: Skia setup, build, and lotio compilation

**Prerequisites:** 
- System libraries: fontconfig, freetype, harfbuzz, icu4c, libpng, ninja, python3
- No bundled dependencies needed!

**Output:** 
- Creates `lotio` binary in project root
- Creates `liblotio.a` static library (for development and Homebrew packaging)

**Used by:**
- Primary build method
- Test scripts
- CI/CD workflows (recommended)

---

### `build_wasm.sh`
**Purpose:** Builds the WebAssembly version of lotio for browser use (unified script)

**Usage:**
```bash
./scripts/build_wasm.sh
```

**When to use:**
- Building WASM version for browser library
- Before publishing to npm/GitHub Packages
- Testing browser functionality

**Prerequisites:** 
- Emscripten must be installed (`brew install emscripten` or EMSDK)
- Skia will be built automatically if not present

**Output:** 
- `browser/lotio.wasm`
- `browser/lotio.js`
- `browser/wasm.js`

**Used by:**
- CI/CD workflows (for npm package publishing)
- `test.sh` (unified test script)

---

### `_build_deps.sh` (Internal)
**Purpose:** Shared dependencies build script (Skia and nlohmann/json) used by `build_binary.sh` and `build_wasm.sh`

**Usage:**
```bash
# Not meant to be called directly - used internally by other build scripts
scripts/_build_deps.sh --target=binary --target-cpu=arm64
```

**When to use:**
- Only if you need to build Skia separately without building lotio
- Generally not called directly - use `build_binary.sh` or `build_wasm.sh` instead

**Key Features:**
- Supports both binary (native) and WASM targets
- Handles Skia structure setup, dependency fetching, and compilation
- Auto-detects target CPU architecture
- Can skip setup for incremental builds

**Parameters:**
- `--target=binary|wasm`: Build target (default: binary)
- `--target-cpu=arm64|x64`: Target CPU for native builds (auto-detected if not specified)
- `--skip-setup`: Skip Skia structure setup (assumes it exists)

---

### `build_skia_docker_multi.sh`
**Purpose:** Builds and pushes multi-arch Skia Docker image to Docker Hub for use as a base image

**Usage:**
```bash
./scripts/build_skia_docker_multi.sh
./scripts/build_skia_docker_multi.sh --tag=v1.0.0
./scripts/build_skia_docker_multi.sh --no-cache
```

**When to use:**
- Building Skia Docker image for use as base image in other Dockerfiles
- Publishing Skia image to Docker Hub for multi-arch support
- Creating platform-specific Skia builds (arm64 and x86_64)
- **Avoiding Skia rebuilds** - Use this image as base to skip cloning and compiling Skia

**Prerequisites:**
- Docker must be installed and running
- Must be logged in to Docker Hub: `docker login`
- BuildKit must be enabled (default in Docker 20.10+)

**Key Features:**
- Builds for both linux/arm64 and linux/amd64 platforms
- Creates multi-arch manifest (single tag for both platforms)
- Uses registry cache for faster rebuilds (stored separately in `matrunchyk/skia:cache`)
- Automatically sets up buildx builder if needed
- Verifies Docker Hub authentication before building

**What the Image Contains:**
- **Skia libraries** (`/opt/skia/lib/*.a`) - Pre-compiled static libraries
- **Skia headers** (`/opt/skia/include`, `/opt/skia/modules`, `/opt/skia/gen`) - All headers needed for compilation
- **Build tools** - g++, gcc, make, pkg-config (ready to compile against Skia)
- **System dependencies** - All dev packages Skia links against (fontconfig, freetype, harfbuzz, icu, libpng, etc.)

**How to Use as Base Image:**
```dockerfile
# Use the Skia base image - no need to clone or compile Skia!
FROM matrunchyk/skia:latest AS my-app

WORKDIR /build

# Copy your source code
COPY src/ ./src/

# Compile against Skia (everything is already set up!)
RUN g++ -std=c++17 -O3 \
    -I/opt/skia/include \
    -I/opt/skia/modules \
    -I/opt/skia/gen \
    -L/opt/skia/lib \
    src/main.cpp src/renderer.cpp \
    -lskia -lskottie -lskparagraph -lsksg -lskshaper \
    -lskunicode_icu -lskunicode_core -lskresources -ljsonreader \
    -lfreetype -lpng -lharfbuzz -licuuc -licui18n -licudata \
    -lz -lfontconfig -lexpat -lm -lpthread \
    -lX11 -lGL -lGLU \
    -o /usr/local/bin/myapp
```

**Benefits:**
- ✅ **No Skia cloning** - Skia is already built and included
- ✅ **No Skia compilation** - Pre-compiled libraries ready to link
- ✅ **Faster builds** - Skip 10-20 minutes of Skia build time
- ✅ **Multi-arch support** - Docker automatically selects correct platform
- ✅ **Ready to compile** - All build tools and dependencies included

**Output:**
- Pushes `matrunchyk/skia:latest` (or custom tag) to Docker Hub
- Cache stored separately in `matrunchyk/skia:cache` (doesn't interfere with image)

**Environment Variables:**
- `DOCKER_USERNAME`: Docker Hub username (default: matrunchyk)
- `DOCKER_TAG`: Tag to use (default: latest)

---

## Docker Build Chain

The project uses a multi-stage Docker build chain for optimized, fast builds:

### Build Chain Overview

```
Dockerfile.skia → matrunchyk/skia:latest
    ↓
Dockerfile.lotio → matrunchyk/lotio:latest (uses matrunchyk/skia:latest)
    ↓
Dockerfile.lotio-ffmpeg → matrunchyk/lotio-ffmpeg:latest (uses matrunchyk/lotio:latest)
```

### Dockerfile.skia
**Purpose:** Base image with pre-built Skia libraries and headers

**What it contains:**
- Pre-compiled Skia static libraries (`/opt/skia/lib/*.a`)
- Skia headers (`/opt/skia/include`, `/opt/skia/modules`, `/opt/skia/gen`)
- Skia source directory (`/opt/skia/src`) - required for internal headers
- Build tools (g++, gcc, make, pkg-config)
- System dependencies (fontconfig, freetype, harfbuzz, icu, libpng, etc.)

**Built by:** `build_skia_docker_multi.sh`

**Benefits:**
- Skia is built once and reused across all lotio builds
- Reduces lotio build time from 15-25 minutes to 2-3 minutes
- Multi-platform support (arm64 and amd64)

### Dockerfile.lotio
**Purpose:** Builds lotio binary using pre-built Skia image

**What it contains:**
- lotio binary (`/usr/local/bin/lotio`)
- lotio static library (`/usr/local/lib/liblotio.a`)
- lotio headers (`/usr/local/include/lotio/`)
- Skia libraries and headers (copied from base image)

**Base image:** `matrunchyk/skia:latest`

**Key optimizations:**
- Uses pre-built Skia (no Skia compilation)
- Only compiles lotio source files
- Minimal runtime image (only required libraries)

**Multi-platform:** Supports `linux/arm64` and `linux/amd64`

### Dockerfile.lotio-ffmpeg
**Purpose:** Adds FFmpeg to lotio image for video encoding

**What it contains:**
- Everything from `matrunchyk/lotio:latest`
- FFmpeg 8.0 with minimal build (optimized for lotio's use case)
- Entrypoint script for automatic video encoding

**Base image:** `matrunchyk/lotio:latest`

**FFmpeg build optimizations:**
- **Minimal build** - Only includes what lotio needs:
  - PNG decoder (for `image2pipe` input)
  - `image2`/`image2pipe` demuxer (for pipe and file input)
  - ProRes encoder (`prores_ks`) - ProRes 4444 with alpha channel
  - MOV muxer (for output)
  - Format filter (for pixel format conversion)
  - Pipe and file protocols
- **Removed features:**
  - No x264/x265 libraries (lotio uses ProRes, not H.264/H.265)
  - No unnecessary codecs, filters, or protocols
- **Benefits:**
  - Faster FFmpeg build (no x264/x265 compilation)
  - Smaller image size
  - Still supports both pipe input (`image2pipe`) and disk-based input (`image2`)

**Entrypoint behavior:**
- If first argument is a command (e.g., `ffmpeg`, `lotio`), executes it directly
- Otherwise, treats arguments as lotio commands and automatically:
  - Adds `--stream` if not present
  - Pipes lotio PNG output to ffmpeg
  - Encodes to ProRes 4444 MOV with alpha channel

**Usage examples:**
```bash
# Render Lottie to video (automatic ffmpeg encoding)
docker run -v $(pwd):/workspace matrunchyk/lotio-ffmpeg:latest \
  data.json - 30 --layer-overrides layer-overrides.json --output video.mov

# Direct command execution
docker run matrunchyk/lotio-ffmpeg:latest ffmpeg -version
docker run matrunchyk/lotio-ffmpeg:latest lotio --version
```

**Multi-platform:** Supports `linux/arm64` and `linux/amd64` with architecture-specific tags (`-arm64`, `-amd64`)

---

## Testing Scripts

### `test_bottle.sh`
**Purpose:** Tests Homebrew bottle creation locally before CI

**Usage:**
```bash
./scripts/test_bottle.sh
```

**When to use:**
- **Before pushing to `main` branch** (saves 40 minutes of CI time!)
- Testing bottle structure changes
- Validating release workflow bottle creation
- Debugging bottle creation issues

**Prerequisites:**
- Binary and library must be built: `./scripts/build_binary.sh`

**What it does:**
- Creates bottle directory structure (matches CI exactly)
- Copies binary, headers, and libraries
- Creates pkg-config file
- Generates tarball with correct structure
- Calculates SHA256
- Tests tarball extraction
- Validates extracted structure
- **Automatically cleans up** test files on exit

**Output:**
- Test tarball: `lotio-<version>-<arch>.bottle.tar.gz`
- Detailed validation report
- SHA256 checksum

**Benefits:**
- ✅ Catch bottle creation errors in seconds instead of waiting 40 minutes
- ✅ Validate directory structure before CI
- ✅ Test pkg-config file format
- ✅ Verify all required files are included

**Example output:**
```
🧪 Testing Homebrew Bottle Creation
===================================
📋 Test Configuration:
   Version: v1.2.3-test
   Architecture: arm64
   Bottle Arch: arm64_big_sur
   Homebrew Prefix: /opt/homebrew

📁 Creating bottle directory structure...
📦 Copying binary...
   ✅ Binary copied
📦 Copying headers...
   ✅ Copied 15 headers
📦 Copying Skia libraries...
   ✅ Copied 8 Skia libraries
📦 Creating tarball...
   ✅ Tarball created: lotio-20251230-test.arm64_big_sur.bottle.tar.gz (15.2M)
   ✅ SHA256: abc123...

✅ Bottle Creation Test Passed!
```

---

## Utility Scripts

### `create_bottle.sh`
**Purpose:** Creates Homebrew bottle tarball with correct directory structure

**Usage:**
```bash
./scripts/create_bottle.sh <VERSION> [BOTTLE_ARCH] [HOMEBREW_PREFIX]
```

**When to use:**
- Creating Homebrew bottles for releases
- Testing bottle structure locally
- Called automatically by CI/CD workflows

**Parameters:**
- `VERSION`: Version string (e.g., "v1.2.3" or "1.2.3")
- `BOTTLE_ARCH`: Architecture (e.g., "arm64_big_sur") - auto-detected if not provided
- `HOMEBREW_PREFIX`: Homebrew prefix (e.g., "/opt/homebrew") - auto-detected if not provided

**Output:**
- Bottle tarball: `lotio-<VERSION>.<BOTTLE_ARCH>.bottle.tar.gz`
- SHA256 checksum (printed to stdout)
- Sets `BOTTLE_FILENAME` and `BOTTLE_SHA256` environment variables (if sourced)

**Used by:**
- `.github/workflows/release.yml` (automated)
- `test_bottle.sh` (for testing)

---

### `update_homebrew_formula.sh`
**Purpose:** Updates Homebrew formula after release

**Usage:**
```bash
./scripts/update_homebrew_formula.sh <version> <source_sha256> <bottle_sha256> <arch>
```

**When to use:**
- Automatically called by CI/CD after release
- Manual Homebrew formula updates

**Used by:**
- `.github/workflows/release.yml` (automated)

**Note:** Requires `HOMEBREW_TAP_TOKEN` environment variable

---

### `build_docs.py`
**Purpose:** Builds documentation for GitHub Pages from markdown files

**Usage:**
```bash
./scripts/build_docs.py
```

**When to use:**
- Building documentation site for GitHub Pages
- Converting markdown to HTML with navigation
- Updating documentation after changes

**Prerequisites:**
- Python 3
- Optional: `markdown` library (`pip install markdown`) for enhanced formatting

**Key Features:**
- Converts markdown files to HTML
- Adds sidebar navigation
- Generates index pages
- Supports code highlighting and tables

**Output:**
- HTML documentation in `docs/_site/` directory

---

### `render_entrypoint.sh`
**Purpose:** Docker entrypoint script for rendering Lottie animations to video

**Usage:**
```bash
# Used as Docker entrypoint in lotio-ffmpeg image
docker run -v $(pwd):/workspace matrunchyk/lotio-ffmpeg:latest \
  data.json - 30 --layer-overrides layer-overrides.json --output video.mov

# Direct command execution (bypasses entrypoint wrapper)
docker run matrunchyk/lotio-ffmpeg:latest ffmpeg -version
docker run matrunchyk/lotio-ffmpeg:latest lotio --version
```

**When to use:**
- Running lotio in Docker containers with video output
- Automatically handles frame rendering and video encoding
- Used in `Dockerfile.lotio-ffmpeg`

**Key Features:**
- **Smart command routing**: If first argument is a command (like `ffmpeg` or `lotio`), executes it directly
- **Automatic video encoding**: Otherwise, treats arguments as lotio commands and wraps with ffmpeg pipeline
- Renders Lottie animation frames using lotio
- Encodes frames to video using ffmpeg (ProRes 4444 with alpha channel)
- Passes through lotio arguments
- Handles text padding and measurement mode options

**Parameters:**
- `--output` / `-o`: Output video file (default: output.mov)
- `--text-padding` / `-p`: Text padding factor (default: 0.97)
- `--text-measurement-mode` / `-m`: Text measurement mode (default: accurate)
- All other arguments passed to lotio

**How it works:**
1. If first argument is a command found in PATH (e.g., `ffmpeg`, `lotio`), executes it directly
2. Otherwise, treats arguments as lotio commands and automatically:
   - Adds `--stream` if not present (required for piping to ffmpeg)
   - Pipes lotio PNG output to ffmpeg
   - Encodes to ProRes 4444 MOV with alpha channel support

---

## Typical Workflows

### First-time Setup
```bash
# 1. Install dependencies (binary build needs fewer deps)
brew install fontconfig freetype harfbuzz icu4c libpng ninja python@3.11

# 2. Build lotio (build_binary.sh handles Skia setup automatically)
./scripts/build_binary.sh
```

### Development Workflow
```bash
# Make code changes, then:
./scripts/build_binary.sh  # Builds lotio binary + liblotio.a library
```

### WASM Development
```bash
# 1. Install Emscripten
brew install emscripten

# 2. Build lotio for WASM (unified script handles Skia build automatically)
./scripts/build_wasm.sh
```

### Testing Before Push
```bash
# Test native build (default)
./scripts/test.sh

# Test Linux build
./scripts/test.sh linux

# Test WASM build
./scripts/test.sh wasm

# Test bottle creation
./scripts/test.sh bottle
```

---

## Script Dependencies

```
build_binary.sh ⭐ (RECOMMENDED)
  └─> _build_deps.sh (--target=binary)
      └─> Handles: Skia setup + build + lotio compilation + liblotio.a

build_wasm.sh
  └─> _build_deps.sh (--target=wasm)
      └─> Handles: Skia WASM build + lotio WASM compilation (requires Emscripten)

build_skia_docker_multi.sh
  └─> Dockerfile.skia
      └─> build_binary.sh (for building Skia in Docker)

test.sh
  ├─> native: build_binary.sh
  ├─> linux: build_binary.sh (in Docker)
  ├─> wasm: build_wasm.sh
  └─> bottle: test_bottle.sh

test_bottle.sh
  └─> create_bottle.sh

create_bottle.sh
  └─> Requires: lotio binary, liblotio.a, headers (from build_binary.sh)
```

---

## Notes

- All scripts use `set -e` to exit on errors
- Scripts automatically detect their location and set `PROJECT_ROOT`
- Most scripts provide helpful error messages and usage hints
- Scripts are designed to be idempotent (safe to run multiple times)

