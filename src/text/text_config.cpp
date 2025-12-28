#include "text_config.h"
#include "../utils/string_utils.h"
#include "../utils/logging.h"
#include <fstream>
#include <regex>
#include <iostream>

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

std::map<std::string, TextLayerConfig> parseTextConfig(const std::string& configPath) {
    std::map<std::string, TextLayerConfig> configs;
    
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return configs;
    }
    
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    // Extract textLayers section - find the start of the object
    size_t textLayersPos = json.find("\"textLayers\"");
    if (textLayersPos != std::string::npos) {
        // Find the opening brace after "textLayers"
        size_t openBrace = json.find('{', textLayersPos);
        if (openBrace != std::string::npos) {
            // Find matching closing brace by counting braces
            int braceCount = 0;
            size_t closeBrace = openBrace;
            for (size_t i = openBrace; i < json.length(); i++) {
                if (json[i] == '{') braceCount++;
                if (json[i] == '}') braceCount--;
                if (braceCount == 0) {
                    closeBrace = i;
                    break;
                }
            }
            
            if (closeBrace > openBrace) {
                std::string layersJson = json.substr(openBrace + 1, closeBrace - openBrace - 1);
                
                // Find each layer config - each layer is "name": { ... }
                std::regex layerPattern("\"([^\"]+)\"\\s*:\\s*\\{");
                std::sregex_iterator iter(layersJson.begin(), layersJson.end(), layerPattern);
                std::sregex_iterator end;
                
                for (; iter != end; ++iter) {
                    std::smatch match = *iter;
                    std::string layerName = match[1].str();
                    size_t layerStart = match.position(0) + match.length(0) - 1; // Position of opening brace
                    
                    // Find the matching closing brace for this layer
                    int layerBraceCount = 0;
                    size_t layerEnd = layerStart;
                    for (size_t i = layerStart; i < layersJson.length(); i++) {
                        if (layersJson[i] == '{') layerBraceCount++;
                        if (layersJson[i] == '}') layerBraceCount--;
                        if (layerBraceCount == 0) {
                            layerEnd = i;
                            break;
                        }
                    }
                    
                    if (layerEnd > layerStart) {
                        std::string layerConfig = layersJson.substr(layerStart + 1, layerEnd - layerStart - 1);
                        
                        TextLayerConfig config;
                        config.minSize = extractJsonFloat(layerConfig, "minSize");
                        config.maxSize = extractJsonFloat(layerConfig, "maxSize");
                        config.fallbackText = extractJsonString(layerConfig, "fallbackText");
                        config.textBoxWidth = extractJsonFloat(layerConfig, "textBoxWidth");  // Optional override
                        config.textValue = "";  // Will be set from textValues section if present
                        
                        configs[layerName] = config;
                    }
                }
            }
        }
    }
    
    // Extract textValues section - similar approach
    size_t textValuesPos = json.find("\"textValues\"");
    if (textValuesPos != std::string::npos) {
        size_t openBrace = json.find('{', textValuesPos);
        if (openBrace != std::string::npos) {
            int braceCount = 0;
            size_t closeBrace = openBrace;
            for (size_t i = openBrace; i < json.length(); i++) {
                if (json[i] == '{') braceCount++;
                if (json[i] == '}') braceCount--;
                if (braceCount == 0) {
                    closeBrace = i;
                    break;
                }
            }
            
            if (closeBrace > openBrace) {
                std::string valuesJson = json.substr(openBrace + 1, closeBrace - openBrace - 1);
                
                std::regex valuePattern("\"([^\"]+)\"\\s*:\\s*\"([^\"]+)\"");
                std::sregex_iterator iter(valuesJson.begin(), valuesJson.end(), valuePattern);
                std::sregex_iterator end;
                
                for (; iter != end; ++iter) {
                    std::smatch match = *iter;
                    std::string layerName = match[1].str();
                    std::string textValue = match[2].str();
                    
                    // Handle \u0003 (ETX) - convert to \r for Lottie newlines
                    // Also handle literal \u0003 in the JSON string
                    replaceAllInPlace(textValue, "\\u0003", "\r");
                    // Also handle if it's already a literal character (shouldn't happen in JSON, but be safe)
                    replaceCharInPlace(textValue, '\x03', '\r');
                    
                    if (configs.find(layerName) != configs.end()) {
                        configs[layerName].textValue = textValue;
                    }
                }
            }
        }
    }
    
    return configs;
}

