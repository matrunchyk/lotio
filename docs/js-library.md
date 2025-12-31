# JavaScript Library

The Lotio JavaScript library provides a WebAssembly-based interface for rendering Lottie animations in the browser.

## Installation

### npm / GitHub Packages

```bash
npm install @matrunchyk/lotio
```

**Note:** To use GitHub Packages, configure npm:

```bash
echo "@matrunchyk:registry=https://npm.pkg.github.com" >> ~/.npmrc
```

Then authenticate with a GitHub Personal Access Token with `read:packages` permission.

## Quick Start

```javascript
import Lotio, { FrameType, State } from '@matrunchyk/lotio';

// Load font
const fontResponse = await fetch('./fonts/OpenSans-Bold.ttf');
const fontData = new Uint8Array(await fontResponse.arrayBuffer());

// Load animation
const animationResponse = await fetch('./animation.json');
const animationData = await animationResponse.json();

// Create animation
const animation = new Lotio({
  fonts: [{ name: 'OpenSans-Bold', data: fontData }],
  fps: 30,
  animation: animationData,
  type: FrameType.PNG,
  wasmPath: './lotio.wasm'
});

// Event handlers
animation
  .on('loaded', (anim) => anim.start())
  .on('frame', () => {
    animation.renderToCanvas(canvas);
  });
```

## Interactive Demo

<div id="demo-container" style="margin: 20px 0;">
  <div id="status" class="status loading" style="padding: 10px; border-radius: 4px; margin-bottom: 15px; font-size: 14px; background: #4a4a4a; color: #fff;">Loading WASM module...</div>
  
  <div style="display: flex; gap: 10px; flex-wrap: wrap; margin-bottom: 20px;">
    <div style="display: flex; align-items: center; gap: 10px;">
      <label style="color: #ccc; font-size: 14px;">Sample:</label>
      <select id="sampleSelect" style="background: #3a3a3a; color: #fff; border: 1px solid #555; padding: 8px; border-radius: 4px; font-size: 14px;">
        <option value="sample1">Sample 1</option>
        <option value="sample2">Sample 2</option>
      </select>
    </div>
    
    <button id="playBtn" disabled style="background: #4a9eff; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; font-size: 14px;">Play</button>
    <button id="pauseBtn" disabled style="background: #4a9eff; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; font-size: 14px;">Pause</button>
    <button id="stopBtn" disabled style="background: #4a9eff; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; font-size: 14px;">Stop</button>
    
    <div style="display: flex; align-items: center; gap: 10px;">
      <label style="color: #ccc; font-size: 14px;">Frame: <span id="frameValue" style="display: inline-block; min-width: 60px; text-align: right; font-variant-numeric: tabular-nums;">0</span> / <span id="totalFrames" style="display: inline-block; min-width: 40px; text-align: left; font-variant-numeric: tabular-nums;">0</span> (<span id="timeValue" style="display: inline-block; min-width: 50px; text-align: right; font-variant-numeric: tabular-nums;">0.0</span>s)</label>
      <input type="range" id="frameSlider" min="0" max="1" step="1" value="0" disabled style="width: 200px;">
    </div>
    
    <div style="display: flex; align-items: center; gap: 10px;">
      <label style="color: #ccc; font-size: 14px;">FPS: <span id="fpsValue">30</span></label>
      <input type="range" id="fpsSlider" min="1" max="120" step="1" value="30" style="width: 150px;">
    </div>
    
    <div style="display: flex; align-items: center; gap: 10px;">
      <label style="color: #ccc; font-size: 14px;">BG Color:</label>
      <input type="color" id="bgColorPicker" value="#2a2a2a" style="width: 50px; height: 30px; border: none; border-radius: 4px; cursor: pointer;">
    </div>
  </div>
  
  <div style="background: #1a1a1a; border: 1px solid #444; border-radius: 4px; padding: 20px; display: inline-block; margin-top: 10px;">
    <canvas id="demo-canvas" style="display: block; max-width: 100%; height: auto; transform: scale(0.5); transform-origin: top left;"></canvas>
  </div>
  
  <div id="demo-info" style="background: #333; padding: 15px; border-radius: 4px; margin-top: 15px; font-size: 13px; line-height: 1.6; display: none;"></div>
</div>

<script type="module">
  (async () => {
    let Lotio, FrameType, State;
    
    const statusDiv = document.getElementById('status');
    
    try {
      statusDiv.className = 'status loading';
      statusDiv.textContent = 'Loading Lotio module...';
      
      console.log('Attempting to import Lotio from ./browser/index.js');
      const module = await import('./browser/index.js');
      
      if (!module || !module.default) {
        throw new Error('Module loaded but default export not found');
      }
      
      Lotio = module.default;
      FrameType = module.FrameType;
      State = module.State;
      
      console.log('Lotio module loaded successfully', { Lotio, FrameType, State });
      statusDiv.className = 'status loading';
      statusDiv.textContent = 'Lotio module loaded. Initializing demo...';
    } catch (error) {
      if (statusDiv) {
        statusDiv.className = 'status error';
        statusDiv.textContent = `Failed to load Lotio module: ${error.message}. Check browser console (F12) for details.`;
      }
      console.error('Failed to import Lotio module:', error);
      console.error('Error details:', {
        message: error.message,
        stack: error.stack,
        name: error.name
      });
      // Try to provide helpful debugging info
      console.log('Current page URL:', window.location.href);
      console.log('Expected browser path:', new URL('./browser/index.js', window.location.href).href);
      return;
    }
  
    let animation = null;
    let isPlaying = false;
    let bgColor = '#2a2a2a';
    
    const infoDiv = document.getElementById('demo-info');
    const canvas = document.getElementById('demo-canvas');
    const sampleSelect = document.getElementById('sampleSelect');
    const playBtn = document.getElementById('playBtn');
    const pauseBtn = document.getElementById('pauseBtn');
    const stopBtn = document.getElementById('stopBtn');
    const frameSlider = document.getElementById('frameSlider');
    const frameValue = document.getElementById('frameValue');
    const totalFrames = document.getElementById('totalFrames');
    const timeValue = document.getElementById('timeValue');
    const fpsSlider = document.getElementById('fpsSlider');
    const fpsValue = document.getElementById('fpsValue');
    const bgColorPicker = document.getElementById('bgColorPicker');
  
  async function convertImagesToDataURIs(jsonData, sampleName) {
    if (!jsonData.assets || !Array.isArray(jsonData.assets)) {
      return jsonData;
    }
    
    const modified = JSON.parse(JSON.stringify(jsonData));
    
    for (const asset of modified.assets) {
      if (asset.e === 1) continue;
      
      if (asset.p && asset.u !== undefined) {
        const imagePath = asset.u + asset.p;
        const imageUrl = `./samples/${sampleName}/${imagePath}`;
        
        try {
          const imageResponse = await fetch(imageUrl);
          if (imageResponse.ok) {
            const imageBlob = await imageResponse.blob();
            const reader = new FileReader();
            const dataUri = await new Promise((resolve, reject) => {
              reader.onload = () => resolve(reader.result);
              reader.onerror = reject;
              reader.readAsDataURL(imageBlob);
            });
            
            asset.u = '';
            asset.p = dataUri;
            asset.e = 1;
          }
        } catch (error) {
          console.warn(`Error loading image ${imagePath}:`, error);
        }
      }
    }
    
    return modified;
  }
  
  async function loadSample(sampleName) {
    try {
      statusDiv.className = 'status loading';
      statusDiv.textContent = `Loading ${sampleName}...`;
      
      if (animation) {
        animation.destroy();
      }
      
      const fontResponse = await fetch('./fonts/OpenSans/OpenSans-Bold.ttf');
      const fontData = new Uint8Array(await fontResponse.arrayBuffer());
      
      const animResponse = await fetch(`./samples/${sampleName}/data.json`);
      const animData = await animResponse.json();
      
      const processedAnim = await convertImagesToDataURIs(animData, sampleName);
      
      let textConfig = null;
      try {
        const textConfigResponse = await fetch(`./samples/${sampleName}/text-config.json`);
        if (textConfigResponse.ok) {
          textConfig = await textConfigResponse.json();
        } else if (textConfigResponse.status === 404) {
          // Text config is optional, 404 is expected if it doesn't exist
          // Silently continue without text config
        }
      } catch (e) {
        // Text config is optional, ignore errors
      }
      
      animation = new Lotio({
        fonts: [{ name: 'OpenSans-Bold', data: fontData }],
        fps: parseFloat(fpsSlider.value),
        animation: processedAnim,
        textConfig: textConfig,
        type: FrameType.PNG,
        wasmPath: './browser/lotio.wasm'
      });
      
      animation
        .on('error', (error, anim) => {
          statusDiv.className = 'status error';
          statusDiv.textContent = `Error: ${error.message}`;
          console.error('Animation error:', error);
        })
        .on('loaded', (anim) => {
          statusDiv.className = 'status success';
          statusDiv.textContent = `${sampleName} loaded successfully!`;
          
          const info = anim.getAnimationInfo();
          infoDiv.style.display = 'block';
          infoDiv.innerHTML = `
            <strong>Animation Info:</strong><br>
            Sample: ${sampleName}<br>
            Size: ${info.width} x ${info.height}<br>
            Duration: ${info.duration.toFixed(2)}s<br>
            FPS: ${info.fps}<br>
            State: ${anim.getState()}
          `;
          
          // Calculate total frames
          const animFPS = info.fps || 30;
          const totalFrameCount = Math.floor(info.duration * animFPS);
          
          // Set up frame slider
          frameSlider.min = 0;
          frameSlider.max = totalFrameCount;
          frameSlider.step = 1;
          frameSlider.value = 0;
          totalFrames.textContent = totalFrameCount;
          
          playBtn.disabled = false;
          pauseBtn.disabled = false;
          stopBtn.disabled = false;
          frameSlider.disabled = false;
          
          updateFrame();
        })
        .on('start', () => {
          isPlaying = true;
          playBtn.disabled = true;
          pauseBtn.disabled = false;
        })
        .on('pause', () => {
          isPlaying = false;
          playBtn.disabled = false;
          pauseBtn.disabled = true;
        })
        .on('stop', () => {
          isPlaying = false;
          playBtn.disabled = false;
          pauseBtn.disabled = true;
        })
        .on('end', () => {
          isPlaying = false;
          playBtn.disabled = false;
          pauseBtn.disabled = true;
        })
        .on('frame', () => {
          if (animation) {
            animation.renderToCanvas(canvas, bgColor);
            const info = animation.getAnimationInfo();
            const frame = animation.getCurrentFrame();
            if (frame) {
              frameSlider.value = frame.number;
              frameValue.textContent = frame.number;
              timeValue.textContent = frame.time.toFixed(2);
            }
          }
        });
      
    } catch (error) {
      statusDiv.className = 'status error';
      statusDiv.textContent = `Error loading ${sampleName}: ${error.message}`;
      console.error(error);
    }
  }
  
  function updateFrame() {
    if (animation && !isPlaying) {
      const frameNumber = parseInt(frameSlider.value);
      animation.seek(frameNumber);
      animation.renderToCanvas(canvas, bgColor);
      
      const frame = animation.getCurrentFrame();
      if (frame) {
        frameValue.textContent = frame.number;
        timeValue.textContent = frame.time.toFixed(2);
      }
    }
  }
  
  sampleSelect.addEventListener('change', () => {
    loadSample(sampleSelect.value);
  });
  
  playBtn.addEventListener('click', () => {
    if (animation) animation.start();
  });
  
  pauseBtn.addEventListener('click', () => {
    if (animation) animation.pause();
  });
  
  stopBtn.addEventListener('click', () => {
    if (animation) animation.stop();
  });
  
  frameSlider.addEventListener('input', () => {
    updateFrame();
  });
  
  fpsSlider.addEventListener('input', () => {
    fpsValue.textContent = fpsSlider.value;
    if (animation) {
      animation.setFps(parseFloat(fpsSlider.value));
    }
  });
  
    bgColorPicker.addEventListener('input', () => {
      bgColor = bgColorPicker.value;
      updateFrame();
    });
    
    loadSample('sample1');
  })().catch(error => {
    const statusDiv = document.getElementById('status');
    if (statusDiv) {
      statusDiv.className = 'status error';
      statusDiv.textContent = `Initialization error: ${error.message}`;
    }
    console.error('Demo initialization error:', error);
  });
</script>

## API Reference

### Constructor

```javascript
new Lotio(options)
```

**Options:**
- `fonts` (Array): Font files to load. Each font should have `{ name: string, data: Uint8Array }`
- `fps` (number): Frames per second (default: 30)
- `animation` (Object|string): Lottie animation JSON (object or stringified)
- `textConfig` (Object|string, optional): Text configuration JSON
- `type` (string): Output type: `'png'` or `'webp'` (default: `'png'`)
- `wasmPath` (string): Path to `lotio.wasm` file (default: `'./lotio.wasm'`)

### Methods

#### Control Methods

- `start()` - Start animation playback
- `pause()` - Pause animation
- `stop()` - Stop animation and reset to beginning
- `seek(frameOrTime)` - Seek to frame number or time (seconds)
- `renderToCanvas(canvas, bgColor)` - Render current frame to canvas

#### Setters (Fluent Interface)

- `setFps(fps)` - Set frames per second
- `setAnimation(animation)` - Set animation data
- `setTextConfig(textConfig)` - Set text configuration
- `setType(type)` - Set output type (`FrameType.PNG` or `FrameType.WEBP`)
- `setFonts(fonts)` - Set fonts array

#### Getters

- `getFps()` - Get current FPS
- `getAnimation()` - Get animation data
- `getTextConfig()` - Get text configuration
- `getType()` - Get output type
- `getState()` - Get current state (`'stopped'`, `'paused'`, `'loaded'`, `'error'`, `'playing'`)
- `getCurrentFrame()` - Get current frame data
- `getAnimationInfo()` - Get animation metadata (width, height, duration, fps)

#### Lifecycle

- `destroy()` - Cleanup resources

### Events

All event handlers support fluent interface chaining:

```javascript
animation
  .on('error', (error, animation) => { /* ... */ })
  .on('loaded', (animation) => { /* ... */ })
  .on('start', (animation) => { /* ... */ })
  .on('pause', (animation) => { /* ... */ })
  .on('stop', (animation) => { /* ... */ })
  .on('end', (animation) => { /* ... */ })
  .on('seek', (animation) => { /* ... */ })
  .on('frame', (frameNumber, time, animation) => { /* ... */ })
  .on('statechange', (newState, oldState, animation) => { /* ... */ });
```

**Events:**
- `error` - Emitted when an error occurs
- `loaded` - Emitted when animation is loaded
- `start` - Emitted when animation starts
- `pause` - Emitted when animation is paused
- `stop` - Emitted when animation stops
- `end` - Emitted when animation ends
- `seek` - Emitted when seeking to a different frame/time
- `frame` - Emitted on each frame during playback
- `statechange` - Emitted when state changes
- `destroy` - Emitted when animation is destroyed

### Constants

- `FrameType.PNG` - PNG output type
- `FrameType.WEBP` - WebP output type
- `State.STOPPED` - Animation is stopped
- `State.PAUSED` - Animation is paused
- `State.LOADED` - Animation is loaded
- `State.ERROR` - Error state
- `State.PLAYING` - Animation is playing

## Examples

### Basic Usage

```javascript
import Lotio from '@matrunchyk/lotio';

const animation = new Lotio({
  animation: animationData,
  wasmPath: './lotio.wasm'
});

animation.on('loaded', () => {
  animation.start();
});

animation.on('frame', () => {
  animation.renderToCanvas(canvas);
});
```

### With Fonts and Text Config

```javascript
const fontData = new Uint8Array(await fontResponse.arrayBuffer());

const animation = new Lotio({
  fonts: [{ name: 'OpenSans-Bold', data: fontData }],
  animation: animationData,
  textConfig: {
    textLayers: {
      "Patient_Name": {
        minSize: 20,
        maxSize: 100,
        textBoxWidth: 500
      }
    },
    textValues: {
      "Patient_Name": "John Doe"
    }
  }
});
```

### Control Playback

```javascript
// Start
animation.start();

// Pause
animation.pause();

// Stop and reset
animation.stop();

// Seek to frame 10
animation.seek(10);

// Seek to time 2.5 seconds
animation.seek(2.5);

// Change FPS
animation.setFps(60);
```

### Get Frame Data

```javascript
const frame = animation.getCurrentFrame();

console.log(frame.number);  // Frame number
console.log(frame.time);    // Time in seconds
console.log(frame.width);   // Frame width
console.log(frame.height);  // Frame height
console.log(frame.data);    // RGBA pixel data (Uint8Array)

// Render to canvas
frame.renderToCanvas(canvas, '#ffffff');
```

## Browser Support

- Chrome/Edge 57+
- Firefox 52+
- Safari 11+
- Opera 44+

Requires WebAssembly support.

## See Also

- [Overview](./overview.html) - General information about Lotio
- [CLI](./cli.html) - Command-line usage
- [C++ Library](./cpp-library.html) - C++ library usage

