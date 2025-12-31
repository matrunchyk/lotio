# Overview

**Lotio** is a high-performance Lottie animation frame renderer using Skia. It renders Lottie animations to PNG/WebP frames for video encoding.

## What is Lotio?

Lotio is a powerful tool for converting Lottie animations into individual frames that can be used for video encoding, image sequences, or programmatic manipulation. It's built on top of Google's Skia graphics library, providing:

- **High Performance**: Multi-threaded rendering for fast frame generation
- **Multiple Formats**: Output to PNG, WebP, or stream directly to stdout
- **Text Customization**: Dynamic text replacement and auto-fit sizing
- **Cross-Platform**: Native binaries for macOS and Linux, WebAssembly for browsers
- **Multiple Interfaces**: Command-line tool, JavaScript library, and C++ library

## What Does It Do?

Lotio takes Lottie animation JSON files and renders them frame-by-frame into image files or streams. Key features include:

### Core Functionality

1. **Frame Rendering**: Converts Lottie animations into individual frames at any FPS
2. **Format Support**: Outputs PNG or WebP images
3. **Text Processing**: 
   - Dynamic text value replacement
   - Auto-fit font sizing to fit text boxes
   - Custom font loading
4. **Streaming**: Can stream frames directly to stdout for piping to video encoders like ffmpeg

### Use Cases

- **Video Encoding**: Generate frame sequences for video creation
- **Image Sequences**: Create sprite sheets or animation frames
- **Web Applications**: Use the WebAssembly build for browser-based rendering
- **Automation**: Integrate into build pipelines or automated workflows
- **Custom Rendering**: Programmatically control animation playback and rendering

## Architecture

Lotio is built with a modular architecture:

```
lotio/
|
├── src/
|
│   ├── core/          # Core functionality (animation setup, rendering, encoding)
|   |
│   ├── text/          # Text processing (configuration, font handling, sizing)
|   |
│   ├── utils/         # Utilities (logging, string utils, crash handling)
|   |
│   └── wasm/          # WebAssembly-specific code
|
├── browser/            # WebAssembly build output
|
└── third_party/skia/  # Skia graphics library
```

## Components

### 1. Command-Line Interface (CLI)
A native binary (`lotio`) for rendering animations from the command line. See the [CLI Documentation](./cli.html) for details.

### 2. JavaScript Library
A WebAssembly-based library for browser use. See the [JS Library Documentation](./js-library.html) for details.

### 3. C++ Library
Headers and static libraries for programmatic use. See the [C++ Library Documentation](./cpp-library.html) for details.

## Installation

### Homebrew (macOS - Recommended)
```bash
brew tap matrunchyk/lotio
brew install lotio
```

### From Source
See the [README](../README.md) for build instructions.

### npm (JavaScript/WebAssembly)
```bash
npm install @matrunchyk/lotio
```

## Quick Start

### Command Line
```bash
lotio --png animation.json frames/ 30
```

### JavaScript
```javascript
import Lotio from '@matrunchyk/lotio';

const animation = new Lotio({
  animation: animationData,
  wasmPath: './lotio.wasm'
});
```

### C++
```cpp
#include <lotio/core/animation_setup.h>

AnimationSetupResult result = setupAndCreateAnimation("input.json", "");
```

## Performance

Lotio is optimized for performance:

- **Multi-threaded rendering**: Utilizes multiple CPU cores
- **Efficient memory usage**: Streams frames to reduce memory footprint
- **Skia acceleration**: Leverages Skia's optimized rendering pipeline
- **WebAssembly optimization**: Optimized WASM build for browser performance

## License

See individual component licenses:
- Skia: `third_party/skia/skia/LICENSE`
- Lotio: See repository LICENSE file

