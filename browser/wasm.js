// Load WASM module
let Module = null;

export async function initLotio(wasmPath = './lotio.wasm') {
    // Load Emscripten module
    let jsPath = wasmPath.replace('.wasm', '.js');
    
    // Keep relative paths as-is - script tags resolve relative to the page's base URL
    // Script tags work best with relative paths like './browser/lotio.js'
    // Only convert to absolute if it's already an absolute URL (http/https)
    if (!jsPath.startsWith('http://') && !jsPath.startsWith('https://') && !jsPath.startsWith('//')) {
        // Path is relative - keep it relative for script tags
        // Script tags will resolve it relative to the current page automatically
        // No need to convert to absolute path
    }
    
    // Check if already loaded
    if (typeof createLotioModule !== 'undefined') {
        Module = await createLotioModule();
        return true;
    }
    
    // Load the module using a script tag approach for CommonJS compatibility
    return new Promise((resolve, reject) => {
        // Load the script
        const script = document.createElement('script');
        script.src = jsPath;
        script.async = true;
        script.onload = () => {
            // Wait for the script to execute and define createLotioModule
            const tryLoad = () => {
                // The script should define createLotioModule globally
                const factory = window.createLotioModule || globalThis.createLotioModule || (typeof createLotioModule !== 'undefined' ? createLotioModule : null);
                if (typeof factory === 'function') {
                    factory().then(mod => {
                        Module = mod;
                        // Verify Module has required functions and properties
                        if (!Module._malloc || !Module._free || !Module._lotio_init) {
                            console.error('Module object:', Module);
                            console.error('Available functions:', Object.keys(Module).filter(k => typeof Module[k] === 'function').slice(0, 20));
                            reject(new Error('Module is missing required functions (_malloc, _free, or _lotio_init)'));
                            return;
                        }
                        // HEAP arrays should be exported directly, but if not, try to create them
                        // First check if they're already available
                        if (Module.HEAP8 && Module.HEAP32 && Module.HEAPF32 && Module.HEAPU8) {
                            console.log('HEAP arrays already available');
                        } else {
                            // Try to find the memory buffer
                            let memoryBuffer = null;
                            
                            // Check if HEAP arrays exist but weren't detected
                            if (Module.HEAP8 && Module.HEAP8.buffer) {
                                memoryBuffer = Module.HEAP8.buffer;
                            } else if (Module.HEAPU8 && Module.HEAPU8.buffer) {
                                memoryBuffer = Module.HEAPU8.buffer;
                            } else if (Module.memory && Module.memory.buffer) {
                                memoryBuffer = Module.memory.buffer;
                            } else if (Module.buffer) {
                                memoryBuffer = Module.buffer;
                            } else {
                                // Last resort: try to get memory from WASM instance
                                // The Module might have the memory internally
                                console.warn('HEAP arrays not found, but continuing - they may be created on first use');
                                // Don't reject, let's see if they get created when needed
                            }
                            
                            if (memoryBuffer) {
                                // Create HEAP arrays from the buffer
                                Module.HEAP8 = new Int8Array(memoryBuffer);
                                Module.HEAPU8 = new Uint8Array(memoryBuffer);
                                Module.HEAP16 = new Int16Array(memoryBuffer);
                                Module.HEAPU16 = new Uint16Array(memoryBuffer);
                                Module.HEAP32 = new Int32Array(memoryBuffer);
                                Module.HEAPU32 = new Uint32Array(memoryBuffer);
                                Module.HEAPF32 = new Float32Array(memoryBuffer);
                                Module.HEAPF64 = new Float64Array(memoryBuffer);
                                console.log('Created HEAP arrays from memory buffer');
                            }
                        }
                        console.log('Module initialized successfully');
                        resolve(true);
                    }).catch(reject);
                } else {
                    // Retry after a short delay
                    if (typeof window.createLotioModule === 'undefined' && typeof createLotioModule === 'undefined') {
                        setTimeout(tryLoad, 50);
                    } else {
                        console.error('Available globals:', Object.keys(window).filter(k => k.includes('Lotio') || k.includes('lotio') || k.includes('create')));
                        reject(new Error('createLotioModule not found after loading script. Check console for available globals.'));
                    }
                }
            };
            tryLoad();
        };
        script.onerror = (error) => {
            reject(new Error(`Failed to load script: ${jsPath} - ${error}`));
        };
        document.head.appendChild(script);
    });
}

export function createAnimation(jsonData, textConfig = null, textPadding = 0.97, textMeasurementMode = 'accurate') {
    if (!Module) {
        throw new Error('WASM module not initialized. Call initLotio() first.');
    }
    
    // Check if Module has the required functions
    if (!Module._malloc || !Module._free || !Module._lotio_init) {
        console.error('Module object:', Module);
        console.error('Available functions:', Object.keys(Module).filter(k => typeof Module[k] === 'function'));
        throw new Error('Module is not properly initialized. Missing _malloc, _free, or _lotio_init functions.');
    }
    
    // Ensure HEAP arrays are available - create them from memory if needed
    if (!Module.HEAP32 || !Module.HEAPF32 || !Module.HEAPU8) {
        // Try to find the memory buffer
        let memoryBuffer = null;
        
        // Check various ways the memory might be exposed
        if (Module.HEAP8 && Module.HEAP8.buffer) {
            memoryBuffer = Module.HEAP8.buffer;
        } else if (Module.HEAPU8 && Module.HEAPU8.buffer) {
            memoryBuffer = Module.HEAPU8.buffer;
        } else if (Module.memory && Module.memory.buffer) {
            memoryBuffer = Module.memory.buffer;
        } else if (Module.buffer) {
            memoryBuffer = Module.buffer;
        } else {
            // Try to get memory from WASM instance
            // After first malloc, memory should be initialized
            // Allocate a small buffer to trigger memory initialization
            const testPtr = Module._malloc(1);
            if (testPtr !== null && testPtr !== 0) {
                // Now try to access memory
                // The memory should be available via Module.HEAP* after first allocation
                if (Module.HEAP8) {
                    memoryBuffer = Module.HEAP8.buffer;
                } else {
                    Module._free(testPtr);
                    throw new Error('Cannot access WASM memory buffer even after allocation');
                }
                Module._free(testPtr);
            } else {
                throw new Error('Failed to allocate test memory - WASM memory not initialized');
            }
        }
        
        if (memoryBuffer) {
            Module.HEAP8 = new Int8Array(memoryBuffer);
            Module.HEAPU8 = new Uint8Array(memoryBuffer);
            Module.HEAP16 = new Int16Array(memoryBuffer);
            Module.HEAPU16 = new Uint16Array(memoryBuffer);
            Module.HEAP32 = new Int32Array(memoryBuffer);
            Module.HEAPU32 = new Uint32Array(memoryBuffer);
            Module.HEAPF32 = new Float32Array(memoryBuffer);
            Module.HEAPF64 = new Float64Array(memoryBuffer);
        }
    }
    
    const jsonStr = typeof jsonData === 'string' ? jsonData : JSON.stringify(jsonData);
    const textConfigStr = textConfig ? (typeof textConfig === 'string' ? textConfig : JSON.stringify(textConfig)) : null;
    
    // Convert textMeasurementMode string to integer (0=FAST, 1=ACCURATE, 2=PIXEL_PERFECT)
    const modeStr = String(textMeasurementMode).toLowerCase();
    let modeInt = 1; // Default to ACCURATE
    if (modeStr === 'fast') {
        modeInt = 0;
    } else if (modeStr === 'accurate') {
        modeInt = 1;
    } else if (modeStr === 'pixel-perfect' || modeStr === 'pixelperfect') {
        modeInt = 2;
    }
    
    // Allocate memory using exported _malloc
    const jsonPtr = Module._malloc(jsonStr.length + 1);
    if (!jsonPtr) {
        throw new Error('Failed to allocate memory for JSON data');
    }
    Module.stringToUTF8(jsonStr, jsonPtr, jsonStr.length + 1);
    
    let textConfigPtr = 0;
    let textConfigLen = 0;
    if (textConfigStr) {
        textConfigPtr = Module._malloc(textConfigStr.length + 1);
        if (!textConfigPtr) {
            Module._free(jsonPtr);
            throw new Error('Failed to allocate memory for text config');
        }
        textConfigLen = textConfigStr.length;
        Module.stringToUTF8(textConfigStr, textConfigPtr, textConfigStr.length + 1);
    }
    
    const result = Module._lotio_init(jsonPtr, jsonStr.length, textConfigPtr, textConfigLen, textPadding, modeInt);
    
    Module._free(jsonPtr);
    if (textConfigPtr) {
        Module._free(textConfigPtr);
    }
    
    if (result !== 0) {
        throw new Error('Failed to initialize animation');
    }
    
    // Get animation info
    const widthPtr = Module._malloc(4);
    const heightPtr = Module._malloc(4);
    const durationPtr = Module._malloc(4);
    const fpsPtr = Module._malloc(4);
    
    Module._lotio_get_info(widthPtr, heightPtr, durationPtr, fpsPtr);
    
    const info = {
        width: Module.HEAP32[widthPtr / 4],
        height: Module.HEAP32[heightPtr / 4],
        duration: Module.HEAPF32[durationPtr / 4],
        fps: Module.HEAPF32[fpsPtr / 4]
    };
    
    Module._free(widthPtr);
    Module._free(heightPtr);
    Module._free(durationPtr);
    Module._free(fpsPtr);
    
    return info;
}

export function renderFrameRGBA(time) {
    if (!Module) {
        throw new Error('WASM module not initialized.');
    }
    
    // Ensure HEAP arrays are available - use same logic as createAnimation
    if (!Module.HEAP32 || !Module.HEAPU8) {
        let memoryBuffer = null;
        if (Module.HEAP8 && Module.HEAP8.buffer) {
            memoryBuffer = Module.HEAP8.buffer;
        } else if (Module.HEAPU8 && Module.HEAPU8.buffer) {
            memoryBuffer = Module.HEAPU8.buffer;
        } else if (Module.memory && Module.memory.buffer) {
            memoryBuffer = Module.memory.buffer;
        } else {
            // Trigger memory initialization with a test allocation
            const testPtr = Module._malloc(1);
            if (testPtr !== null && testPtr !== 0) {
                if (Module.HEAP8) {
                    memoryBuffer = Module.HEAP8.buffer;
                }
                Module._free(testPtr);
            }
        }
        if (memoryBuffer) {
            Module.HEAPU8 = new Uint8Array(memoryBuffer);
            Module.HEAP32 = new Int32Array(memoryBuffer);
        }
    }
    
    // Get animation info to determine buffer size
    const widthPtr = Module._malloc(4);
    const heightPtr = Module._malloc(4);
    Module._lotio_get_info(widthPtr, heightPtr, null, null);
    const width = Module.HEAP32[widthPtr / 4];
    const height = Module.HEAP32[heightPtr / 4];
    Module._free(widthPtr);
    Module._free(heightPtr);
    
    const bufferSize = width * height * 4; // RGBA
    const bufferPtr = Module._malloc(bufferSize);
    
    const result = Module._lotio_render_frame(time, bufferPtr, bufferSize);
    
    if (result !== 0) {
        Module._free(bufferPtr);
        const errorMsg = result === 1 ? 'Animation not initialized' :
                        result === 2 ? 'Buffer too small' :
                        result === 3 ? 'Failed to create surface' :
                        result === 4 ? 'Failed to create image snapshot' :
                        result === 5 ? 'Failed to read pixels' :
                        result === 6 ? 'Failed to create conversion surface' :
                        result === 7 ? 'Failed to create converted image' :
                        `Unknown error: ${result}`;
        throw new Error(`Failed to render frame: ${errorMsg} (code ${result})`);
    }
    
    // Copy data to Uint8Array
    // Create a view into the WASM memory
    const bufferView = new Uint8Array(Module.HEAPU8.buffer, bufferPtr, bufferSize);
    // Create a copy to avoid issues with memory being freed
    const rgba = new Uint8Array(bufferView);
    
    Module._free(bufferPtr);
    
    return { rgba, width, height };
}

export function registerFont(fontName, fontData) {
    if (!Module) {
        throw new Error('WASM module not initialized. Call initLotio() first.');
    }
    
    if (!Module._lotio_register_font) {
        throw new Error('Font registration not available. Rebuild with _lotio_register_font exported.');
    }
    
    // Ensure HEAP arrays are available
    if (!Module.HEAPU8) {
        if (Module.memory && Module.memory.buffer) {
            Module.HEAPU8 = new Uint8Array(Module.memory.buffer);
        } else {
            throw new Error('Cannot access WASM memory for font registration.');
        }
    }
    
    // Allocate memory for font data
    const fontDataPtr = Module._malloc(fontData.length);
    if (!fontDataPtr) {
        throw new Error('Failed to allocate memory for font data');
    }
    
    // Copy font data to WASM memory
    Module.HEAPU8.set(fontData, fontDataPtr);
    
    // Allocate memory for font name
    const fontNamePtr = Module._malloc(fontName.length + 1);
    if (!fontNamePtr) {
        Module._free(fontDataPtr);
        throw new Error('Failed to allocate memory for font name');
    }
    Module.stringToUTF8(fontName, fontNamePtr, fontName.length + 1);
    
    // Register font
    const result = Module._lotio_register_font(fontNamePtr, fontDataPtr, fontData.length);
    
    // Free memory
    Module._free(fontDataPtr);
    Module._free(fontNamePtr);
    
    if (result !== 0) {
        throw new Error(`Failed to register font: ${fontName} (error code: ${result})`);
    }
    
    console.log(`Font registered: ${fontName}`);
}

export function cleanup() {
    if (Module) {
        Module._lotio_cleanup();
    }
}

// Helper to render frame to canvas
export function renderFrameToCanvas(canvas, time, bgColor = '#2a2a2a') {
    const { rgba, width, height } = renderFrameRGBA(time);
    
    // Set canvas size
    canvas.width = width;
    canvas.height = height;
    
    const ctx = canvas.getContext('2d');
    
    // Fill with background color first
    ctx.fillStyle = bgColor;
    ctx.fillRect(0, 0, width, height);
    
    const imageData = ctx.createImageData(width, height);
    
    // Copy RGBA data - ensure we have the right length
    if (rgba.length !== width * height * 4) {
        console.error(`RGBA buffer size mismatch: expected ${width * height * 4}, got ${rgba.length}`);
        throw new Error(`Invalid RGBA buffer size: expected ${width * height * 4}, got ${rgba.length}`);
    }
    
    // Create a temporary canvas to composite the image over the background
    // This ensures the background color shows through transparent areas
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = width;
    tempCanvas.height = height;
    const tempCtx = tempCanvas.getContext('2d');
    
    // Fill temp canvas with background color
    tempCtx.fillStyle = bgColor;
    tempCtx.fillRect(0, 0, width, height);
    
    // Draw the RGBA image data on top
    imageData.data.set(rgba);
    tempCtx.putImageData(imageData, 0, 0);
    
    // Draw the composited result to the main canvas
    ctx.drawImage(tempCanvas, 0, 0);
}

// Helper to render frame as ImageData
export function renderFrameAsImageData(time) {
    const { rgba, width, height } = renderFrameRGBA(time);
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d');
    const imageData = ctx.createImageData(width, height);
    imageData.data.set(rgba);
    return imageData;
}

