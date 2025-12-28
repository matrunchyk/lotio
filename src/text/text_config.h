#ifndef TEXT_CONFIG_H
#define TEXT_CONFIG_H

#include <string>
#include <map>

// Text configuration (for auto-fit and dynamic text values)
struct TextLayerConfig {
    float minSize;
    float maxSize;
    std::string fallbackText;
    std::string textValue;  // Optional: text to set
    float textBoxWidth;     // Optional: override text box width (0 = use from JSON or animation width)
};

// Parse text-config.json
std::map<std::string, TextLayerConfig> parseTextConfig(const std::string& configPath);

// Simple JSON value extractors (for config.json)
std::string extractJsonString(const std::string& json, const std::string& key);
float extractJsonFloat(const std::string& json, const std::string& key);

#endif // TEXT_CONFIG_H

