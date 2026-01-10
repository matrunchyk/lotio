#include "layer_overrides.h"
#include "../utils/string_utils.h"
#include "../utils/logging.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontScanner_FreeType.h"
#ifndef __EMSCRIPTEN__
#include "include/ports/SkFontMgr_fontconfig.h"
#include <fontconfig/fontconfig.h>
#endif
#include <nlohmann/json.hpp>
#include <fstream>
#include <regex>
#include <iostream>
#include <filesystem>

std::string extractJsonString(const std::string& json, const std::string& key) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        return match[1].str();
    }
    return "";
}

float extractJsonFloat(const std::string& json, const std::string& key) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*([0-9]+\\.?[0-9]*)");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        try {
            return std::stof(match[1].str());
        } catch (...) {
            return 0.0f;
        }
    }
    return 0.0f;
}

bool validateTextLayerConfig(const std::string& layerName, const LayerOverride& config, std::string& errorMsg) {
    // Validate minSize and maxSize if both specified
    if (config.minSize > 0 && config.maxSize > 0) {
        if (config.maxSize <= config.minSize) {
            errorMsg = "maxSize (" + std::to_string(config.maxSize) + ") must be greater than minSize (" + std::to_string(config.minSize) + ") for layer: " + layerName;
            return false;
        }
    }
    
    // Validate non-zero values
    if (config.minSize < 0) {
        errorMsg = "minSize cannot be negative for layer: " + layerName;
        return false;
    }
    if (config.maxSize < 0) {
        errorMsg = "maxSize cannot be negative for layer: " + layerName;
        return false;
    }
    if (config.textBoxWidth < 0) {
        errorMsg = "textBoxWidth cannot be negative for layer: " + layerName;
        return false;
    }
    
    return true;
}

bool validateImageLayerConfig(const std::string& assetId, const ImageLayerOverride& config, const std::string& configPath, std::string& errorMsg) {
    // Get the layer-overrides file's parent directory for resolving relative paths
    std::filesystem::path overridesPath(configPath);
    std::filesystem::path overridesBaseDir = overridesPath.has_parent_path() 
        ? overridesPath.parent_path() 
        : std::filesystem::path(".");
    std::error_code ec;
    std::filesystem::path absOverridesBaseDir = std::filesystem::absolute(overridesBaseDir, ec);
    const std::string overridesBaseDirStr = (ec ? overridesBaseDir.string() : absOverridesBaseDir.string());
    
    // Check if fileName is empty and filePath is empty - invalid
    if (config.fileName.empty() && config.filePath.empty()) {
        errorMsg = "Both fileName and filePath are empty for asset ID: " + assetId;
        return false;
    }
    
    // Check for URLs (not supported)
    bool isHttpUrl = (config.filePath.length() >= 7 && config.filePath.substr(0, 7) == "http://");
    bool isHttpsUrl = (config.filePath.length() >= 8 && config.filePath.substr(0, 8) == "https://");
    if (isHttpUrl || isHttpsUrl) {
        errorMsg = "URLs are not supported in filePath for asset ID: " + assetId;
        return false;
    }
    
    // Determine the full path to validate
    std::string fullPath;
    if (config.filePath.empty() && !config.fileName.empty()) {
        // filePath is empty string, fileName contains full path
        fullPath = config.fileName;
    } else if (!config.filePath.empty() && !config.fileName.empty()) {
        // Both specified, combine them
        std::filesystem::path pathObj(config.filePath);
        if (pathObj.is_absolute()) {
            fullPath = config.filePath;
            // Ensure path ends with separator if needed
            if (fullPath.back() != '/' && fullPath.back() != '\\') {
                fullPath += "/";
            }
            fullPath += config.fileName;
        } else {
            // Relative path: resolve relative to layer-overrides.json directory
            std::filesystem::path resolvedPath = std::filesystem::path(overridesBaseDirStr) / config.filePath;
            std::error_code resolveEc;
            std::filesystem::path absResolvedPath = std::filesystem::absolute(resolvedPath, resolveEc);
            std::string resolvedDir = (resolveEc ? resolvedPath.string() : absResolvedPath.string());
            if (resolvedDir.back() != '/' && resolvedDir.back() != '\\') {
                resolvedDir += "/";
            }
            fullPath = resolvedDir + config.fileName;
        }
    } else if (!config.filePath.empty() && config.fileName.empty()) {
        // Only filePath specified - this is valid (will use default fileName from assets[].p)
        return true;
    }
    
    // Validate file exists (if both fileName and filePath are specified, or if fileName is a full path)
    if (!fullPath.empty()) {
        std::filesystem::path fullPathObj(fullPath);
        std::error_code pathEc;
        std::filesystem::path absFullPath = std::filesystem::absolute(fullPathObj, pathEc);
        std::string finalPath = (pathEc ? fullPath : absFullPath.string());
        
        if (!std::filesystem::exists(finalPath)) {
            errorMsg = "Image file does not exist: " + finalPath + " for asset ID: " + assetId;
            return false;
        }
        if (!std::filesystem::is_regular_file(finalPath)) {
            errorMsg = "Image path is not a regular file: " + finalPath + " for asset ID: " + assetId;
            return false;
        }
    }
    
    return true;
}

bool validateFontExists(const std::string& fontName, const std::string& dataJsonPath, std::string& errorMsg) {
#ifndef __EMSCRIPTEN__
    // Check system fonts via fontconfig (not available in WASM)
    try {
        FcInit();
        auto scanner = SkFontScanner_Make_FreeType();
        if (scanner) {
            auto fontMgr = SkFontMgr_New_FontConfig(nullptr, std::move(scanner));
            if (fontMgr) {
                // Try to find font by name
                auto typeface = fontMgr->matchFamilyStyle(fontName.c_str(), SkFontStyle::Normal());
                if (typeface) {
                    return true;  // Found in system fonts
                }
                // Try legacy method
                typeface = fontMgr->legacyMakeTypeface(fontName.c_str(), SkFontStyle::Normal());
                if (typeface) {
                    return true;  // Found in system fonts
                }
            }
        }
    } catch (...) {
        // Fontconfig not available, continue to check fonts directory
    }
#endif
    
    // Check fonts directory (relative to data.json or working directory)
    std::string fontFileName = fontName + ".ttf";
    
    // First, try relative to data.json directory
    if (!dataJsonPath.empty()) {
        std::filesystem::path dataPath(dataJsonPath);
        std::filesystem::path dataDir = dataPath.has_parent_path() 
            ? dataPath.parent_path() 
            : std::filesystem::path(".");
        
        std::filesystem::path fontPath = dataDir / "fonts" / fontFileName;
        if (std::filesystem::exists(fontPath) && std::filesystem::is_regular_file(fontPath)) {
            return true;
        }
    }
    
    // Try current working directory
    std::filesystem::path cwdFontPath = std::filesystem::path("fonts") / fontFileName;
    if (std::filesystem::exists(cwdFontPath) && std::filesystem::is_regular_file(cwdFontPath)) {
        return true;
    }
    
    // Try absolute fonts directory
    std::filesystem::path absFontPath = std::filesystem::path("/usr/local/share/fonts") / fontFileName;
    if (std::filesystem::exists(absFontPath) && std::filesystem::is_regular_file(absFontPath)) {
        return true;
    }
    
    errorMsg = "Font file not found: " + fontFileName + " (checked system fonts and fonts directories)";
    return false;
}

std::map<std::string, LayerOverride> parseLayerOverrides(const std::string& configPath) {
    std::map<std::string, LayerOverride> configs;
    
    if (configPath.empty()) {
        return configs;
    }
    
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return configs;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        file.close();
        
        // Parse textLayers section
        if (j.contains("textLayers") && j["textLayers"].is_object()) {
            for (auto& [layerName, layerConfig] : j["textLayers"].items()) {
                LayerOverride config;
                
                // Extract optional fields
                if (layerConfig.contains("minSize") && layerConfig["minSize"].is_number()) {
                    config.minSize = layerConfig["minSize"].get<float>();
                } else {
                    config.minSize = 0.0f;
                }
                
                if (layerConfig.contains("maxSize") && layerConfig["maxSize"].is_number()) {
                    config.maxSize = layerConfig["maxSize"].get<float>();
                } else {
                    config.maxSize = 0.0f;
                }
                
                if (layerConfig.contains("fallbackText") && layerConfig["fallbackText"].is_string()) {
                    config.fallbackText = layerConfig["fallbackText"].get<std::string>();
                } else {
                    config.fallbackText = "";
                }
                
                if (layerConfig.contains("textBoxWidth") && layerConfig["textBoxWidth"].is_number()) {
                    config.textBoxWidth = layerConfig["textBoxWidth"].get<float>();
                } else {
                    config.textBoxWidth = 0.0f;
                }
                
                if (layerConfig.contains("value") && layerConfig["value"].is_string()) {
                    config.value = layerConfig["value"].get<std::string>();
                } else {
                    config.value = "";
                }
                
                // Handle \u0003 (ETX) - convert to \r for Lottie newlines
                replaceAllInPlace(config.value, "\\u0003", "\r");
                replaceCharInPlace(config.value, '\x03', '\r');
                
                // Validate the config
                std::string errorMsg;
                if (!validateTextLayerConfig(layerName, config, errorMsg)) {
                    LOG_CERR("[ERROR] " << errorMsg) << std::endl;
                    continue;  // Skip invalid config
                }
                
                configs[layerName] = config;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_CERR("[ERROR] Failed to parse layer-overrides.json: " << e.what()) << std::endl;
    }
    
    return configs;
}

std::map<std::string, ImageLayerOverride> parseImageLayers(const std::string& configPath) {
    std::map<std::string, ImageLayerOverride> imageLayers;
    
    if (configPath.empty()) {
        return imageLayers;
    }
    
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "[WARNING] Could not open layer overrides file for image layers: " << configPath << std::endl;
        return imageLayers;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        file.close();
        
        // Parse imageLayers section
        if (j.contains("imageLayers") && j["imageLayers"].is_object()) {
            for (auto& [assetId, layerConfig] : j["imageLayers"].items()) {
                if (assetId.empty()) {
                    std::cerr << "[WARNING] Empty asset ID found in imageLayers, skipping" << std::endl;
                    continue;
                }
                
                ImageLayerOverride config;
                
                // Extract optional fields
                if (layerConfig.contains("filePath") && layerConfig["filePath"].is_string()) {
                    config.filePath = layerConfig["filePath"].get<std::string>();
                } else {
                    config.filePath = "";
                }
                
                if (layerConfig.contains("fileName") && layerConfig["fileName"].is_string()) {
                    config.fileName = layerConfig["fileName"].get<std::string>();
                } else {
                    config.fileName = "";
                }
                
                // Validate the config
                std::string errorMsg;
                if (!validateImageLayerConfig(assetId, config, configPath, errorMsg)) {
                    LOG_CERR("[ERROR] " << errorMsg) << std::endl;
                    continue;  // Skip invalid config
                }
                
                imageLayers[assetId] = config;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_CERR("[ERROR] Failed to parse layer-overrides.json for image layers: " << e.what()) << std::endl;
    }
    
    return imageLayers;
}
