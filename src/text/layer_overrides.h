#ifndef LAYER_OVERRIDES_H
#define LAYER_OVERRIDES_H

#include <string>
#include <map>

// Text layer override configuration
struct LayerOverride {
    float minSize;          // Optional: minimum font size (0 = not specified, no auto-fit)
    float maxSize;          // Optional: maximum font size (0 = not specified, no auto-fit)
    std::string fallbackText; // Optional: fallback text if text doesn't fit (defaults to empty)
    float textBoxWidth;     // Optional: text box width (0 = use from JSON or animation width)
    std::string value;      // Optional: text value to set (defaults to original text from data.json)
};

// Image layer override configuration
struct ImageLayerOverride {
    std::string filePath;   // Optional: directory path (defaults to assets[].u, empty string = use full path from fileName)
    std::string fileName;   // Optional: filename (defaults to assets[].p)
};

// Parse layer-overrides.json
std::map<std::string, LayerOverride> parseLayerOverrides(const std::string& configPath);

// Parse image layers from layer-overrides.json
std::map<std::string, ImageLayerOverride> parseImageLayers(const std::string& configPath);

// Validation functions
bool validateTextLayerConfig(const std::string& layerName, const LayerOverride& config, std::string& errorMsg);
bool validateImageLayerConfig(const std::string& assetId, const ImageLayerOverride& config, const std::string& configPath, std::string& errorMsg);
bool validateFontExists(const std::string& fontName, const std::string& dataJsonPath, std::string& errorMsg);

// Simple JSON value extractors (for config.json)
std::string extractJsonString(const std::string& json, const std::string& key);
float extractJsonFloat(const std::string& json, const std::string& key);

#endif // LAYER_OVERRIDES_H

