#include "text_processor.h"
#include "layer_overrides.h"
#include "font_utils.h"
#include "text_sizing.h"
#include "json_manipulation.h"
#include "../utils/logging.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontScanner_FreeType.h"
#ifndef __EMSCRIPTEN__
#include "include/ports/SkFontMgr_fontconfig.h"
#include <fontconfig/fontconfig.h>
#endif
#include <nlohmann/json.hpp>
#include <vector>
#include <filesystem>

std::string processLayerOverrides(
    std::string& json_data,
    const std::string& layer_overrides_file,
    float textPadding,
    TextMeasurementMode textMeasurementMode
) {
    if (layer_overrides_file.empty()) {
        return json_data;  // No processing needed
    }
    
    const char* modeStr = (textMeasurementMode == TextMeasurementMode::FAST) ? "FAST" :
                         (textMeasurementMode == TextMeasurementMode::ACCURATE) ? "ACCURATE" : "PIXEL_PERFECT";
    LOG_DEBUG("Loading layer overrides from: " << layer_overrides_file);
    LOG_DEBUG("Text measurement mode: " << modeStr);
    LOG_DEBUG("Text padding: " << textPadding << " (" << (textPadding * 100.0f) << "% of target width)");
    auto layerOverrides = parseLayerOverrides(layer_overrides_file);
    auto imageLayers = parseImageLayers(layer_overrides_file);
    
    // Get the layer-overrides file's parent directory for resolving relative image paths
    std::filesystem::path overridesPath(layer_overrides_file);
    std::filesystem::path overridesBaseDir = overridesPath.has_parent_path() 
        ? overridesPath.parent_path() 
        : std::filesystem::path(".");
    std::error_code ec;
    std::filesystem::path absOverridesBaseDir = std::filesystem::absolute(overridesBaseDir, ec);
    const std::string overridesBaseDirStr = (ec ? overridesBaseDir.string() : absOverridesBaseDir.string());
    LOG_DEBUG("Layer-overrides base directory for relative image paths: " << overridesBaseDirStr);
    
    // Process image layer overrides first (before text processing)
    if (!imageLayers.empty()) {
        LOG_DEBUG("Found " << imageLayers.size() << " image layer overrides");
        
        try {
            nlohmann::json j = nlohmann::json::parse(json_data);
            
            if (j.contains("assets") && j["assets"].is_array()) {
                // Process each asset
                for (const auto& [assetId, imageConfig] : imageLayers) {
                    LOG_DEBUG("Processing image override for asset ID: " << assetId);
                    
                    // Find asset by ID
                    nlohmann::json* foundAsset = nullptr;
                    for (auto& asset : j["assets"]) {
                        if (asset.contains("id") && asset["id"].is_string() && asset["id"].get<std::string>() == assetId) {
                            foundAsset = &asset;
                            break;
                        }
                    }
                    
                    if (foundAsset == nullptr) {
                        LOG_CERR("[WARNING] Asset ID " << assetId << " not found in assets array") << std::endl;
                        continue;
                    }
                    
                    // Determine the full path from filePath and fileName
                    std::string dir;
                    std::string filename;
                    
                    if (imageConfig.filePath.empty() && !imageConfig.fileName.empty()) {
                        // filePath is empty string, fileName contains full path
                        std::filesystem::path fullPathObj(imageConfig.fileName);
                        if (fullPathObj.is_absolute()) {
                            dir = fullPathObj.parent_path().string();
                            filename = fullPathObj.filename().string();
                        } else {
                            // Relative path - keep it relative, split into dir and filename
                            size_t lastSlash = imageConfig.fileName.find_last_of("/\\");
                            dir = (lastSlash != std::string::npos) ? imageConfig.fileName.substr(0, lastSlash + 1) : "";
                            filename = (lastSlash != std::string::npos) ? imageConfig.fileName.substr(lastSlash + 1) : imageConfig.fileName;
                        }
                    } else if (!imageConfig.filePath.empty() && !imageConfig.fileName.empty()) {
                        // Both specified, combine them
                        std::filesystem::path pathObj(imageConfig.filePath);
                        if (pathObj.is_absolute()) {
                            // Absolute path - use as-is
                            dir = imageConfig.filePath;
                            if (dir.back() != '/' && dir.back() != '\\') {
                                dir += "/";
                            }
                            filename = imageConfig.fileName;
                        } else {
                            // Relative path - keep it relative (don't convert to absolute)
                            // Skia will resolve it relative to the animation file's directory
                            dir = imageConfig.filePath;
                            if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
                                dir += "/";
                            }
                            filename = imageConfig.fileName;
                        }
                    } else if (!imageConfig.filePath.empty() && imageConfig.fileName.empty()) {
                        // Only filePath specified - use default fileName from assets[].p
                        dir = imageConfig.filePath;
                        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
                            dir += "/";
                        }
                        // Extract filename from assets[].p
                        if ((*foundAsset).contains("p") && (*foundAsset)["p"].is_string()) {
                            filename = (*foundAsset)["p"].get<std::string>();
                            LOG_DEBUG("Using default fileName from assets[].p: " << filename);
                        } else {
                            LOG_CERR("[WARNING] Could not find \"p\" property for asset ID: " << assetId << ", skipping") << std::endl;
                            continue;
                        }
                    } else {
                        // Both empty - skip (shouldn't happen due to validation)
                        LOG_CERR("[WARNING] Both filePath and fileName are empty for asset ID: " << assetId) << std::endl;
                        continue;
                    }
                    
                    // Normalize directory path (ensure it ends with / for relative paths)
                    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
                        dir += "/";
                    }
                    if (dir == "/" || dir == "\\") {
                        dir = "";  // Root path means empty directory
                    }
                    
                    // Update u and p properties
                    (*foundAsset)["u"] = dir;
                    (*foundAsset)["p"] = filename;
                    
                    LOG_DEBUG("Updated asset " << assetId << ": u=\"" << dir << "\", p=\"" << filename << "\"");
                    LOG_DEBUG("Image override applied successfully for asset ID: " << assetId);
                }
                
                // Serialize back to JSON
                json_data = j.dump(4);
                LOG_DEBUG("Assets array updated in JSON");
            } else {
                LOG_CERR("[WARNING] Assets array not found in JSON - image overrides will not be applied") << std::endl;
            }
        } catch (const nlohmann::json::exception& e) {
            LOG_CERR("[ERROR] Failed to parse JSON for image asset processing: " << e.what()) << std::endl;
        }
    }
    
    if (layerOverrides.empty()) {
        LOG_DEBUG("No text layer overrides found in config file");
        return json_data;
    }
    
    LOG_DEBUG("Found " << layerOverrides.size() << " text layer overrides");
    
    // Extract animation width from JSON (for fallback text box width)
    float animationWidth = 720.0f;  // Default fallback
    try {
        nlohmann::json j = nlohmann::json::parse(json_data);
        if (j.contains("w") && j["w"].is_number()) {
            animationWidth = j["w"].get<float>();
            LOG_DEBUG("Animation width: " << animationWidth);
        }
    } catch (const nlohmann::json::exception&) {
        // Use default width if parsing fails
    }
    
    // Create font manager early for text measurement
    sk_sp<SkFontMgr> tempFontMgr;
#ifndef __EMSCRIPTEN__
    try {
        FcInit(); // Initialize fontconfig
        auto scanner = SkFontScanner_Make_FreeType();
        if (scanner) {
            tempFontMgr = SkFontMgr_New_FontConfig(nullptr, std::move(scanner));
        }
    } catch (...) {
        // Will create font manager later
    }
#endif
    
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
    
    for (const auto& [layerName, config] : layerOverrides) {
        LOG_DEBUG("Processing text layer: " << layerName);
        
        // Extract font info from JSON
        FontInfo fontInfo = extractFontInfoFromJson(json_data, layerName);
        
        if (fontInfo.name.empty()) {
            LOG_DEBUG("Warning: Could not find font info for layer " << layerName);
            continue;
        }
        
        // Determine text to use
        std::string textToUse = config.value.empty() ? fontInfo.text : config.value;
        
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
                                             fontInfo.name, fontInfo.size, textToUse, textMeasurementMode);
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
        
        // If minSize and maxSize are not specified, no auto-fit - just use original size or update text value
        float optimalSize = fontInfo.size;
        float finalWidth = 0.0f;
        
        if (config.minSize > 0 && config.maxSize > 0) {
            // Apply padding to target width to prevent text from touching edges
            // textPadding: 0.97 means 97% of target width (3% padding, 1.5% per side)
            float paddedTargetWidth = targetWidth * textPadding;
            LOG_DEBUG("  Padded target width: " << paddedTargetWidth << " (" << (textPadding * 100.0f) << "% of " << targetWidth << ")");
            
            // Calculate optimal font size
            optimalSize = calculateOptimalFontSize(
                tempFontMgr.get(),
                fontInfo,
                config,
                textToUse,
                paddedTargetWidth,
                textMeasurementMode
            );
            
            if (optimalSize >= 0) {
                finalWidth = measureTextWidth(tempFontMgr.get(), fontInfo.family, fontInfo.style,
                                             fontInfo.name, optimalSize, textToUse, textMeasurementMode);
                LOG_DEBUG("  Optimal size: " << optimalSize << ", final width: " << finalWidth);
            }
            
            if (optimalSize < 0) {
            // Text doesn't fit even at min size, use fallback
            float minWidth = measureTextWidth(tempFontMgr.get(), fontInfo.family, fontInfo.style,
                                             fontInfo.name, config.minSize, textToUse, textMeasurementMode);
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
                                                     config.minSize, textToUse, textMeasurementMode);
            
            if (fallbackMinWidth > paddedTargetWidth) {
                // Fallback doesn't fit even at min size, use min size anyway (will overflow)
                LOG_DEBUG("  Fallback text doesn't fit at min size (" << fallbackMinWidth << " > " << paddedTargetWidth << "), using min size (will overflow)");
                optimalSize = config.minSize;
                finalWidth = measureTextWidth(tempFontMgr.get(), fallbackFontInfo.family,
                                             fallbackFontInfo.style, fallbackFontInfo.name,
                                             config.minSize, textToUse, textMeasurementMode);
            } else {
                // Fallback fits at min size, try to maximize up to maxSize
                float min = config.minSize;
                float max = config.maxSize;
                float bestSize = config.minSize;
                
                for (int i = 0; i < 10; i++) {  // Binary search
                    float testSize = (min + max) / 2.0f;
                    float testWidth = measureTextWidth(tempFontMgr.get(), fallbackFontInfo.family,
                                                      fallbackFontInfo.style, fallbackFontInfo.name,
                                                      testSize, textToUse, textMeasurementMode);
                    
                    if (testWidth <= paddedTargetWidth) {
                        bestSize = testSize;
                        min = testSize;
                    } else {
                        max = testSize;
                    }
                }
                
                optimalSize = std::min(bestSize, config.maxSize);
                finalWidth = measureTextWidth(tempFontMgr.get(), fallbackFontInfo.family,
                                             fallbackFontInfo.style, fallbackFontInfo.name,
                                             optimalSize, textToUse, textMeasurementMode);
                LOG_DEBUG("  Fallback text optimal size: " << optimalSize << " (width: " << finalWidth << " / " << paddedTargetWidth << ")");
            }
            }
        } else {
            // No auto-fit, just measure at original size
            finalWidth = currentWidth;
            optimalSize = fontInfo.size;
            LOG_DEBUG("  No auto-fit (minSize/maxSize not specified), using original size: " << optimalSize);
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

