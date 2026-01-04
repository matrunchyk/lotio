/**
 * Lotio - High-performance Lottie animation renderer for the browser
 * @module @matrunchyk/lotio
 */

/**
 * Lotio - High-performance Lottie animation renderer for the browser
 * @module @matrunchyk/lotio
 */

import { initLotio, createAnimation, renderFrameToCanvas, renderFrameRGBA, cleanup, registerFont } from './wasm.js';

/**
 * Frame output types
 */
export const FrameType = {
    PNG: 'png',
    WEBP: 'webp'
};

/**
 * Animation states
 */
export const State = {
    STOPPED: 'stopped',
    PAUSED: 'paused',
    LOADED: 'loaded',
    ERROR: 'error',
    PLAYING: 'playing'
};

/**
 * Text measurement modes
 */
export const TextMeasurementMode = {
    FAST: 'fast',
    ACCURATE: 'accurate',
    PIXEL_PERFECT: 'pixel-perfect'
};

/**
 * Event emitter implementation
 */
class EventEmitter {
    constructor() {
        this.events = {};
    }

    on(event, callback) {
        if (!this.events[event]) {
            this.events[event] = [];
        }
        this.events[event].push(callback);
        return this; // Fluent interface
    }

    off(event, callback) {
        if (this.events[event]) {
            this.events[event] = this.events[event].filter(cb => cb !== callback);
        }
        return this;
    }

    emit(event, ...args) {
        if (this.events[event]) {
            this.events[event].forEach(callback => {
                try {
                    callback(...args);
                } catch (error) {
                    console.error(`Error in event handler for ${event}:`, error);
                }
            });
        }
        return this;
    }
}

/**
 * Lotio animation class
 */
export class Lotio extends EventEmitter {
    /**
     * @param {Object} options - Configuration options
     * @param {Array<{name: string, data: Uint8Array}>} options.fonts - Font files to load
     * @param {number} options.fps - Frames per second (default: 30)
     * @param {Object|string} options.animation - Lottie animation JSON (object or string)
     * @param {Object|string} options.textConfig - Text configuration JSON (object or string)
     * @param {number} options.textPadding - Text padding factor (0.0-1.0, default: 0.97)
     * @param {string} options.textMeasurementMode - Text measurement mode: 'fast'|'accurate'|'pixel-perfect' (default: 'accurate')
     * @param {string} options.type - Output type: 'png' or 'webp' (default: 'png')
     * @param {string} options.wasmPath - Path to lotio.wasm file (default: './lotio.wasm')
     */
    constructor(options = {}) {
        super();
        
        this._wasmPath = options.wasmPath || '../lib/lotio.wasm';
        this._fonts = options.fonts || [];
        this._fps = options.fps || 30;
        this._animation = options.animation || null;
        this._textConfig = options.textConfig || null;
        this._textPadding = options.textPadding !== undefined ? options.textPadding : 0.97;
        this._textMeasurementMode = options.textMeasurementMode || 'accurate';
        this._type = options.type || FrameType.PNG;
        this._state = State.STOPPED;
        this._wasmInitialized = false;
        this._animationInfo = null;
        this._currentFrame = 0;
        this._currentTime = 0;
        this._isPlaying = false;
        this._animationFrameId = null;
        this._startTime = 0;
        this._registeredFonts = new Set();
        
        // Auto-initialize if animation is provided
        if (this._animation) {
            this._init().catch(error => {
                this._setState(State.ERROR);
                this.emit('error', error, this);
            });
        }
    }

    /**
     * Initialize WASM module
     * @private
     */
    async _init() {
        if (this._wasmInitialized) {
            return;
        }

        try {
            await initLotio(this._wasmPath);
            this._wasmInitialized = true;
            
            // Register fonts
            for (const font of this._fonts) {
                await this._registerFont(font.name, font.data);
            }
            
            // Load animation if provided
            if (this._animation) {
                await this._loadAnimation();
            } else {
                this._setState(State.LOADED);
            }
        } catch (error) {
            this._setState(State.ERROR);
            this.emit('error', error, this);
            throw error;
        }
    }

    /**
     * Register a font
     * @private
     */
    async _registerFont(name, data) {
        if (this._registeredFonts.has(name)) {
            return;
        }
        
        if (!this._wasmInitialized) {
            await this._init();
        }
        
        registerFont(name, data);
        this._registeredFonts.add(name);
    }

    /**
     * Load animation
     * @private
     */
    async _loadAnimation() {
        if (!this._wasmInitialized) {
            await this._init();
        }

        try {
            const animationJson = typeof this._animation === 'string' 
                ? this._animation 
                : JSON.stringify(this._animation);
            
            const textConfigJson = this._textConfig 
                ? (typeof this._textConfig === 'string' 
                    ? this._textConfig 
                    : JSON.stringify(this._textConfig))
                : null;

            this._animationInfo = createAnimation(
                JSON.parse(animationJson),
                textConfigJson ? JSON.parse(textConfigJson) : null,
                this._textPadding,
                this._textMeasurementMode
            );

            this._currentTime = 0;
            this._currentFrame = 0;
            this._setState(State.LOADED);
            this.emit('loaded', this);
        } catch (error) {
            this._setState(State.ERROR);
            this.emit('error', error, this);
            throw error;
        }
    }

    /**
     * Set state and emit event
     * @private
     */
    _setState(newState) {
        const oldState = this._state;
        this._state = newState;
        
        if (oldState !== newState) {
            this.emit('statechange', newState, oldState, this);
        }
    }

    /**
     * Animation loop
     * @private
     */
    _animate() {
        if (!this._isPlaying || !this._animationInfo) {
            return;
        }

        const targetFPS = this._fps;
        const animationFPS = this._animationInfo.fps || 30;
        const speedMultiplier = targetFPS / animationFPS;

        const elapsed = (performance.now() - this._startTime) / 1000;
        this._currentTime = (elapsed * speedMultiplier) % this._animationInfo.duration;
        this._currentFrame = Math.floor(this._currentTime * animationFPS);

        // Check if animation ended
        if (this._currentTime >= this._animationInfo.duration) {
            this.stop();
            this.emit('end', this);
            return;
        }

        this.emit('frame', this._currentFrame, this._currentTime, this);
        this.emit('seek', this);

        this._animationFrameId = requestAnimationFrame(() => this._animate());
    }

    // Getters
    getFps() {
        return this._fps;
    }

    getAnimation() {
        return this._animation;
    }

    getTextConfig() {
        return this._textConfig;
    }

    getTextPadding() {
        return this._textPadding;
    }

    getTextMeasurementMode() {
        return this._textMeasurementMode;
    }

    getType() {
        return this._type;
    }

    getState() {
        return this._state;
    }

    getCurrentFrame() {
        if (!this._animationInfo) {
            return null;
        }

        const { rgba, width, height } = renderFrameRGBA(this._currentTime);
        
        return {
            number: this._currentFrame,
            time: this._currentTime,
            width,
            height,
            data: rgba,
            // Helper method to render to canvas
            renderToCanvas: (canvas, bgColor = '#2a2a2a') => {
                renderFrameToCanvas(canvas, this._currentTime, bgColor);
            }
        };
    }

    getAnimationInfo() {
        return this._animationInfo ? { ...this._animationInfo } : null;
    }

    // Setters (fluent interface)
    setFps(fps) {
        if (fps <= 0) {
            throw new Error('FPS must be greater than 0');
        }
        this._fps = fps;
        
        // Restart animation if playing
        if (this._isPlaying) {
            const currentTime = this._currentTime;
            this._startTime = performance.now() - (currentTime / (this._fps / (this._animationInfo?.fps || 30)) * 1000);
        }
        
        return this;
    }

    async setAnimation(animation) {
        this._animation = animation;
        
        if (this._isPlaying) {
            this.stop();
        }
        
        await this._loadAnimation();
        return this;
    }

    async setTextConfig(textConfig) {
        this._textConfig = textConfig;
        
        if (this._animation) {
            await this._loadAnimation();
        }
        
        return this;
    }

    setType(type) {
        if (type !== FrameType.PNG && type !== FrameType.WEBP) {
            throw new Error(`Type must be ${FrameType.PNG} or ${FrameType.WEBP}`);
        }
        this._type = type;
        return this;
    }

    async setFonts(fonts) {
        this._fonts = fonts;
        this._registeredFonts.clear();
        
        if (this._wasmInitialized) {
            for (const font of fonts) {
                await this._registerFont(font.name, font.data);
            }
            
            // Reload animation if loaded to apply new fonts
            if (this._animation) {
                await this._loadAnimation();
            }
        }
        
        return this;
    }

    // Flow control methods
    seek(frameOrTime) {
        if (!this._animationInfo) {
            throw new Error('Animation not loaded');
        }

        // Determine if input is frame number or time
        if (frameOrTime < 1) {
            // Likely a time value (0.0 - duration)
            this._currentTime = Math.max(0, Math.min(frameOrTime, this._animationInfo.duration));
            this._currentFrame = Math.floor(this._currentTime * (this._animationInfo.fps || 30));
        } else {
            // Likely a frame number
            const animationFPS = this._animationInfo.fps || 30;
            this._currentFrame = Math.max(0, Math.min(Math.floor(frameOrTime), Math.floor(this._animationInfo.duration * animationFPS)));
            this._currentTime = this._currentFrame / animationFPS;
        }

        if (this._isPlaying) {
            this._startTime = performance.now() - (this._currentTime / (this._fps / (this._animationInfo.fps || 30)) * 1000);
        }

        this.emit('seek', this);
        return this;
    }

    start() {
        if (!this._animationInfo) {
            throw new Error('Animation not loaded');
        }

        if (this._isPlaying) {
            return this;
        }

        this._isPlaying = true;
        this._setState(State.PLAYING);
        
        const animationFPS = this._animationInfo.fps || 30;
        const speedMultiplier = this._fps / animationFPS;
        this._startTime = performance.now() - (this._currentTime / speedMultiplier * 1000);
        
        this.emit('start', this);
        this._animate();
        
        return this;
    }

    pause() {
        if (!this._isPlaying) {
            return this;
        }

        this._isPlaying = false;
        this._setState(State.PAUSED);
        
        if (this._animationFrameId) {
            cancelAnimationFrame(this._animationFrameId);
            this._animationFrameId = null;
        }

        this.emit('pause', this);
        return this;
    }

    stop() {
        const wasPlaying = this._isPlaying;
        
        this.pause();
        this.seek(0);
        this._setState(State.STOPPED);

        if (wasPlaying) {
            this.emit('stop', this);
        }
        
        return this;
    }

    /**
     * Render current frame to canvas
     * @param {HTMLCanvasElement} canvas - Canvas element to render to
     * @param {string} bgColor - Background color (default: '#2a2a2a')
     */
    renderToCanvas(canvas, bgColor = '#2a2a2a') {
        if (!this._animationInfo) {
            throw new Error('Animation not loaded');
        }
        renderFrameToCanvas(canvas, this._currentTime, bgColor);
        return this;
    }

    /**
     * Cleanup resources
     */
    destroy() {
        this.stop();
        cleanup();
        this._wasmInitialized = false;
        this._animationInfo = null;
        this._state = State.STOPPED;
        this.emit('destroy', this);
        return this;
    }
}

// Export default
export default Lotio;
