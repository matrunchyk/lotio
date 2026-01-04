#include "text_sizing.h"
#include "font_utils.h"
#include "../utils/logging.h"
#include <algorithm>

float calculateOptimalFontSize(
    SkFontMgr* fontMgr,
    const FontInfo& fontInfo,
    const TextLayerConfig& config,
    const std::string& text,
    float targetWidth,
    TextMeasurementMode mode
) {
    if (targetWidth <= 0) {
        return fontInfo.size;  // No constraint, use original size
    }
    
    // Measure with current size
    float currentSize = fontInfo.size;
    float currentWidth = measureTextWidth(fontMgr, fontInfo.family, fontInfo.style, 
                                         fontInfo.name, currentSize, text, mode);
    
    if (g_debug_mode) {
        LOG_COUT("[DEBUG] calculateOptimalFontSize: text=\"" << text << "\", currentSize=" << currentSize 
                 << ", currentWidth=" << currentWidth << ", targetWidth=" << targetWidth) << std::endl;
    }
    
    // If text fits, try to maximize size up to maxSize
    if (currentWidth <= targetWidth) {
        // Binary search for maximum size that fits
        float min = currentSize;
        float max = config.maxSize;
        float bestSize = currentSize;
        
        for (int i = 0; i < 10; i++) {  // 10 iterations should be enough
            float testSize = (min + max) / 2.0f;
            float testWidth = measureTextWidth(fontMgr, fontInfo.family, fontInfo.style,
                                              fontInfo.name, testSize, text, mode);
            
            if (testWidth <= targetWidth) {
                bestSize = testSize;
                min = testSize;
            } else {
                max = testSize;
            }
        }
        
        return std::min(bestSize, config.maxSize);
    } else {
        // Text too wide, reduce size
        // First check if it fits at minimum size
        float minWidth = measureTextWidth(fontMgr, fontInfo.family, fontInfo.style,
                                         fontInfo.name, config.minSize, text, mode);
        if (minWidth > targetWidth) {
            // Doesn't fit even at min size - return -1 to indicate fallback needed
            if (g_debug_mode) {
                LOG_COUT("[DEBUG] calculateOptimalFontSize: text doesn't fit at minSize (" 
                         << config.minSize << "), width=" << minWidth << " > " << targetWidth) << std::endl;
            }
            return -1.0f;
        }
        
        // Binary search for maximum size that fits (between minSize and currentSize)
        float min = config.minSize;
        float max = currentSize;
        float bestSize = config.minSize;  // Start with minSize as the best we know fits
        
        for (int i = 0; i < 15; i++) {  // More iterations for better precision
            float testSize = (min + max) / 2.0f;
            float testWidth = measureTextWidth(fontMgr, fontInfo.family, fontInfo.style,
                                              fontInfo.name, testSize, text, mode);
            
            if (testWidth <= targetWidth) {
                // This size fits, try larger
                bestSize = testSize;
                min = testSize;
            } else {
                // This size doesn't fit, try smaller
                max = testSize;
            }
            
            // Early exit if we're close enough
            if ((max - min) < 0.1f) {
                break;
            }
        }
        
        if (g_debug_mode) {
            float finalWidth = measureTextWidth(fontMgr, fontInfo.family, fontInfo.style,
                                               fontInfo.name, bestSize, text, mode);
            LOG_COUT("[DEBUG] calculateOptimalFontSize: reduced from " << currentSize 
                     << " to " << bestSize << " (width: " << finalWidth << " / " << targetWidth << ")") << std::endl;
        }
        
        return bestSize;
    }
}

