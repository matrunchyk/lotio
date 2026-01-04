# C++ Library

The Lotio C++ library provides headers and static libraries for programmatic use in C++ applications.

## Installation

### Homebrew (macOS)

```bash
brew tap matrunchyk/lotio
brew install lotio
```

This installs:
- Binary: `lotio`
- Headers: `/opt/homebrew/include/lotio/` (or `/usr/local/include/lotio/`)
- Libraries: `/opt/homebrew/lib/` (Skia static libraries)
- pkg-config: `lotio.pc`

### From Source

See the [README](../README.md) for build instructions. The developer package includes:
- Headers in `include/lotio/`
- Static libraries in `lib/`
- pkg-config file in `lib/pkgconfig/`

## Headers

Headers are organized by module:

```cpp
#include <lotio/core/animation_setup.h>  // Animation initialization
#include <lotio/core/renderer.h>          // Frame rendering
#include <lotio/core/frame_encoder.h>     // Frame encoding (PNG/WebP)
#include <lotio/text/text_processor.h>    // Text processing
#include <lotio/text/text_config.h>       // Text configuration
#include <lotio/text/font_utils.h>        // Text measurement modes
#include <lotio/utils/logging.h>          // Logging utilities
```

### Text Measurement Mode

```cpp
enum class TextMeasurementMode {
    FAST,          // Fastest, basic accuracy
    ACCURATE,       // Good balance, accounts for kerning and glyph metrics (default)
    PIXEL_PERFECT   // Most accurate, accounts for anti-aliasing and subpixel rendering
};
```

The `TextMeasurementMode` enum controls the accuracy vs performance trade-off for measuring text width:

- **`FAST`**: Fastest measurement using basic font metrics. Good for most cases but may underestimate width for some fonts.
- **`ACCURATE`** (default): Good balance of accuracy and performance. Uses SkTextBlob bounds which accounts for kerning and glyph metrics. Recommended for most use cases.
- **`PIXEL_PERFECT`**: Most accurate measurement by rendering text and scanning actual pixels. Accounts for anti-aliasing and subpixel rendering. Slower but most precise.

## Basic Usage

### Setting Up an Animation

```cpp
#include <lotio/core/animation_setup.h>
#include <lotio/core/renderer.h>
#include <lotio/core/frame_encoder.h>
#include <string>

int main() {
    std::string inputJson = "animation.json";
    std::string textConfigJson = "";  // Optional
    
    // Setup and create animation with default text padding and measurement mode
    AnimationSetupResult result = setupAndCreateAnimation(
        inputJson, 
        textConfigJson,
        0.97f,  // textPadding: 97% of width (3% padding)
        TextMeasurementMode::ACCURATE  // textMeasurementMode: good balance
    );
    
    if (!result.success) {
        std::cerr << "Failed to setup animation: " << result.errorMessage << std::endl;
        return 1;
    }
    
    // Use the animation...
    
    return 0;
}
```

### Rendering Frames

```cpp
#include <lotio/core/renderer.h>
#include <lotio/core/frame_encoder.h>

// Render a single frame
double time = 0.5;  // Time in seconds
int width = 800;
int height = 600;

// Render frame
std::vector<uint8_t> rgbaData = renderFrame(result.animation, time, width, height);

// Encode to PNG
std::string outputPath = "frame_0001.png";
bool success = encodeFrameToPNG(rgbaData, width, height, outputPath);
```

### Text Processing

```cpp
#include <lotio/text/text_processor.h>
#include <lotio/text/text_config.h>

// Load text configuration
TextConfig config = loadTextConfig("text_config.json");

// Process text layers in animation JSON
std::string modifiedJson = processTextLayers(
    originalJson,
    config
);
```

## Linking

### Using pkg-config (Recommended)

```bash
g++ $(pkg-config --cflags --libs lotio) your_app.cpp -o your_app
```

### Manual Linking

```bash
g++ -I/opt/homebrew/include \
    -L/opt/homebrew/lib \
    -llotio \
    -lskottie -lskia -lskparagraph -lsksg -lskshaper \
    -lskunicode_icu -lskunicode_core -lskresources -ljsonreader \
    your_app.cpp -o your_app
```

## API Reference

### Animation Setup

```cpp
struct AnimationSetupResult {
    bool success;
    std::string errorMessage;
    skottie::Animation* animation;  // Skottie animation pointer
};

AnimationSetupResult setupAndCreateAnimation(
    const std::string& inputJsonPath,
    const std::string& textConfigPath,
    float textPadding = 0.97f,
    TextMeasurementMode textMeasurementMode = TextMeasurementMode::ACCURATE
);
```

**Parameters:**
- `inputJsonPath`: Path to Lottie animation JSON file
- `textConfigPath`: Path to text configuration JSON file (empty string if not used)
- `textPadding`: Text padding factor (0.0-1.0, default: 0.97 = 3% padding). Controls how much of the target text box width is used for text sizing.
- `textMeasurementMode`: Text measurement accuracy mode (default: `ACCURATE`). See `TextMeasurementMode` enum above.
```

### Frame Rendering

```cpp
std::vector<uint8_t> renderFrame(
    skottie::Animation* animation,
    double time,
    int width,
    int height
);
```

Returns RGBA pixel data as a `std::vector<uint8_t>` (4 bytes per pixel).

### Frame Encoding

```cpp
bool encodeFrameToPNG(
    const std::vector<uint8_t>& rgbaData,
    int width,
    int height,
    const std::string& outputPath
);

bool encodeFrameToWebP(
    const std::vector<uint8_t>& rgbaData,
    int width,
    int height,
    const std::string& outputPath,
    int quality = 80
);
```

### Text Processing

```cpp
struct TextLayerConfig {
    int minSize;
    int maxSize;
    int textBoxWidth;
    int textBoxHeight;  // Optional
};

struct TextConfig {
    std::map<std::string, TextLayerConfig> textLayers;
    std::map<std::string, std::string> textValues;
};

TextConfig loadTextConfig(const std::string& configPath);
std::string processTextLayers(
    const std::string& animationJson,
    const TextConfig& config
);
```

## Complete Example

```cpp
#include <lotio/core/animation_setup.h>
#include <lotio/core/renderer.h>
#include <lotio/core/frame_encoder.h>
#include <lotio/text/text_processor.h>
#include <iostream>
#include <iomanip>
#include <sstream>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.json> <output_dir> [fps]" << std::endl;
        return 1;
    }
    
    std::string inputJson = argv[1];
    std::string outputDir = argv[2];
    int fps = (argc > 3) ? std::stoi(argv[3]) : 25;
    
    // Setup animation with custom text padding and measurement mode
    AnimationSetupResult result = setupAndCreateAnimation(
        inputJson, 
        "",
        0.95f,  // textPadding: 95% of width (5% padding)
        TextMeasurementMode::PIXEL_PERFECT  // Most accurate measurement
    );
    
    if (!result.success) {
        std::cerr << "Error: " << result.errorMessage << std::endl;
        return 1;
    }
    
    // Get animation info
    double duration = result.animation->duration();
    int width = static_cast<int>(result.animation->size().width());
    int height = static_cast<int>(result.animation->size().height());
    
    // Render frames
    int totalFrames = static_cast<int>(duration * fps);
    
    for (int frame = 0; frame < totalFrames; frame++) {
        double time = static_cast<double>(frame) / fps;
        
        // Render frame
        std::vector<uint8_t> rgbaData = renderFrame(result.animation, time, width, height);
        
        // Save as PNG
        std::ostringstream filename;
        filename << outputDir << "/frame_" 
                 << std::setfill('0') << std::setw(4) << frame << ".png";
        
        if (!encodeFrameToPNG(rgbaData, width, height, filename.str())) {
            std::cerr << "Failed to encode frame " << frame << std::endl;
            continue;
        }
        
        std::cout << "Rendered frame " << frame << "/" << totalFrames << std::endl;
    }
    
    std::cout << "Rendering complete!" << std::endl;
    return 0;
}
```

## Using Skia Directly

The lotio package includes Skia headers and libraries, so you can use Skia features directly:

```cpp
// Use Skia directly
#include <skia/core/SkCanvas.h>
#include <skia/core/SkSurface.h>
#include <skia/modules/skottie/include/Skottie.h>

// Use lotio
#include <lotio/core/animation_setup.h>

int main() {
    // Use Skia API directly
    SkImageInfo info = SkImageInfo::MakeN32(800, 600, kOpaque_SkAlphaType);
    auto surface = SkSurfaces::Raster(info);
    SkCanvas* canvas = surface->getCanvas();
    
    // Use lotio functions
    AnimationSetupResult result = setupAndCreateAnimation("input.json", "");
    
    // Render using Skia
    result.animation->render(canvas);
    
    return 0;
}
```

## Include Paths

The pkg-config file includes all necessary include paths:
- `-I${includedir}` - Lotio headers
- `-I${includedir}/skia` - Skia core headers
- `-I${includedir}/skia/gen` - Skia generated headers

## Libraries

The following Skia libraries are included:
- `libskottie.a` - Skottie animation library
- `libskia.a` - Skia core library
- `libskparagraph.a` - Text paragraph library
- `libsksg.a` - Scene graph library
- `libskshaper.a` - Text shaping library
- `libskunicode_icu.a` - Unicode support (ICU)
- `libskunicode_core.a` - Unicode core
- `libskresources.a` - Resource management
- `libjsonreader.a` - JSON parsing

## Multi-threaded Rendering

Lotio supports multi-threaded rendering for improved performance:

```cpp
#include <lotio/core/renderer.h>
#include <thread>
#include <vector>

void renderFrameRange(
    skottie::Animation* animation,
    int startFrame,
    int endFrame,
    int fps,
    int width,
    int height,
    const std::string& outputDir
) {
    for (int frame = startFrame; frame < endFrame; frame++) {
        double time = static_cast<double>(frame) / fps;
        std::vector<uint8_t> rgbaData = renderFrame(animation, time, width, height);
        
        // Save frame...
    }
}

// Render with multiple threads
int numThreads = std::thread::hardware_concurrency();
int framesPerThread = totalFrames / numThreads;

std::vector<std::thread> threads;
for (int i = 0; i < numThreads; i++) {
    int start = i * framesPerThread;
    int end = (i == numThreads - 1) ? totalFrames : (i + 1) * framesPerThread;
    
    threads.emplace_back(renderFrameRange, 
        result.animation, start, end, fps, width, height, outputDir);
}

for (auto& thread : threads) {
    thread.join();
}
```

## Troubleshooting

### Include Errors

- Verify headers are installed: `ls /opt/homebrew/include/lotio/`
- Check pkg-config: `pkg-config --cflags --libs lotio`
- Reload IDE after installation

### Linker Errors

- Verify libraries exist: `ls /opt/homebrew/lib/libskia.a`
- Check library paths in build command
- Ensure all Skia dependencies are linked

### Runtime Errors

- Check animation JSON is valid
- Verify font paths in Lottie JSON
- Check file permissions for output directory

## See Also

- [Overview](./overview.html) - General information about Lotio
- [CLI](./cli.html) - Command-line usage
- [JS Library](./js-library.html) - JavaScript/WebAssembly usage

