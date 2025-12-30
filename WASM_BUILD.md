# WebAssembly Build Guide

This document explains how to build and test the WebAssembly version of lotio.

## Prerequisites

### Option 1: Homebrew (Recommended for macOS)

```bash
brew install emscripten
emcc --version
```

This is the simplest method and works out of the box.

### Option 2: Emscripten SDK (Official method)

If you prefer the official SDK or need a specific version:

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
emcc --version
```

**Note:** The build scripts automatically detect both installation methods.

## Building Locally

### Step 1: Build Skia for WebAssembly

```bash
./build_skia_wasm.sh
```

This will:
- Configure Skia for WebAssembly target
- Build Skia libraries (takes 10-30 minutes)
- Output libraries to `third_party/skia/skia/out/Wasm/`

### Step 2: Build lotio WASM

```bash
./build_wasm.sh
```

This will:
- Compile lotio source files with Emscripten
- Link against Skia WASM libraries
- Generate `lotio.wasm` and `lotio.js`

### Quick Test Script

```bash
./test_wasm_local.sh
```

This script automates the build process and checks for prerequisites.

## Testing in Browser

1. **Start a local HTTP server:**
   ```bash
   cd wasm
   python3 -m http.server 8000
   ```

2. **Open test page:**
   - Navigate to `http://localhost:8000/test.html`
   - The page will attempt to load `../samples/sample1/data.json`
   - Or modify `test.html` to load your own Lottie JSON file

3. **Test features:**
   - Use the slider to scrub through animation
   - Click "Play" to animate
   - Click "Render PNG" to download a frame as PNG

## Generated Files

After building, you'll have:

- `lotio.wasm` - WebAssembly binary (main module)
- `lotio.js` - Emscripten loader/glue code
- `wasm/lotio_wasm.js` - JavaScript wrapper API

## API Usage

```javascript
import { 
    initLotio, 
    createAnimation, 
    renderFrameToCanvas,
    renderFrameRGBA,
    renderFramePNG,
    renderFrameAsImageData,
    cleanup 
} from './lotio_wasm.js';

// Initialize
await initLotio('./lotio.wasm');

// Load animation
const jsonData = await fetch('./animation.json').then(r => r.json());
const info = createAnimation(jsonData);
// info: { width, height, duration, fps }

// Render to canvas
renderFrameToCanvas(canvas, 0.5); // Render at 0.5 seconds

// Get raw RGBA pixels
const { rgba, width, height } = renderFrameRGBA(0.5);

// Get PNG bytes
const pngBytes = renderFramePNG(0.5);

// Get ImageData object
const imageData = renderFrameAsImageData(0.5);

// Cleanup
cleanup();
```

## Troubleshooting

### "Emscripten not found"
- Make sure you've activated Emscripten: `source emsdk/emsdk_env.sh`
- Check `$EMSDK` environment variable is set

### "Skia not built for WebAssembly"
- Run `./build_skia_wasm.sh` first
- This takes a while (10-30 minutes)

### Build errors
- Check that all source files exist in `src/wasm/`
- Verify Skia WASM libraries are in `third_party/skia/skia/out/Wasm/`
- Check Emscripten version compatibility

### Runtime errors in browser
- Check browser console for errors
- Verify WASM files are served with correct MIME types
- Ensure CORS is enabled if loading from different origin

## CI/CD Integration

The WASM build is automatically included in GitHub Actions releases:

- Job: `build-wasm` runs on `ubuntu-latest`
- Builds Skia and lotio for WASM
- Packages files into `lotio-wasm-<version>.tar.gz`
- Uploads to GitHub Releases alongside other packages

## File Structure

```
.
├── build_skia_wasm.sh      # Build Skia for WASM
├── build_wasm.sh           # Build lotio WASM
├── test_wasm_local.sh      # Local test script
├── wasm/
│   ├── lotio_wasm.js       # JavaScript wrapper
│   └── test.html           # Test page
└── src/wasm/
    └── lotio_wasm.cpp      # WASM source code
```

