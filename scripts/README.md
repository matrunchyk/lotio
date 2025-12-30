# Scripts Directory

This directory contains build, test, and utility scripts for the `lotio` project.

## Core Build Scripts

### `install_skia.sh`
**Purpose:** Builds Skia library from source (first-time setup)

**Usage:**
```bash
./scripts/install_skia.sh
```

**When to use:**
- First time building the project
- After updating Skia dependencies
- When Skia build is missing or corrupted

**Dependencies:** Requires `ninja`, `python3`, and system libraries (fontconfig, freetype, harfbuzz, icu4c, libpng, jpeg-turbo, libwebp)

**Used by:**
- `build_local.sh` (automatically calls if Skia not built)
- `Makefile` (automatically calls if Skia not built)
- CI/CD workflows
- Docker builds

---

### `build_local.sh`
**Purpose:** Builds the native `lotio` binary for your platform

**Usage:**
```bash
./scripts/build_local.sh
```

**When to use:**
- Building the project locally
- After modifying source code
- Before running tests

**Prerequisites:** Skia must be built (runs `install_skia.sh` automatically if needed)

**Output:** Creates `lotio` binary in project root

**Used by:**
- `Makefile` (alternative to `make`)
- CI/CD workflows
- Test scripts

---

### `build_skia_wasm.sh`
**Purpose:** Builds Skia library for WebAssembly target

**Usage:**
```bash
./scripts/build_skia_wasm.sh
```

**When to use:**
- Before building WASM version of lotio
- First time setting up WASM build
- After updating Skia dependencies for WASM

**Dependencies:** Requires Emscripten (`emcc`)

**Output:** Skia libraries in `third_party/skia/skia/out/Wasm/`

**Used by:**
- `build_wasm.sh` (automatically calls if Skia WASM not built)

---

### `build_wasm.sh`
**Purpose:** Builds the WebAssembly version of lotio for browser use

**Usage:**
```bash
./scripts/build_wasm.sh
```

**When to use:**
- Building WASM version for browser library
- Before publishing to npm/GitHub Packages
- Testing browser functionality

**Prerequisites:** 
- Skia WASM must be built (runs `build_skia_wasm.sh` automatically if needed)
- Emscripten must be installed

**Output:** 
- `browser/lotio.wasm`
- `browser/lotio.js`
- `browser/wasm.js`

**Used by:**
- CI/CD workflows (for npm package publishing)
- `test_wasm_local.sh`

---

## Dependency Management Scripts

### `sync_skia_deps.sh`
**Purpose:** Syncs Skia's third-party dependencies using `python3 tools/git-sync-deps`

**Usage:**
```bash
./scripts/sync_skia_deps.sh [skia_directory]
```

**When to use:**
- After cloning Skia repository
- When Skia dependencies are out of sync
- Before building Skia

**Used by:**
- CI/CD workflows
- `install_skia.sh` (indirectly)

---

### `fetch_skia_deps.sh`
**Purpose:** Fetches Skia's external dependencies (used in Docker builds)

**Usage:**
```bash
./scripts/fetch_skia_deps.sh
```

**When to use:**
- Docker container builds
- Manual dependency fetching

**Used by:**
- Dockerfile

---

## Testing Scripts

### `test_local.sh`
**Purpose:** Quick local test script for macOS builds

**Usage:**
```bash
./scripts/test_local.sh
```

**When to use:**
- Testing macOS build before pushing
- Verifying dependencies are installed
- Quick build validation

**What it does:**
- Checks dependencies
- Builds Skia if needed
- Builds lotio
- Tests binary

---

### `test_wasm_local.sh`
**Purpose:** Tests WASM build locally

**Usage:**
```bash
./scripts/test_wasm_local.sh
```

**When to use:**
- Testing WASM build before pushing
- Verifying Emscripten setup
- Validating browser library build

**What it does:**
- Checks Emscripten installation
- Builds Skia for WASM if needed
- Builds lotio WASM
- Lists generated files

---

### `test_linux_build.sh`
**Purpose:** Tests Linux build in a Docker container

**Usage:**
```bash
./scripts/test_linux_build.sh
```

**When to use:**
- Testing Linux compatibility
- Validating cross-platform builds
- Before releasing Linux packages

**What it does:**
- Runs Docker container
- Installs dependencies
- Builds Skia
- Builds lotio
- Tests binary

---

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
- Binary must be built: `./scripts/build_local.sh`
- Skia must be built: `./scripts/install_skia.sh`

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
   Version: v20251230-test
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

### `check_build.sh`
**Purpose:** Monitors Skia build progress and status

**Usage:**
```bash
./scripts/check_build.sh
```

**When to use:**
- While Skia is building (takes 10-20 minutes)
- Checking if build is still running
- Monitoring build progress

**What it shows:**
- Build status (running/complete)
- Progress percentage
- Last build activity
- Library files status

**Tip:** Use `watch -n 2 './scripts/check_build.sh'` for real-time monitoring

---

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
# 1. Install dependencies
brew install fontconfig freetype harfbuzz icu4c libpng jpeg-turbo libwebp ninja python@3.11

# 2. Build Skia (takes 10-20 minutes)
./scripts/install_skia.sh

# 3. Build lotio
./scripts/build_local.sh
```

### Development Workflow
```bash
# Make code changes, then:
./scripts/build_local.sh

# Or use Makefile:
make
```

### WASM Development
```bash
# 1. Install Emscripten
brew install emscripten

# 2. Build Skia for WASM
./scripts/build_skia_wasm.sh

# 3. Build lotio WASM
./scripts/build_wasm.sh
```

### Testing Before Push
```bash
# Test native build
./scripts/test_local.sh

# Test WASM build
./scripts/test_wasm_local.sh
```

---

## Script Dependencies

```
install_skia.sh
  └─> sync_skia_deps.sh (via python3 tools/git-sync-deps)

build_local.sh
  └─> install_skia.sh (if Skia not built)

build_skia_wasm.sh
  └─> (requires Emscripten)

build_wasm.sh
  └─> build_skia_wasm.sh (if Skia WASM not built)

test_local.sh
  ├─> install_skia.sh (if needed)
  └─> build_local.sh

test_wasm_local.sh
  ├─> build_skia_wasm.sh (if needed)
  └─> build_wasm.sh
```

---

## Notes

- All scripts use `set -e` to exit on errors
- Scripts automatically detect their location and set `PROJECT_ROOT`
- Most scripts provide helpful error messages and usage hints
- Scripts are designed to be idempotent (safe to run multiple times)

