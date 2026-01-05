# Scripts Directory

This directory contains build, test, and utility scripts for the `lotio` project.

## Core Build Scripts

### `build_minimal.sh` ⭐ **RECOMMENDED**
**Purpose:** Builds lotio with minimal Skia dependencies (zero bundled dependencies)

**Usage:**
```bash
./scripts/build_minimal.sh
```

**When to use:**
- **Primary build method** - recommended for all local development
- Building with minimal dependencies for faster builds
- Reducing build size and complexity
- CI/CD optimization

**Key Features:**
- Zero bundled dependencies (all use system/Homebrew libraries)
- Faster builds (no WebP/JPEG codecs, no GPU backends)
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
**Purpose:** Builds lotio with minimal Skia dependencies (10 essential deps instead of 40+)

**Usage:**
```bash
./scripts/build_minimal.sh
```

**When to use:**
- Building with minimal dependencies for faster builds
- Reducing build size and complexity
- Understanding which dependencies are actually required
- CI/CD optimization


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
- Skia structure (run `build_minimal.sh` first to set up Skia)
- Emscripten must be installed (`brew install emscripten` or EMSDK)

**Output:** 
- `browser/lotio.wasm`
- `browser/lotio.js`
- `browser/wasm.js`

**Used by:**
- CI/CD workflows (for npm package publishing)
- `test.sh` (unified test script)

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
- Binary and library must be built: `./scripts/build_minimal.sh`

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

### `debug_skia_gn.sh`
**Purpose:** Debug script to see GN configuration warnings

**Usage:**
```bash
./scripts/debug_skia_gn.sh
```

**When to use:**
- When Skia build fails with GN warnings
- Debugging build configuration issues
- Understanding GN build system

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

## Typical Workflows

### First-time Setup
```bash
# 1. Install dependencies (minimal build needs fewer deps)
brew install fontconfig freetype harfbuzz icu4c libpng ninja python@3.11

# 2. Build lotio (build_minimal.sh handles Skia setup automatically)
./scripts/build_minimal.sh
```

### Development Workflow
```bash
# Make code changes, then:
./scripts/build_minimal.sh  # Builds lotio binary + liblotio.a library
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
build_minimal.sh ⭐ (RECOMMENDED)
  └─> Handles everything: Skia setup + build + lotio compilation + liblotio.a

build_wasm.sh
  └─> Handles everything: Skia WASM build + lotio WASM compilation (requires Emscripten)

test.sh
  ├─> native: build_minimal.sh
  ├─> linux: build_minimal.sh (in Docker)
  ├─> wasm: build_wasm.sh (unified script)
  └─> bottle: test_bottle.sh

test_bottle.sh
  └─> create_bottle.sh
```

---

## Notes

- All scripts use `set -e` to exit on errors
- Scripts automatically detect their location and set `PROJECT_ROOT`
- Most scripts provide helpful error messages and usage hints
- Scripts are designed to be idempotent (safe to run multiple times)

