#include "text_processor.h"
#include "text_config.h"
#include "font_utils.h"
#include "text_sizing.h"
#include "json_manipulation.h"
#include "../utils/logging.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontScanner_FreeType.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include <fontconfig/fontconfig.h>
#include <regex>
#include <vector>
#include <algorithm>
#include <cmath>

std::string processTextConfiguration(
    std::string& json_data,
    const std::string& text_config_file
) {
    if (text_config_file.empty()) {
        return json_data;  // No processing needed
    }
    
    LOG_DEBUG("Loading text configuration from: " << text_config_file);
    auto textConfigs = parseTextConfig(text_config_file);
    
    if (textConfigs.empty()) {
        LOG_DEBUG("No text layer configuration(s) found in config file");
        return json_data;
    }
    
    LOG_DEBUG("Found " << textConfigs.size() << " text layer configurations");
    
    // Extract animation width from JSON (for fallback text box width)
    float animationWidth = 720.0f;  // Default fallback
    std::regex widthPattern("\"w\"\\s*:\\s*([0-9]+\\.?[0-9]*)");
    std::smatch widthMatch;
    if (std::regex_search(json_data, widthMatch, widthPattern)) {
        animationWidth = std::stof(widthMatch[1].str());
        LOG_DEBUG("Animation width: " << animationWidth);
    }
    
    // Create font manager early for text measurement
    sk_sp<SkFontMgr> tempFontMgr;
    try {
        const auto fcInitOk = FcInit();
        auto scanner = SkFontScanner_Make_FreeType();
        if (scanner) {
            tempFontMgr = SkFontMgr_New_FontConfig(nullptr, std::move(scanner));
        }
    } catch (...) {
        // Will create font manager later
    }
    
    if (!tempFontMgr) {
        tempFontMgr = SkFontMgr::RefEmpty();
    }
    
    // First pass: extract all font info and calculate optimal sizes
    struct LayerModification {
        std::string layerName;
        std::string textToUse;
        float optimalSize;
        float originalTextWidth;  // Original text width at original size
        float newTextWidth;       // New text width at optimal size
    };
    std::vector<LayerModification> modifications;
    
    for (const auto& [layerName, config] : textConfigs) {
        LOG_DEBUG("Processing text layer: " << layerName);
        
        // Extract font info from JSON
        FontInfo fontInfo = extractFontInfoFromJson(json_data, layerName);
        
        if (fontInfo.name.empty()) {
            LOG_DEBUG("Warning: Could not find font info for layer " << layerName);
            continue;
        }
        
        // Determine text to use
        std::string textToUse = config.textValue.empty() ? fontInfo.text : config.textValue;
        
        if (textToUse.empty()) {
            LOG_DEBUG("Warning: No text value for layer " << layerName);
            continue;
        }
        
        // Determine target width (priority: config override > JSON sz > animation width)
        float targetWidth = animationWidth;
        if (config.textBoxWidth > 0) {
            targetWidth = config.textBoxWidth;
        } else if (fontInfo.textBoxWidth > 0) {
            targetWidth = fontInfo.textBoxWidth;
        }
        
        // Debug: measure current text at original size
        float currentWidth = measureTextWidth(tempFontMgr.get(), fontInfo.family, fontInfo.style,
                                             fontInfo.name, fontInfo.size, textToUse);
        LOG_DEBUG("  Original text: \"" << textToUse << "\"");
        LOG_DEBUG("  Original size: " << fontInfo.size << ", measured width: " << currentWidth);
        if (config.textBoxWidth > 0) {
            LOG_DEBUG("  Text box width (from config override): " << config.textBoxWidth);
        } else if (fontInfo.textBoxWidth > 0) {
            LOG_DEBUG("  Text box width (from sz): " << fontInfo.textBoxWidth);
        } else {
            LOG_DEBUG("  Text box width: not found, using animation width");
        }
        LOG_DEBUG("  Target width: " << targetWidth);
        LOG_DEBUG("  Min size: " << config.minSize << ", Max size: " << config.maxSize);
        
        // Calculate optimal font size
        float optimalSize = calculateOptimalFontSize(
            tempFontMgr.get(),
            fontInfo,
            config,
            textToUse,
            targetWidth
        );
        
        float finalWidth = 0.0f;
        if (optimalSize >= 0) {
            finalWidth = measureTextWidth(tempFontMgr.get(), fontInfo.family, fontInfo.style,
                                         fontInfo.name, optimalSize, textToUse);
            LOG_DEBUG("  Optimal size: " << optimalSize << ", final width: " << finalWidth);
        }
        
        if (optimalSize < 0) {
            // Text doesn't fit even at min size, use fallback
            float minWidth = measureTextWidth(tempFontMgr.get(), fontInfo.family, fontInfo.style,
                                             fontInfo.name, config.minSize, textToUse);
            LOG_DEBUG("Text doesn't fit at min size for " << layerName << ":");
            LOG_DEBUG("  Text length: " << textToUse.length() << " characters");
            LOG_DEBUG("  Text content: \"" << textToUse << "\"");
            LOG_DEBUG("  Measured width at min size (" << config.minSize << "): " << minWidth);
            LOG_DEBUG("  Using fallback text: \"" << config.fallbackText << "\"");
            textToUse = config.fallbackText;
            
            // Create a temporary fontInfo with minSize as the starting size
            FontInfo fallbackFontInfo = fontInfo;
            fallbackFontInfo.size = config.minSize;
            
            // Measure fallback text at min size
            float fallbackMinWidth = measureTextWidth(tempFontMgr.get(), fallbackFontInfo.family, 
                                                     fallbackFontInfo.style, fallbackFontInfo.name, 
                                                     config.minSize, textToUse);
            
            if (fallbackMinWidth > targetWidth) {
                // Fallback doesn't fit even at min size, use min size anyway (will overflow)
                LOG_DEBUG("  Fallback text doesn't fit at min size (" << fallbackMinWidth << " > " << targetWidth << "), using min size (will overflow)");
                optimalSize = config.minSize;
                finalWidth = measureTextWidth(tempFontMgr.get(), fallbackFontInfo.family,
                                             fallbackFontInfo.style, fallbackFontInfo.name,
                                             config.minSize, textToUse);
            } else {
                // Fallback fits at min size, try to maximize up to maxSize
                float min = config.minSize;
                float max = config.maxSize;
                float bestSize = config.minSize;
                
                for (int i = 0; i < 10; i++) {  // Binary search
                    float testSize = (min + max) / 2.0f;
                    float testWidth = measureTextWidth(tempFontMgr.get(), fallbackFontInfo.family,
                                                      fallbackFontInfo.style, fallbackFontInfo.name,
                                                      testSize, textToUse);
                    
                    if (testWidth <= targetWidth) {
                        bestSize = testSize;
                        min = testSize;
                    } else {
                        max = testSize;
                    }
                }
                
                optimalSize = std::min(bestSize, config.maxSize);
                finalWidth = measureTextWidth(tempFontMgr.get(), fallbackFontInfo.family,
                                             fallbackFontInfo.style, fallbackFontInfo.name,
                                             optimalSize, textToUse);
                LOG_DEBUG("  Fallback text optimal size: " << optimalSize << " (width: " << finalWidth << " / " << targetWidth << ")");
            }
        }
        
        // Store original and new text widths for position adjustment
        float originalTextWidth = currentWidth;
        float newTextWidth = finalWidth;
        
        modifications.push_back({layerName, textToUse, optimalSize, originalTextWidth, newTextWidth});
    }
    
    // Second pass: apply modifications in reverse order (from end to start)
    // This prevents position shifts from affecting subsequent modifications
    for (auto it = modifications.rbegin(); it != modifications.rend(); ++it) {
        modifyTextLayerInJson(json_data, it->layerName, it->textToUse, it->optimalSize);
        
        // Adjust text animator position keyframes based on text width change
        float widthDiff = it->newTextWidth - it->originalTextWidth;
        if (std::abs(widthDiff) > 0.1f) {  // Only adjust if there's a significant change
            // Always move further left by the absolute difference to ensure text stays off-screen
            float adjustment = std::abs(widthDiff);
            adjustTextAnimatorPosition(json_data, it->layerName, adjustment);
            LOG_DEBUG("Adjusted text animator position for " << it->layerName << " by " << adjustment << "px (widthDiff: " << widthDiff << ")");
        }
        
        LOG_DEBUG("Updated " << it->layerName << ": text=\"" << it->textToUse << "\", size=" << it->optimalSize);
    }
    
    return json_data;
}

