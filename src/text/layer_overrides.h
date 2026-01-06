#ifndef LAYER_OVERRIDES_H
#define LAYER_OVERRIDES_H

#include <string>
#include <map>

// Layer override configuration (for text auto-fit, dynamic text values, and image path overrides)
struct LayerOverride {
    float minSize;
    float maxSize;
    std::string fallbackText;
    std::string textValue;  // Optional: text to set
    float textBoxWidth;     // Optional: override text box width (0 = use from JSON or animation width)
    std::string imagePath;  // Optional: image path override (empty = use data.json default)
};

// Parse layer-overrides.json
std::map<std::string, LayerOverride> parseLayerOverrides(const std::string& configPath);

// Parse image paths from layer-overrides.json
std::map<std::string, std::string> parseImagePaths(const std::string& configPath);

// Simple JSON value extractors (for config.json)
std::string extractJsonString(const std::string& json, const std::string& key);
float extractJsonFloat(const std::string& json, const std::string& key);

#endif // LAYER_OVERRIDES_H

