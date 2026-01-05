# Lotio Documentation

High-performance Lottie animation frame renderer for the browser using WebAssembly.

## Installation

### npm / GitHub Packages

```bash
npm install @matrunchyk/lotio
```

**Note:** To use GitHub Packages, configure npm:

```bash
echo "@matrunchyk:registry=https://npm.pkg.github.com" >> ~/.npmrc
```

The package is public and does not require authentication.

## Quick Start

```javascript
import Lotio, { State } from '@matrunchyk/lotio';

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
  wasmPath: './lotio.wasm'
});

// Event handlers
animation
  .on('loaded', (anim) => anim.start())
  .on('frame', () => {
    animation.renderToCanvas(canvas);
  });
```

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
- `setFonts(fonts)` - Set fonts array

#### Getters

- `getFps()` - Get current FPS
- `getAnimation()` - Get animation data
- `getTextConfig()` - Get text configuration
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

## License

MIT

