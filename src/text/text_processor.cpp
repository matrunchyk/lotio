#include "text_processor.h"
#include "layer_overrides.h"
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
    
    LOG_DEBUG("Loading layer overrides from: " << layer_overrides_file);
    auto layerOverrides = parseLayerOverrides(layer_overrides_file);
    auto imagePaths = parseImagePaths(layer_overrides_file);
    
    // Process image path overrides first (before text processing)
    if (!imagePaths.empty()) {
        LOG_DEBUG("Found " << imagePaths.size() << " image path overrides");
        
        // Find assets array in JSON
        size_t assetsPos = json_data.find("\"assets\"");
        if (assetsPos != std::string::npos) {
            LOG_DEBUG("Found assets array in JSON at position " << assetsPos);
            size_t arrayStart = json_data.find('[', assetsPos);
            if (arrayStart != std::string::npos) {
                LOG_DEBUG("Found assets array start at position " << arrayStart);
                // Find the end of the assets array
                int bracketCount = 0;
                size_t arrayEnd = arrayStart;
                for (size_t i = arrayStart; i < json_data.length(); i++) {
                    if (json_data[i] == '[') bracketCount++;
                    if (json_data[i] == ']') bracketCount--;
                    if (bracketCount == 0) {
                        arrayEnd = i;
                        break;
                    }
                }
                
                if (arrayEnd > arrayStart) {
                    LOG_DEBUG("Found assets array end at position " << arrayEnd << " (length: " << (arrayEnd - arrayStart + 1) << " bytes)");
                    std::string assetsJson = json_data.substr(arrayStart, arrayEnd - arrayStart + 1);
                    std::string modifiedAssets = assetsJson;
                    
                    // Process each asset
                    for (const auto& [assetId, imagePath] : imagePaths) {
                        LOG_DEBUG("Processing image override for asset ID: " << assetId << " -> " << imagePath);
                        
                        // Validate image path exists
                        if (imagePath.empty()) {
                            LOG_CERR("[WARNING] Empty image path provided for asset ID: " << assetId) << std::endl;
                            continue;
                        }
                        
                        // Check if path exists (if it's an absolute or relative file path)
                        std::filesystem::path pathObj(imagePath);
                        // Check if path is a data URI or URL (C++17 compatible - no starts_with)
                        bool isDataUri = (imagePath.length() >= 5 && imagePath.substr(0, 5) == "data:");
                        bool isHttpUrl = (imagePath.length() >= 7 && imagePath.substr(0, 7) == "http://");
                        bool isHttpsUrl = (imagePath.length() >= 8 && imagePath.substr(0, 8) == "https://");
                        if (!pathObj.empty() && !isDataUri && !isHttpUrl && !isHttpsUrl) {
                            if (std::filesystem::exists(pathObj)) {
                                LOG_DEBUG("Image file exists: " << imagePath);
                                if (std::filesystem::is_regular_file(pathObj)) {
                                    auto fileSize = std::filesystem::file_size(pathObj);
                                    LOG_DEBUG("Image file size: " << fileSize << " bytes");
                                } else {
                                    LOG_CERR("[WARNING] Image path is not a regular file: " << imagePath) << std::endl;
                                }
                            } else {
                                LOG_CERR("[WARNING] Image file does not exist: " << imagePath << " (will attempt to load at runtime)") << std::endl;
                            }
                        } else {
                            LOG_DEBUG("Image path appears to be data URI or URL: " << (imagePath.length() > 50 ? imagePath.substr(0, 50) + "..." : imagePath));
                        }
                        
                        // Find asset by ID
                        std::string idPattern = "\"id\"\\s*:\\s*\"" + assetId + "\"";
                        std::regex idRegex(idPattern);
                        std::smatch idMatch;
                        
                        if (std::regex_search(modifiedAssets, idMatch, idRegex)) {
                            size_t assetStart = idMatch.position(0);
                            LOG_DEBUG("Found asset with ID " << assetId << " at position " << assetStart);
                            // Find the asset object boundaries
                            size_t objStart = modifiedAssets.rfind('{', assetStart);
                            if (objStart != std::string::npos) {
                                int objBraceCount = 0;
                                size_t objEnd = objStart;
                                for (size_t i = objStart; i < modifiedAssets.length(); i++) {
                                    if (modifiedAssets[i] == '{') objBraceCount++;
                                    if (modifiedAssets[i] == '}') objBraceCount--;
                                    if (objBraceCount == 0) {
                                        objEnd = i;
                                        break;
                                    }
                                }
                                
                                if (objEnd > objStart) {
                                    std::string assetObj = modifiedAssets.substr(objStart, objEnd - objStart + 1);
                                    LOG_DEBUG("Extracted asset object for ID " << assetId << " (length: " << (objEnd - objStart + 1) << " bytes)");
                                    
                                    // Split image path into u (directory) and p (filename)
                                    std::filesystem::path pathObj(imagePath);
                                    std::string dir = pathObj.parent_path().string();
                                    std::string filename = pathObj.filename().string();
                                    
                                    LOG_DEBUG("Split image path - directory: \"" << dir << "\", filename: \"" << filename << "\"");
                                    
                                    // Normalize directory path (ensure it ends with / for relative paths)
                                    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
                                        dir += "/";
                                    }
                                    if (dir == "/" || dir == "\\") {
                                        dir = "";  // Root path means empty directory
                                    }
                                    
                                    // Update u and p properties
                                    std::regex uPattern("\"u\"\\s*:\\s*\"[^\"]*\"");
                                    std::regex pPattern("\"p\"\\s*:\\s*\"[^\"]*\"");
                                    
                                    std::string newU = "\"u\":\"" + dir + "\"";
                                    std::string newP = "\"p\":\"" + filename + "\"";
                                    
                                    assetObj = std::regex_replace(assetObj, uPattern, newU);
                                    assetObj = std::regex_replace(assetObj, pPattern, newP);
                                    
                                    // Replace the asset object in modifiedAssets
                                    modifiedAssets.replace(objStart, objEnd - objStart + 1, assetObj);
                                    LOG_DEBUG("Updated asset " << assetId << ": u=\"" << dir << "\", p=\"" << filename << "\"");
                                    LOG_DEBUG("Image override applied successfully for asset ID: " << assetId);
                                } else {
                                    LOG_CERR("[WARNING] Failed to find asset object boundaries for asset ID: " << assetId) << std::endl;
                                }
                            } else {
                                LOG_CERR("[WARNING] Failed to find asset object start for asset ID: " << assetId) << std::endl;
                            }
                        } else {
                            LOG_CERR("[WARNING] Asset ID " << assetId << " not found in assets array") << std::endl;
                        }
                    }
                    
                    // Replace assets array in json_data
                    json_data.replace(arrayStart, arrayEnd - arrayStart + 1, modifiedAssets);
                    LOG_DEBUG("Assets array updated in JSON (replaced " << (arrayEnd - arrayStart + 1) << " bytes)");
                } else {
                    LOG_CERR("[WARNING] Failed to find valid assets array boundaries") << std::endl;
                }
            } else {
                LOG_CERR("[WARNING] Assets array start bracket not found") << std::endl;
            }
        } else {
            LOG_CERR("[WARNING] Assets array not found in JSON - image overrides will not be applied") << std::endl;
        }
    }
    
    if (layerOverrides.empty()) {
        LOG_DEBUG("No text layer overrides found in config file");
        return json_data;
    }
    
    LOG_DEBUG("Found " << layerOverrides.size() << " text layer overrides");
    
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
        FcInit(); // Initialize fontconfig
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
    
    for (const auto& [layerName, config] : layerOverrides) {
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
        
        // Apply padding to target width to prevent text from touching edges
        // textPadding: 0.97 means 97% of target width (3% padding, 1.5% per side)
        float paddedTargetWidth = targetWidth * textPadding;
        LOG_DEBUG("  Padded target width: " << paddedTargetWidth << " (" << (textPadding * 100.0f) << "% of " << targetWidth << ")");
        
        // Calculate optimal font size
        float optimalSize = calculateOptimalFontSize(
            tempFontMgr.get(),
            fontInfo,
            config,
            textToUse,
            paddedTargetWidth,
            textMeasurementMode
        );
        
        float finalWidth = 0.0f;
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

