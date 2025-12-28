// Render all frames of a Lottie animation to PNG and/or WebP files (frame-by-frame)
// This is used as input for video encoding with ffmpeg
// Usage: lotio [--png] [--webp] <input.json> <output_dir> [fps]

#include "modules/skottie/include/Skottie.h"
#include "modules/skresources/include/SkResources.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkImage.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"
#include "include/codec/SkCodec.h"
#include "include/codec/SkPngDecoder.h"
#include "include/encode/SkPngEncoder.h"
#include "include/encode/SkWebpEncoder.h"
#include "include/core/SkStream.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontScanner.h"
#include "include/ports/SkFontScanner_FreeType.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include <fontconfig/fontconfig.h>
#include <cstring>
#include <iostream>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <string>
#include <algorithm>
#include <filesystem>
#include <ctime>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <regex>

// Get current timestamp as string in format [YYYY-MM-DD HH:MM:SS.nnnnnnnnn]
static std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()) % 1000000000;
    
    std::tm tm_buf;
    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
    #else
        localtime_r(&time_t, &tm_buf);
    #endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(9) << ns.count();
    return oss.str();
}

// Global flags
static bool g_stream_mode = false;
static bool g_debug_mode = false;

// Helper macros for timestamped output
// In stream mode, LOG_COUT uses stderr to avoid corrupting stdout PNG data
#define LOG_COUT(msg) (g_stream_mode ? std::cerr : std::cout) << "[" << getTimestamp() << "] " << msg
#define LOG_CERR(msg) std::cerr << "[" << getTimestamp() << "] " << msg
#define LOG_DEBUG(msg) if (g_debug_mode) { LOG_COUT("[DEBUG] " << msg) << std::endl; }

// Helper functions (defined early so they can be used by other functions)
static size_t replaceAllInPlace(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return 0;
    size_t count = 0;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
        count++;
    }
    return count;
}

static size_t replaceCharInPlace(std::string& s, char from, char to) {
    size_t count = 0;
    for (auto& ch : s) {
        if (ch == from) {
            ch = to;
            count++;
        }
    }
    return count;
}

// Escape special regex characters
static std::string escapeRegex(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '\\' || c == '^' || c == '$' || c == '.' || c == '|' || 
            c == '?' || c == '*' || c == '+' || c == '(' || c == ')' || 
            c == '[' || c == '{') {
            result += '\\';
        }
        result += c;
    }
    return result;
}

// Text configuration (for auto-fit and dynamic text values)
struct TextLayerConfig {
    float minSize;
    float maxSize;
    std::string fallbackText;
    std::string textValue;  // Optional: text to set
    float textBoxWidth;     // Optional: override text box width (0 = use from JSON or animation width)
};

// Simple JSON value extractor (for config.json)
static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        return match[1].str();
    }
    return "";
}

static float extractJsonFloat(const std::string& json, const std::string& key) {
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

// Parse text-config.json
static std::map<std::string, TextLayerConfig> parseTextConfig(const std::string& configPath) {
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
                
                size_t lastPos = 0;
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

// Get font style from Lottie font info
static SkFontStyle getSkFontStyle(const std::string& styleStr) {
    if (styleStr.find("Bold") != std::string::npos && styleStr.find("Italic") != std::string::npos) {
        return SkFontStyle::BoldItalic();
    } else if (styleStr.find("Bold") != std::string::npos) {
        return SkFontStyle::Bold();
    } else if (styleStr.find("Italic") != std::string::npos) {
        return SkFontStyle::Italic();
    }
    return SkFontStyle::Normal();
}

// Measure text width with given font
static SkScalar measureTextWidth(
    SkFontMgr* fontMgr,
    const std::string& fontFamily,
    const std::string& fontStyle,
    const std::string& fontName,  // Full name like "SegoeUI-Bold"
    float fontSize,
    const std::string& text
) {
    // Try to get typeface using family and style
    sk_sp<SkTypeface> typeface = fontMgr->matchFamilyStyle(
        fontFamily.c_str(),
        getSkFontStyle(fontStyle)
    );
    
    // Fallback: try with full font name
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle(fontName.c_str(), SkFontStyle::Normal());
    }
    
    // Last resort: try legacy method
    if (!typeface) {
        typeface = fontMgr->legacyMakeTypeface(fontName.c_str(), SkFontStyle::Normal());
    }
    
    if (!typeface) {
        LOG_DEBUG("Warning: Could not find typeface for " << fontName << ", using default");
        typeface = fontMgr->legacyMakeTypeface(nullptr, SkFontStyle::Normal());
    }
    
    SkFont font(typeface, fontSize);
    
    // Split text by newlines (\r or \n) and measure each line
    // Return the width of the longest line
    SkScalar maxWidth = 0.0f;
    std::string currentLine;
    
    for (size_t i = 0; i <= text.length(); i++) {
        if (i == text.length() || text[i] == '\r' || text[i] == '\n') {
            // Measure current line
            if (!currentLine.empty()) {
                SkRect bounds;
                font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
                maxWidth = std::max(maxWidth, bounds.width());
                if (g_debug_mode && (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)) {
                    LOG_COUT("[DEBUG] Measured line: \"" << currentLine << "\" width: " << bounds.width()) << std::endl;
                }
            }
            currentLine.clear();
            
            // Skip \r\n combination
            if (i < text.length() && text[i] == '\r' && i + 1 < text.length() && text[i + 1] == '\n') {
                i++;  // Skip the \n
            }
        } else {
            currentLine += text[i];
        }
    }
    
    // Handle last line if text doesn't end with newline
    if (!currentLine.empty()) {
        SkRect bounds;
        font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
        maxWidth = std::max(maxWidth, bounds.width());
        if (g_debug_mode && (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)) {
            LOG_COUT("[DEBUG] Measured line: \"" << currentLine << "\" width: " << bounds.width()) << std::endl;
        }
    }
    
    if (g_debug_mode && (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)) {
        LOG_COUT("[DEBUG] Multiline text - longest line width: " << maxWidth) << std::endl;
    }
    
    return maxWidth;
}

// Extract font info from Lottie JSON for a text layer
struct FontInfo {
    std::string family;
    std::string style;
    std::string name;
    float size;
    std::string text;
    float textBoxWidth;  // From sz[0] if available
};

static FontInfo extractFontInfoFromJson(const std::string& json, const std::string& layerName) {
    FontInfo info;
    
    // Find the layer by name - try multiple approaches
    size_t layerNamePos = std::string::npos;
    
    // Approach 1: Use regex with escaped name
    std::string escapedLayerName = escapeRegex(layerName);
    std::string layerNamePattern = "\"nm\"\\s*:\\s*\"" + escapedLayerName + "\"";
    std::regex layerNameRegex(layerNamePattern);
    std::smatch layerMatch;
    
    if (std::regex_search(json, layerMatch, layerNameRegex)) {
        layerNamePos = layerMatch.position(0);
    } else {
        // Approach 2: Use regex without escaping
        std::string simplePattern = "\"nm\"\\s*:\\s*\"" + layerName + "\"";
        std::regex simpleRegex(simplePattern);
        if (std::regex_search(json, layerMatch, simpleRegex)) {
            layerNamePos = layerMatch.position(0);
        } else {
            // Approach 3: Simple string search - find "nm" then check the name
            size_t nmPos = json.find("\"nm\"");
            while (nmPos != std::string::npos && nmPos < json.length() - 100) {
                // Find the quoted name after "nm"
                size_t colonPos = json.find(':', nmPos);
                if (colonPos != std::string::npos && colonPos < nmPos + 20) {
                    size_t nameStart = json.find('"', colonPos);
                    if (nameStart != std::string::npos) {
                        size_t nameEnd = json.find('"', nameStart + 1);
                        if (nameEnd != std::string::npos) {
                            std::string foundName = json.substr(nameStart + 1, nameEnd - nameStart - 1);
                            if (foundName == layerName) {
                                layerNamePos = nmPos;
                                break;
                            }
                        }
                    }
                }
                nmPos = json.find("\"nm\"", nmPos + 1);
            }
        }
    }
    
    if (layerNamePos == std::string::npos) {
        LOG_DEBUG("Layer name not found: " << layerName);
        return info;
    }
    
    // Check if this is a text layer (ty:5) - search in a window around the layer name
    // The "ty" field could be before or after "nm" in the JSON
    size_t tySearchStart = (layerNamePos > 1000) ? layerNamePos - 1000 : 0;
    size_t tySearchEnd = std::min(layerNamePos + 3000, json.length());
    std::string layerSection = json.substr(tySearchStart, tySearchEnd - tySearchStart);
    size_t relativeLayerNamePos = layerNamePos - tySearchStart;
    
    // Check for text layer type - look for "ty":5 in the layer section
    // Try to find "ty" that's reasonably close to the layer name (within the same object)
    bool isTextLayer = false;
    
    // Search for "ty" before the layer name
    size_t tySearchOffset = (relativeLayerNamePos > 200) ? relativeLayerNamePos - 200 : 0;
    size_t tyPos = layerSection.find("\"ty\"", tySearchOffset);
    
    // Also search after the layer name
    if (tyPos == std::string::npos || tyPos > relativeLayerNamePos + 500) {
        tyPos = layerSection.find("\"ty\"", relativeLayerNamePos);
    }
    
    if (tyPos != std::string::npos) {
        // Check if followed by :5
        size_t colonPos = layerSection.find(':', tyPos);
        if (colonPos != std::string::npos && colonPos < tyPos + 30) {
            // Skip whitespace and check for 5
            size_t numStart = colonPos + 1;
            while (numStart < layerSection.length() && (layerSection[numStart] == ' ' || layerSection[numStart] == '\t' || layerSection[numStart] == '\n' || layerSection[numStart] == '\r')) {
                numStart++;
            }
            if (numStart < layerSection.length() && layerSection[numStart] == '5') {
                isTextLayer = true;
            }
        }
    }
    
    if (!isTextLayer) {
        LOG_DEBUG("Layer " << layerName << " found but not identified as text layer (ty:5)");
        return info;  // Not a text layer
    }
    
    // Find the "t" object (text data) - it should be after the layer name
    // Search within a reasonable window after the layer name
    size_t tSearchStart = layerNamePos;
    size_t tSearchEnd = std::min(layerNamePos + 5000, json.length());
    size_t textDataPos = json.find("\"t\"", tSearchStart);
    
    // Try multiple "t" occurrences - skip numeric "t" values (like "t": 110 in keyframes)
    // We want the "t" object that contains text data: "t": { "d": { "k": [ { "s": {
    while (textDataPos != std::string::npos && textDataPos < tSearchEnd) {
        // Check what follows "t" - if it's a colon followed by a number, skip it (it's a keyframe time)
        size_t colonPos = json.find(':', textDataPos);
        if (colonPos != std::string::npos && colonPos < textDataPos + 10) {
            // Skip whitespace after colon
            size_t valueStart = colonPos + 1;
            while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
                valueStart++;
            }
            
            // If followed by a digit, this is a numeric "t" (keyframe time), skip it
            if (valueStart < json.length() && std::isdigit(json[valueStart])) {
                textDataPos = json.find("\"t\"", textDataPos + 1);
                continue;
            }
            
            // If followed by '{', this is the text data object
            if (valueStart < json.length() && json[valueStart] == '{') {
                // Verify it contains "d" nearby (text data structure)
                size_t checkWindow = std::min(textDataPos + 100, json.length());
                std::string checkSection = json.substr(textDataPos, checkWindow - textDataPos);
                if (checkSection.find("\"d\"") != std::string::npos) {
                    break;  // Found the text data object
                }
            }
        }
        
        // Try next occurrence
        textDataPos = json.find("\"t\"", textDataPos + 1);
    }
    
    if (textDataPos == std::string::npos || textDataPos >= tSearchEnd) {
        LOG_DEBUG("Could not find \"t\" object for layer " << layerName);
        return info;
    }
    
    // Find the "s" object within the text data (text style)
    // The "s" object should be inside the "t" -> "d" -> "k" -> [0] -> "s" structure
    size_t textStylePos = json.find("\"s\"", textDataPos);
    if (textStylePos == std::string::npos || textStylePos > textDataPos + 1000) {
        LOG_DEBUG("Could not find \"s\" object for layer " << layerName);
        return info;
    }
    
    // Extract the text style object - find the opening brace and matching closing brace
    size_t styleOpenBrace = json.find('{', textStylePos);
    if (styleOpenBrace == std::string::npos) {
        return info;
    }
    
    // Find matching closing brace
    int braceCount = 0;
    size_t styleCloseBrace = styleOpenBrace;
    for (size_t i = styleOpenBrace; i < std::min(styleOpenBrace + 500, json.length()); i++) {
        if (json[i] == '{') braceCount++;
        if (json[i] == '}') braceCount--;
        if (braceCount == 0) {
            styleCloseBrace = i;
            break;
        }
    }
    
    if (styleCloseBrace > styleOpenBrace) {
        std::string textStyleJson = json.substr(styleOpenBrace, styleCloseBrace - styleOpenBrace + 1);
        
        // Extract font size
        std::regex sizePattern("\"s\"\\s*:\\s*([0-9]+\\.?[0-9]*)");
        std::smatch sizeMatch;
        if (std::regex_search(textStyleJson, sizeMatch, sizePattern)) {
            info.size = std::stof(sizeMatch[1].str());
        }
        
        // Extract font name
        std::regex fontPattern("\"f\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch fontMatch;
        if (std::regex_search(textStyleJson, fontMatch, fontPattern)) {
            info.name = fontMatch[1].str();
        }
        
        // Extract text content
        std::regex textPattern("\"t\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch textMatch;
        if (std::regex_search(textStyleJson, textMatch, textPattern)) {
            info.text = textMatch[1].str();
            // Handle escaped newlines in extracted text (\r, \n, \u0003)
            // Convert escaped sequences to actual characters (unescape)
            // Normalize all to \r for Lottie compatibility
            replaceAllInPlace(info.text, "\\r", "\r");
            replaceAllInPlace(info.text, "\\n", "\r");  // Convert \n to \r for Lottie
            replaceAllInPlace(info.text, "\\u0003", "\r");
            replaceCharInPlace(info.text, '\x03', '\r');
            // Also convert any existing \n to \r for consistency
            replaceCharInPlace(info.text, '\n', '\r');
        }
        
        // Extract text box size (sz)
        std::regex szPattern("\"sz\"\\s*:\\s*\\[\\s*([0-9]+\\.?[0-9]*)\\s*,\\s*([0-9]+\\.?[0-9]*)");
        std::smatch szMatch;
        if (std::regex_search(textStyleJson, szMatch, szPattern)) {
            info.textBoxWidth = std::stof(szMatch[1].str());
        }
    }
    
    // Extract font family and style from fonts.list
    std::regex fontsPattern("\"fonts\"\\s*:\\s*\\{[^}]*\"list\"\\s*:\\s*\\[([^\\]]+)\\]");
    std::smatch fontsMatch;
    if (std::regex_search(json, fontsMatch, fontsPattern)) {
        std::string fontsList = fontsMatch[1].str();
        std::string escapedFontName = escapeRegex(info.name);
        std::string fontDefPatternStr = "\\{[^}]*\"fName\"\\s*:\\s*\"" + escapedFontName + "\"[^}]*\"fFamily\"\\s*:\\s*\"([^\"]+)\"[^}]*\"fStyle\"\\s*:\\s*\"([^\"]+)\"";
        std::regex fontDefPattern(fontDefPatternStr);
        std::smatch fontDefMatch;
        if (std::regex_search(fontsList, fontDefMatch, fontDefPattern)) {
            info.family = fontDefMatch[1].str();
            info.style = fontDefMatch[2].str();
        }
    }
    
    return info;
}

// Calculate optimal font size for text to fit
static float calculateOptimalFontSize(
    SkFontMgr* fontMgr,
    const FontInfo& fontInfo,
    const TextLayerConfig& config,
    const std::string& text,
    float targetWidth
) {
    if (targetWidth <= 0) {
        return fontInfo.size;  // No constraint, use original size
    }
    
    // Measure with current size
    float currentSize = fontInfo.size;
    float currentWidth = measureTextWidth(fontMgr, fontInfo.family, fontInfo.style, 
                                         fontInfo.name, currentSize, text);
    
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
                                              fontInfo.name, testSize, text);
            
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
                                         fontInfo.name, config.minSize, text);
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
                                              fontInfo.name, testSize, text);
            
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
                                               fontInfo.name, bestSize, text);
            LOG_COUT("[DEBUG] calculateOptimalFontSize: reduced from " << currentSize 
                     << " to " << bestSize << " (width: " << finalWidth << " / " << targetWidth << ")") << std::endl;
        }
        
        return bestSize;
    }
}

// Adjust text animator position keyframes based on text width change
// For right-aligned text, when text is wider, we need to move it further left (more negative X)
static void adjustTextAnimatorPosition(
    std::string& json,
    const std::string& layerName,
    float widthDiff
) {
    if (std::abs(widthDiff) < 0.1f) {
        return;  // No significant change
    }
    
    // Find the layer by name
    std::string layerNamePattern = "\"nm\"\\s*:\\s*\"" + escapeRegex(layerName) + "\"";
    std::regex layerNameRegex(layerNamePattern);
    std::smatch layerMatch;
    
    if (!std::regex_search(json, layerMatch, layerNameRegex)) {
        return;  // Layer not found
    }
    
    size_t layerNamePos = layerMatch.position(0);
    
    // Find the text animator position property: t.a[0].a.p.k (array of keyframes)
    // Search for the position property in the text animator within a reasonable window
    size_t searchStart = layerNamePos;
    size_t searchEnd = std::min(layerNamePos + 15000, json.length());
    std::string layerSection = json.substr(searchStart, searchEnd - searchStart);
    
    // Find "p" property in text animator that has animated keyframes
    // Look for pattern: "p": {"a": 1, "k": [
    // We need to find the position property that's inside the text animator array
    size_t textDataPos = layerSection.find("\"t\"");
    if (textDataPos == std::string::npos) {
        return;  // No text data found
    }
    
    // Find animator array "a": [
    size_t animatorArrayPos = layerSection.find("\"a\"", textDataPos);
    if (animatorArrayPos == std::string::npos) {
        return;  // No animator array found
    }
    
    // Find the position property "p" with animated keyframes
    // Pattern: "p": {"a": 1, "k": [{"s": [-679, 0, 0], ...}, ...]}
    size_t pPos = layerSection.find("\"p\"", animatorArrayPos);
    while (pPos != std::string::npos && pPos < textDataPos + 5000) {
        // Check if this "p" is followed by animated keyframes
        size_t checkPos = pPos + 3;
        while (checkPos < layerSection.length() && (layerSection[checkPos] == ' ' || layerSection[checkPos] == '\t' || layerSection[checkPos] == ':')) {
            checkPos++;
        }
        
        // Look for "a": 1 pattern (animated)
        size_t aCheck = layerSection.find("\"a\"", pPos);
        if (aCheck != std::string::npos && aCheck < pPos + 50) {
            size_t colonPos = layerSection.find(':', aCheck);
            if (colonPos != std::string::npos) {
                size_t valueStart = colonPos + 1;
                while (valueStart < layerSection.length() && (layerSection[valueStart] == ' ' || layerSection[valueStart] == '\t')) {
                    valueStart++;
                }
                if (valueStart < layerSection.length() && layerSection[valueStart] == '1') {
                    // Found animated position property, now find and adjust keyframes
                    // Find keyframes array "k": [
                    size_t kPos = layerSection.find("\"k\"", pPos);
                    if (kPos != std::string::npos) {
                        size_t arrayStart = layerSection.find('[', kPos);
                        if (arrayStart != std::string::npos) {
                            // Find all "s": [x, y, z] patterns and adjust X values
                            // Search in the full json starting from the absolute position of arrayStart
                            size_t absoluteArrayStart = searchStart + arrayStart;
                            size_t arrayEnd = absoluteArrayStart;
                            
                            // Find the end of the keyframes array by counting brackets
                            int bracketCount = 0;
                            bool inArray = false;
                            for (size_t i = absoluteArrayStart; i < json.length() && i < absoluteArrayStart + 10000; i++) {
                                if (json[i] == '[') {
                                    bracketCount++;
                                    inArray = true;
                                } else if (json[i] == ']') {
                                    bracketCount--;
                                    if (bracketCount == 0 && inArray) {
                                        arrayEnd = i + 1;
                                        break;
                                    }
                                }
                            }
                            
                            if (arrayEnd > absoluteArrayStart) {
                                // Use regex to find and replace all negative X values in keyframes
                                // Pattern: "s": [-number, ...] - we want to replace the -number part
                                std::ostringstream widthDiffStr;
                                widthDiffStr << std::fixed << std::setprecision(1) << widthDiff;
                                
                                // Find all occurrences of negative X values in the keyframes array
                                std::string keyframeSection = json.substr(absoluteArrayStart, arrayEnd - absoluteArrayStart);
                                
                                // Use regex to find all "s": [-number, ...] patterns
                                std::regex negXPattern("\"s\"\\s*:\\s*\\[\\s*-([0-9]+\\.?[0-9]*)\\s*,");
                                std::sregex_iterator iter(keyframeSection.begin(), keyframeSection.end(), negXPattern);
                                std::sregex_iterator end;
                                
                                // Collect replacements (from end to start)
                                std::vector<std::pair<size_t, std::string>> replacements;
                                
                                for (; iter != end; ++iter) {
                                    std::smatch match = *iter;
                                    float x = -std::stof(match[1].str());  // Negative value (off-screen left)
                                    // widthDiff = newWidth - originalWidth
                                    // For right-aligned text that moves off-screen to the left:
                                    // - If text gets WIDER (widthDiff > 0): need to move further left (more negative)
                                    // - If text gets NARROWER (widthDiff < 0): can keep same position or move less left
                                    // Since the user is seeing the tail, the text is likely still too wide for the position
                                    // So we should move it further left regardless: newX = x - abs(widthDiff) when text is wider
                                    // But actually, if widthDiff is negative (narrower), x - widthDiff makes it less negative (wrong direction)
                                    // We want: if wider, move more left; if narrower, don't adjust (or adjust proportionally)
                                    float newX = x;
                                    if (widthDiff > 0.1f) {
                                        // Text got wider - move further left (more negative)
                                        newX = x - widthDiff;
                                    } else if (widthDiff < -0.1f) {
                                        // Text got narrower - but user still sees tail, so maybe we need to move it more left anyway?
                                        // Actually, if text is narrower, the tail shouldn't be visible unless position is wrong
                                        // For now, don't adjust if text got narrower
                                        newX = x;
                                    }
                                    
                                    std::ostringstream newValue;
                                    newValue << std::fixed << std::setprecision(1) << newX;
                                    
                                    // Find the position of the X value in the original json
                                    size_t relativePos = match.position(0) + match.str().find(match[1].str());
                                    size_t absolutePos = absoluteArrayStart + relativePos;
                                    
                                    // Replace just the number part
                                    size_t numStart = absolutePos;
                                    size_t numEnd = numStart + match[1].str().length();
                                    
                                    // Include the minus sign
                                    if (numStart > 0 && json[numStart - 1] == '-') {
                                        numStart--;
                                    }
                                    
                                    replacements.push_back({numStart, newValue.str()});
                                }
                                
                                // Apply replacements from end to start
                                for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
                                    size_t numStart = it->first;
                                    // Find the end of the number
                                    size_t numEnd = numStart;
                                    if (json[numEnd] == '-') numEnd++;
                                    while (numEnd < json.length() && (std::isdigit(json[numEnd]) || json[numEnd] == '.')) {
                                        numEnd++;
                                    }
                                    
                                    json.replace(numStart, numEnd - numStart, it->second);
                                    
                                    if (g_debug_mode) {
                                        LOG_COUT("[DEBUG] Replaced X value at " << numStart << ": -> " << it->second) << std::endl;
                                    }
                                }
                            }
                            
                            return;  // Found and adjusted, done
                        }
                    }
                }
            }
        }
        
        // Try next "p" occurrence
        pPos = layerSection.find("\"p\"", pPos + 1);
    }
}

// Modify JSON to update text layer
static void modifyTextLayerInJson(
    std::string& json,
    const std::string& layerName,
    const std::string& newText,
    float newSize
) {
    // Find the layer using the same approach as extractFontInfoFromJson
    size_t layerNamePos = std::string::npos;
    
    // Try regex first
    std::string simplePattern = "\"nm\"\\s*:\\s*\"" + layerName + "\"";
    std::regex simpleRegex(simplePattern);
    std::smatch layerMatch;
    
    if (std::regex_search(json, layerMatch, simpleRegex)) {
        layerNamePos = layerMatch.position(0);
    } else {
        // Fallback: simple string search
        size_t nmPos = json.find("\"nm\"");
        while (nmPos != std::string::npos && nmPos < json.length() - 100) {
            size_t colonPos = json.find(':', nmPos);
            if (colonPos != std::string::npos && colonPos < nmPos + 20) {
                size_t nameStart = json.find('"', colonPos);
                if (nameStart != std::string::npos) {
                    size_t nameEnd = json.find('"', nameStart + 1);
                    if (nameEnd != std::string::npos) {
                        std::string foundName = json.substr(nameStart + 1, nameEnd - nameStart - 1);
                        if (foundName == layerName) {
                            layerNamePos = nmPos;
                            break;
                        }
                    }
                }
            }
            nmPos = json.find("\"nm\"", nmPos + 1);
        }
    }
    
    if (layerNamePos == std::string::npos) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find layer name in JSON: " << layerName) << std::endl;
        }
        return;
    }
    
    // Find the "t" object (text data)
    size_t searchWindow = std::min(layerNamePos + 5000, json.length());
    size_t textDataPos = json.find("\"t\"", layerNamePos);
    if (textDataPos == std::string::npos || textDataPos > searchWindow) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find \"t\" object for layer: " << layerName) << std::endl;
        }
        return;
    }
    
    // Find the "s" object (text style) - it should be in t.d.k[0].s
    // We need to find "s" that's inside the structure: "t": { "d": { "k": [ { "s": {
    // Look for "d" first, then "k", then find "s" inside the first array element
    size_t dPos = json.find("\"d\"", textDataPos);
    if (dPos == std::string::npos || dPos > textDataPos + 500) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find \"d\" object for layer: " << layerName) << std::endl;
        }
        return;
    }
    
    // Find "k" (the array) after "d"
    size_t kPos = json.find("\"k\"", dPos);
    if (kPos == std::string::npos || kPos > dPos + 200) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find \"k\" array for layer: " << layerName) << std::endl;
        }
        return;
    }
    
    // Find the opening bracket of the array
    size_t arrayOpen = json.find('[', kPos);
    if (arrayOpen == std::string::npos) {
        return;
    }
    
    // Find the opening brace of the first array element
    size_t elemOpen = json.find('{', arrayOpen);
    if (elemOpen == std::string::npos) {
        return;
    }
    
    // The structure is: k: [{ "s": { "s": 122, ... }, "t": 0 }]
    // We need to find the "s": { object (text style object) within the first array element
    // Find the closing brace of the array element first to limit our search
    int elemBraceCount = 0;
    size_t elemClose = elemOpen;
    for (size_t i = elemOpen; i < std::min(elemOpen + 1000, json.length()); i++) {
        if (json[i] == '{') elemBraceCount++;
        if (json[i] == '}') elemBraceCount--;
        if (elemBraceCount == 0) {
            elemClose = i;
            break;
        }
    }
    
    // Look for "s": { pattern within the array element
    size_t textStyleObjStart = std::string::npos;
    size_t searchStart = elemOpen;
    size_t searchEnd = elemClose;
    
    while (searchStart < searchEnd) {
        size_t sPos = json.find("\"s\"", searchStart);
        if (sPos == std::string::npos || sPos >= searchEnd) {
            break;
        }
        // Check what follows "s"
        size_t colonPos = json.find(':', sPos);
        if (colonPos != std::string::npos && colonPos < sPos + 10 && colonPos < searchEnd) {
            // Skip whitespace after colon
            size_t valueStart = colonPos + 1;
            while (valueStart < json.length() && valueStart < colonPos + 20 && 
                   (json[valueStart] == ' ' || json[valueStart] == '\t' || json[valueStart] == '\n' || json[valueStart] == '\r')) {
                valueStart++;
            }
            // If followed by '{', this is the text style object "s": {
            if (valueStart < json.length() && json[valueStart] == '{') {
                textStyleObjStart = valueStart;  // This is the opening brace of "s": {
                break;
            }
        }
        searchStart = sPos + 1;
    }
    
    if (textStyleObjStart == std::string::npos) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find \"s\": { object for layer " << layerName) << std::endl;
        }
        return;
    }
    
    size_t styleOpenBrace = textStyleObjStart;
    
    // Find matching closing brace - need to handle nested structures
    int braceCount = 0;
    size_t styleCloseBrace = styleOpenBrace;
    size_t searchLimit = std::min(styleOpenBrace + 2000, json.length());  // Increased limit
    for (size_t i = styleOpenBrace; i < searchLimit; i++) {
        if (json[i] == '{') braceCount++;
        if (json[i] == '}') braceCount--;
        if (braceCount == 0) {
            styleCloseBrace = i;
            break;
        }
    }
    
    if (styleCloseBrace <= styleOpenBrace || styleCloseBrace >= searchLimit) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Warning: Could not find matching closing brace for text style object in layer " << layerName) << std::endl;
        }
        return;
    }
    
    // Extract the text style JSON
    std::string textStyleJson = json.substr(styleOpenBrace + 1, styleCloseBrace - styleOpenBrace - 1);
    
    if (g_debug_mode) {
        // Show first 200 chars of extracted JSON for debugging
        std::string preview = textStyleJson.substr(0, std::min(200UL, textStyleJson.length()));
        LOG_COUT("[DEBUG] Extracted textStyleJson for " << layerName << " (first 200 chars): " << preview) << std::endl;
    }
    
    // Replace font size - find "s": number and replace it directly using string operations
    // The extracted textStyleJson contains "s": 122, "f": ..., so we need to replace the number
    size_t sFieldPos = textStyleJson.find("\"s\"");
    if (sFieldPos != std::string::npos) {
        // Find the colon after "s"
        size_t colonPos = textStyleJson.find(':', sFieldPos);
        if (colonPos != std::string::npos && colonPos < sFieldPos + 10) {
            // Skip whitespace and find the number
            size_t numStart = colonPos + 1;
            while (numStart < textStyleJson.length() && 
                   (textStyleJson[numStart] == ' ' || textStyleJson[numStart] == '\t' || 
                    textStyleJson[numStart] == '\n' || textStyleJson[numStart] == '\r')) {
                numStart++;
            }
            // Check if it's a number (not an object)
            if (numStart < textStyleJson.length() && std::isdigit(textStyleJson[numStart])) {
                // Find the end of the number (digit or decimal point)
                size_t numEnd = numStart;
                while (numEnd < textStyleJson.length() && 
                       (std::isdigit(textStyleJson[numEnd]) || textStyleJson[numEnd] == '.')) {
                    numEnd++;
                }
                // Skip whitespace after number
                size_t afterNum = numEnd;
                while (afterNum < textStyleJson.length() && 
                       (textStyleJson[afterNum] == ' ' || textStyleJson[afterNum] == '\t')) {
                    afterNum++;
                }
                // Replace the number with new size
                std::ostringstream sizeStream;
                sizeStream << std::fixed << std::setprecision(1) << newSize;
                std::string newSizeStr = sizeStream.str();
                textStyleJson = textStyleJson.substr(0, numStart) + newSizeStr + textStyleJson.substr(afterNum);
            }
        }
    }
    
    // Replace text content (escape quotes in newText)
    // For newlines, we need to use Unicode escape sequences in JSON (\u000D for \r)
    // First, convert \u0003 and actual \r/\n characters to \r
    std::string processedText = newText;
    replaceAllInPlace(processedText, "\\u0003", "\r");
    replaceCharInPlace(processedText, '\x03', '\r');
    // Normalize \n to \r for Lottie
    replaceCharInPlace(processedText, '\n', '\r');
    
    // Now escape for JSON
    std::string escapedText = processedText;
    // Escape backslashes first (before other escapes)
    replaceAllInPlace(escapedText, "\\", "\\\\");
    // Escape quotes
    replaceAllInPlace(escapedText, "\"", "\\\"");
    // Convert actual \r characters to Unicode escape \u000D (carriage return)
    // This must be done after backslash escaping to avoid double-escaping
    replaceAllInPlace(escapedText, "\r", "\\u000D");
    // Also handle \t
    replaceAllInPlace(escapedText, "\t", "\\t");
    
    // Replace text content using string operations for reliability
    // Find "t": "text" and replace the text value
    size_t tFieldPos = textStyleJson.find("\"t\"");
    if (tFieldPos != std::string::npos) {
        // Find the colon after "t"
        size_t colonPos = textStyleJson.find(':', tFieldPos);
        if (colonPos != std::string::npos && colonPos < tFieldPos + 10) {
            // Find the opening quote
            size_t quoteStart = textStyleJson.find('"', colonPos);
            if (quoteStart != std::string::npos && quoteStart < colonPos + 20) {
                // Find the closing quote (handle escaped quotes)
                size_t quoteEnd = quoteStart + 1;
                while (quoteEnd < textStyleJson.length()) {
                    if (textStyleJson[quoteEnd] == '"' && textStyleJson[quoteEnd - 1] != '\\') {
                        break;  // Found unescaped closing quote
                    }
                    quoteEnd++;
                }
                if (quoteEnd < textStyleJson.length()) {
                    // Replace the text between quotes
                    std::string beforeReplace = textStyleJson;
                    textStyleJson = textStyleJson.substr(0, quoteStart + 1) + escapedText + textStyleJson.substr(quoteEnd);
                    
                    if (g_debug_mode) {
                        // Verify replacement
                        if (textStyleJson.find(escapedText) == std::string::npos) {
                            LOG_COUT("[DEBUG] Error: Text replacement failed for layer " << layerName) << std::endl;
                        } else {
                            LOG_COUT("[DEBUG] Text replacement successful for " << layerName << ": \"" << newText << "\"") << std::endl;
                        }
                    }
                } else {
                    if (g_debug_mode) {
                        LOG_COUT("[DEBUG] Warning: Could not find closing quote for text field in layer " << layerName) << std::endl;
                    }
                }
            }
        }
    }
    
    if (g_debug_mode) {
        // Verify the replacement worked by checking if the new text appears
        if (textStyleJson.find(escapedText) == std::string::npos) {
            LOG_COUT("[DEBUG] Warning: Text replacement may have failed for layer " << layerName) << std::endl;
            LOG_COUT("[DEBUG] Looking for text in: " << textStyleJson.substr(0, std::min(300UL, textStyleJson.length()))) << std::endl;
        }
    }
    
    // Reconstruct the JSON
    // styleOpenBrace points to the opening '{', textStyleJson is the content without braces
    // styleCloseBrace points to the closing '}'
    // We need: [before] { [modified content] } [after]
    if (styleOpenBrace >= json.length() || styleCloseBrace >= json.length() || styleCloseBrace <= styleOpenBrace) {
        if (g_debug_mode) {
            LOG_COUT("[DEBUG] Error: Invalid brace positions for layer " << layerName) << std::endl;
        }
        return;
    }
    
    std::string before = json.substr(0, styleOpenBrace + 1);  // Includes opening brace
    std::string after = json.substr(styleCloseBrace);         // Starts from closing brace (includes it)
    json = before + textStyleJson + after;
    
    if (g_debug_mode) {
        // Quick validation: check that braces are balanced in the modified section
        int braceCount = 0;
        for (char c : textStyleJson) {
            if (c == '{') braceCount++;
            if (c == '}') braceCount--;
        }
        if (braceCount != 0) {
            LOG_COUT("[DEBUG] Warning: Unbalanced braces in modified textStyleJson for " << layerName << " (count: " << braceCount << ")") << std::endl;
        }
    }
    
    // Validate: ensure we didn't break the JSON structure
    // Count braces in the modified section to ensure balance
    int openBraces = 0, closeBraces = 0;
    for (char c : textStyleJson) {
        if (c == '{') openBraces++;
        if (c == '}') closeBraces++;
    }
    // The textStyleJson should not contain unmatched braces since it's the content of a single object
}

static void normalizeLottieTextNewlines(std::string& json) {
    // Some Lottie producers use U+0003 (ETX) as a soft line-break marker inside "t".
    // Skottie interprets '\r' as a newline in Lottie JSON.
    // We normalize it to '\r' (which will be escaped as "\\r" in JSON strings).
    //
    // We handle both representations:
    // 1) Escaped form inside JSON: "\\u0003"
    // 2) Literal byte form already present in the string: '\x03'
    const auto replacedEscaped = replaceAllInPlace(json, "\\u0003", "\\r");
    const auto replacedLiteral = replaceCharInPlace(json, '\x03', '\r');

    LOG_DEBUG("Text newline normalization: replacedEscaped=\\u0003->\\r x" << replacedEscaped
              << ", replacedLiteral=0x03->\\r x" << replacedLiteral);
}

static void skottieCrashHandler(int sig) {
    void* frames[128];
    int n = backtrace(frames, 128);

    const char* name = "UNKNOWN";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGABRT: name = "SIGABRT"; break;
        case SIGILL:  name = "SIGILL";  break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGBUS:  name = "SIGBUS";  break;
    }

    dprintf(STDERR_FILENO, "[ERROR] Caught signal %d (%s). Backtrace (%d frames):\n", sig, name, n);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(128 + sig);
}

static void installCrashHandlers() {
    std::signal(SIGSEGV, skottieCrashHandler);
    std::signal(SIGABRT, skottieCrashHandler);
    std::signal(SIGILL,  skottieCrashHandler);
    std::signal(SIGFPE,  skottieCrashHandler);
    std::signal(SIGBUS,  skottieCrashHandler);
}

int main(int argc, char* argv[]) {
    installCrashHandlers();

    // Parse command-line arguments
    bool output_png = false;
    bool output_webp = false;
    bool stream_mode = false;
    bool debug_mode = false;
    const char* input_file = nullptr;
    const char* output_dir = nullptr;
    const char* text_config_file = nullptr;
    float fps = 25.0f;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--png") {
            output_png = true;
        } else if (arg == "--webp") {
            output_webp = true;
        } else if (arg == "--stream") {
            stream_mode = true;
        } else if (arg == "--debug") {
            debug_mode = true;
        } else if (arg == "--text-config") {
            if (i + 1 < argc) {
                text_config_file = argv[++i];
            } else {
                LOG_CERR("Error: --text-config requires a file path") << std::endl;
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            LOG_CERR("Usage: " << argv[0] << " [--png] [--webp] [--stream] [--debug] [--text-config <config.json>] <input.json> <output_dir> [fps]") << std::endl;
            LOG_CERR("  --png:         Output frames as PNG files") << std::endl;
            LOG_CERR("  --webp:        Output frames as WebP files") << std::endl;
            LOG_CERR("  --stream:      Stream frames to stdout (for piping to ffmpeg)") << std::endl;
            LOG_CERR("  --debug:       Enable debug output") << std::endl;
            LOG_CERR("  --text-config: Path to text configuration JSON (for auto-fit and dynamic text values)") << std::endl;
            LOG_CERR("  fps:           Frames per second for output (default: 25)") << std::endl;
            LOG_CERR("") << std::endl;
            LOG_CERR("At least one of --png or --webp must be specified.") << std::endl;
            LOG_CERR("When --stream is used, output_dir can be '-' or any value (ignored).") << std::endl;
            return 1;
        } else if (arg[0] != '-' || arg == "-") {
            // Positional argument (including "-" which is used for streaming/stdout)
            if (!input_file) {
                input_file = argv[i];
            } else if (!output_dir) {
                output_dir = argv[i];
            } else {
                // Try to parse as fps
                try {
                    fps = std::stof(arg);
                } catch (...) {
                    LOG_CERR("Error: Invalid fps value: " << arg) << std::endl;
                    return 1;
                }
            }
        } else {
            LOG_CERR("Error: Unknown option: " << arg) << std::endl;
            LOG_CERR("Use --help for usage information.") << std::endl;
            return 1;
        }
    }

    // Validate arguments
    if (!output_png && !output_webp) {
        LOG_CERR("Error: At least one of --png or --webp must be specified.") << std::endl;
        LOG_CERR("Use --help for usage information.") << std::endl;
        return 1;
    }
    
    if (stream_mode && !output_png) {
        LOG_CERR("Error: Streaming mode requires --png (ffmpeg image2pipe expects PNG format).") << std::endl;
        LOG_CERR("Use --help for usage information.") << std::endl;
        return 1;
    }
    
    // Set global flags (affect logging behavior)
    g_stream_mode = stream_mode;
    g_debug_mode = debug_mode;

    if (!input_file) {
        LOG_CERR("Error: Missing input file.") << std::endl;
        LOG_CERR("Usage: " << argv[0] << " [--png] [--webp] [--stream] <input.json> <output_dir> [fps]") << std::endl;
        LOG_CERR("Use --help for usage information.") << std::endl;
        return 1;
    }

    // Validate input file exists and is a file (not a directory)
    std::filesystem::path input_path(input_file);
    if (!std::filesystem::exists(input_path)) {
        LOG_CERR("Error: Input file does not exist: " << input_file) << std::endl;
        return 1;
    }
    if (!std::filesystem::is_regular_file(input_path)) {
        LOG_CERR("Error: Input path is not a file (is it a directory?): " << input_file) << std::endl;
        return 1;
    }

    // Handle output directory (not needed in stream mode)
    if (!stream_mode) {
        if (!output_dir) {
            LOG_CERR("Error: Missing output directory (use '-' for streaming mode).") << std::endl;
            LOG_CERR("Usage: " << argv[0] << " [--png] [--webp] [--stream] <input.json> <output_dir> [fps]") << std::endl;
            return 1;
        }
        
        // Create output directory if it doesn't exist
        std::filesystem::path output_path(output_dir);
        if (!std::filesystem::exists(output_path)) {
            std::error_code ec;
            if (!std::filesystem::create_directories(output_path, ec)) {
                LOG_CERR("Error: Could not create output directory: " << output_dir) << std::endl;
                LOG_CERR("  " << ec.message()) << std::endl;
                return 1;
            }
            LOG_DEBUG("Created output directory: " << output_dir);
        } else if (!std::filesystem::is_directory(output_path)) {
            LOG_CERR("Error: Output path exists but is not a directory: " << output_dir) << std::endl;
            return 1;
        }
    } else {
        // In stream mode, output_dir is optional (can be "-")
        if (!output_dir) {
            output_dir = "-";  // Default to "-" for streaming
        }
        LOG_DEBUG("Stream mode enabled - frames will be written to stdout");
    }

    // Read Lottie JSON file
    std::ifstream file(input_file);
    if (!file.is_open()) {
        LOG_CERR("Error: Could not open input file: " << input_file) << std::endl;
        return 1;
    }

    std::string json_data((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    file.close();

    // Register codecs needed by SkResources FileResourceProvider for image decoding.
    // (SkResources docs: clients must call SkCodec::Register() before using FileResourceProvider.)
    SkCodecs::Register(SkPngDecoder::Decoder());
    LOG_DEBUG("Registered image codecs via SkCodecs::Register: png");

    normalizeLottieTextNewlines(json_data);
    
    // Apply text configuration if config file is provided (auto-fit and dynamic text values)
    if (text_config_file) {
        LOG_DEBUG("Loading text configuration from: " << text_config_file);
        auto textConfigs = parseTextConfig(text_config_file);
        
        if (!textConfigs.empty()) {
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
                    
                    // Calculate optimal font size for fallback text (try to fit it as best as possible)
                    // Start from minSize and try to maximize up to maxSize
                    // If it doesn't fit at min size, we'll use min size anyway (it will overflow/truncate)
                    // But if it fits, we'll scale it up to max size to fill the frame
                    
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
                // For right-aligned text, we need to ensure the text stays off-screen
                // The right edge is at: position + textWidth
                // We need: position + newTextWidth <= 0 to keep it off-screen
                // Original: oldPosition + originalWidth was designed to be <= 0
                // New: newPosition + newTextWidth should be <= 0
                // So: newPosition should be <= -newTextWidth
                // Adjustment needed: newPosition - oldPosition = -(newTextWidth - originalWidth) = -widthDiff
                // But since we want to move it further left (more negative), we use: oldPosition - abs(widthDiff)
                float widthDiff = it->newTextWidth - it->originalTextWidth;
                if (std::abs(widthDiff) > 0.1f) {  // Only adjust if there's a significant change
                    // Always move further left by the absolute difference to ensure text stays off-screen
                    float adjustment = std::abs(widthDiff);
                    adjustTextAnimatorPosition(json_data, it->layerName, adjustment);
                    LOG_DEBUG("Adjusted text animator position for " << it->layerName << " by " << adjustment << "px (widthDiff: " << widthDiff << ")");
                }
                
                LOG_DEBUG("Updated " << it->layerName << ": text=\"" << it->textToUse << "\", size=" << it->optimalSize);
            }
        } else {
            LOG_DEBUG("No text layer configuration(s) found in config file");
        }
    }
    
    skottie::Animation::Builder builder;
    
    LOG_DEBUG("Creating Skottie animation...");
    LOG_DEBUG("JSON size: " << json_data.length() << " bytes");
    
    // Debug: save modified JSON to file for inspection
    if (g_debug_mode && text_config_file) {
        std::ofstream debugFile("/workspace/modified_json_debug.json");
        if (debugFile.is_open()) {
            debugFile << json_data;
            debugFile.close();
            LOG_DEBUG("Saved modified JSON to modified_json_debug.json for inspection");
        }
    }
    
    // Resource provider (images, etc.)
    {
        std::filesystem::path jsonPath(input_file);
        std::filesystem::path baseDir = jsonPath.has_parent_path() ? jsonPath.parent_path()
                                                                   : std::filesystem::path(".");
        std::error_code ec;
        std::filesystem::path absBaseDir = std::filesystem::absolute(baseDir, ec);
        const auto baseDirStr = (ec ? baseDir.string() : absBaseDir.string());

        LOG_DEBUG("ResourceProvider base_dir: " << baseDirStr);
        auto fileRP = skresources::FileResourceProvider::Make(SkString(baseDirStr.c_str()),
                                                              skresources::ImageDecodeStrategy::kPreDecode);
        if (!fileRP) {
            LOG_CERR("[ERROR] Failed to create skresources::FileResourceProvider for base_dir=" << baseDirStr) << std::endl;
        } else {
            auto cachingRP = skresources::CachingResourceProvider::Make(std::move(fileRP));
            builder.setResourceProvider(std::move(cachingRP));
            LOG_DEBUG("ResourceProvider set (FileResourceProvider + CachingResourceProvider)");
        }
    }

    // Font manager: Use fontconfig (handles both system fonts and custom fonts via fontconfig)
    // Custom fonts in /usr/local/share/fonts should be registered via fc-cache
    LOG_DEBUG("Setting up font manager...");
    sk_sp<SkFontMgr> fontMgr;
    
    try {
        const auto fcInitOk = FcInit();
        LOG_DEBUG("FcInit() returned " << (fcInitOk ? "true" : "false"));

        auto scanner = SkFontScanner_Make_FreeType();
        if (!scanner) {
            LOG_CERR("[ERROR] SkFontScanner_Make_FreeType() returned nullptr; cannot use fontconfig") << std::endl;
            fontMgr = SkFontMgr::RefEmpty();
        } else {
            fontMgr = SkFontMgr_New_FontConfig(nullptr, std::move(scanner));
            if (fontMgr) {
                LOG_DEBUG("Fontconfig font manager created successfully");
                LOG_DEBUG("Fontconfig will find system fonts and custom fonts (if registered via fc-cache)");
            } else {
                LOG_CERR("[ERROR] Failed to create fontconfig font manager") << std::endl;
                fontMgr = SkFontMgr::RefEmpty();
            }
        }
    } catch (...) {
        LOG_CERR("[ERROR] Exception creating fontconfig font manager") << std::endl;
        fontMgr = SkFontMgr::RefEmpty();
    }
    
    builder.setFontManager(fontMgr);
    LOG_DEBUG("Font manager set on builder");

    LOG_DEBUG("Calling builder.make() to parse JSON...");
    sk_sp<skottie::Animation> animation = builder.make(json_data.c_str(), json_data.length());
    
    if (!animation) {
        LOG_CERR("[ERROR] Failed to parse Lottie animation from JSON") << std::endl;
        return 1;
    }
    
    LOG_DEBUG("Animation parsed successfully");

    // Get animation dimensions and duration
    SkSize size = animation->size();
    int width = static_cast<int>(size.width());
    int height = static_cast<int>(size.height());
    float duration = animation->duration();
    float animation_fps = animation->fps();

    LOG_DEBUG("Animation loaded: " << width << "x" << height);
    LOG_DEBUG("Duration: " << duration << " seconds");
    LOG_DEBUG("Animation FPS: " << animation_fps);
    LOG_DEBUG("Output FPS: " << fps);

    // Calculate number of frames to render
    int num_frames = static_cast<int>(std::ceil(duration * fps));
    LOG_DEBUG("Rendering " << num_frames << " frames...");

    // Create a surface to render to with transparent background
    // Use kUnpremul_SkAlphaType to preserve transparency better
    LOG_DEBUG("Creating Skia surface: " << width << "x" << height << " with kUnpremul_SkAlphaType");
    SkImageInfo info = SkImageInfo::MakeN32(width, height, kUnpremul_SkAlphaType);
    
    // CRITICAL: Allocate pixel buffer explicitly initialized to transparent
    // This ensures the surface starts with transparent pixels, not black
    size_t rowBytes = info.minRowBytes();
    size_t totalBytes = info.computeByteSize(rowBytes);

    // Create RGBA conversion surface once (reuse for all frames)
    SkImageInfo rgbaInfo = SkImageInfo::MakeN32(width, height, kUnpremul_SkAlphaType);
    auto rgbaSurface = SkSurfaces::Raster(rgbaInfo);
    if (!rgbaSurface) {
        LOG_CERR("[ERROR] Failed to create RGBA conversion surface") << std::endl;
        return 1;
    }
    LOG_DEBUG("RGBA conversion surface created (will be reused for all frames)");

    // Determine number of threads for parallel rendering
    int num_threads = std::max(1, (int)std::thread::hardware_concurrency());
    LOG_DEBUG("Using " << num_threads << " threads for parallel rendering");

    // Create per-thread animations and surfaces
    std::vector<sk_sp<skottie::Animation>> thread_animations;
    std::vector<sk_sp<SkSurface>> thread_surfaces;
    std::vector<sk_sp<SkSurface>> thread_rgba_surfaces;
    std::vector<std::vector<uint8_t>> thread_pixel_buffers;

    for (int t = 0; t < num_threads; t++) {
        // Create animation for each thread (thread-safe: each thread has its own)
        auto thread_animation = builder.make(json_data.c_str(), json_data.length());
        if (!thread_animation) {
            LOG_CERR("[ERROR] Failed to create animation for thread " << t) << std::endl;
            return 1;
        }
        thread_animations.push_back(thread_animation);
        
        // Create surface for each thread
        std::vector<uint8_t> thread_pixels(totalBytes, 0);
        thread_pixel_buffers.push_back(std::move(thread_pixels));
        auto thread_surface = SkSurfaces::WrapPixels(info, thread_pixel_buffers[t].data(), rowBytes, nullptr);
        if (!thread_surface) {
            LOG_CERR("[ERROR] Failed to create surface for thread " << t) << std::endl;
            return 1;
        }
        thread_surfaces.push_back(thread_surface);
        
        // Create RGBA conversion surface for each thread
        auto thread_rgba_surface = SkSurfaces::Raster(rgbaInfo);
        if (!thread_rgba_surface) {
            LOG_CERR("[ERROR] Failed to create RGBA surface for thread " << t) << std::endl;
            return 1;
        }
        thread_rgba_surfaces.push_back(thread_rgba_surface);
    }

    // Pre-compute frame times (avoid per-frame calculation)
    std::vector<float> frame_times(num_frames);
    for (int i = 0; i < num_frames; i++) {
        frame_times[i] = (i < num_frames - 1) ? (float)i / (num_frames - 1) * duration : duration;
    }

    // Pre-distribute frames to threads (round-robin for better load balancing)
    std::vector<std::vector<int>> thread_frames(num_threads);
    for (int i = 0; i < num_frames; i++) {
        thread_frames[i % num_threads].push_back(i);
    }

    // Pre-compute filename base to avoid repeated string operations
    std::string filename_base = std::string(output_dir) + "/frame_";
    const size_t filename_base_len = filename_base.length();

    std::atomic<int> completed_frames(0);
    std::atomic<int> failed_frames(0);
    std::mutex progress_mutex;  // Mutex for thread-safe progress reporting

    // Frame buffer for streaming mode (ensures sequential output)
    struct BufferedFrame {
        int frame_idx;
        sk_sp<SkData> png_data;
        sk_sp<SkData> webp_data;
        bool ready;
        
        BufferedFrame() : frame_idx(-1), ready(false) {}
    };
    
    std::vector<BufferedFrame> frame_buffer;
    std::mutex buffer_mutex;
    std::condition_variable buffer_cv;
    int next_frame_to_write = 0;
    bool streaming_complete = false;
    
    if (stream_mode) {
        frame_buffer.resize(num_frames);
        LOG_DEBUG("Frame buffer allocated for " << num_frames << " frames");
    }

    // Worker function for parallel frame rendering
    auto render_frame_worker = [&](int thread_id) {
        auto& animation = thread_animations[thread_id];
        auto& surface = thread_surfaces[thread_id];
        auto& rgba_surface = thread_rgba_surfaces[thread_id];
        auto* canvas = surface->getCanvas();
        
        // Thread-local progress counter to reduce atomic contention
        thread_local int local_completed = 0;
        local_completed = 0;
        
        // Reusable filename buffer
        char filename[512];
        
        // Process pre-assigned frames
        for (int frame_idx : thread_frames[thread_id]) {
            // Use pre-computed frame time
            float t = frame_times[frame_idx];
            
            // Clear canvas with transparent background
            canvas->clear(SK_ColorTRANSPARENT);

            // Seek to the desired frame time
            animation->seekFrameTime(t);
            
            // Render the animation frame
            animation->render(canvas);

            // Get the image from the surface
            sk_sp<SkImage> image = surface->makeImageSnapshot();
            if (!image) {
                LOG_CERR("[ERROR] Failed to create image snapshot for frame " << frame_idx) << std::endl;
                failed_frames++;
                continue;
            }
            
            // Get image info once (reuse for debug and conversion check)
            SkImageInfo imgInfo = image->imageInfo();
            
            // Debug output for first frame
            if (frame_idx == 0 && thread_id == 0) {
                LOG_DEBUG("Image snapshot created: " << image->width() << "x" << image->height());
                LOG_DEBUG("Image color type: " << imgInfo.colorType() << ", alpha type: " << imgInfo.alphaType());
                LOG_DEBUG("Image has alpha: " << (imgInfo.alphaType() != kOpaque_SkAlphaType));
            }
            
            // Check if conversion is needed (only convert if necessary)
            bool needs_conversion = (imgInfo.colorType() != kN32_SkColorType || 
                                     imgInfo.alphaType() != kUnpremul_SkAlphaType);
            
            if (needs_conversion) {
                // Convert to RGBA_8888 with kUnpremul_SkAlphaType
                rgba_surface->getCanvas()->clear(SK_ColorTRANSPARENT);
                rgba_surface->getCanvas()->drawImage(image, 0, 0, SkSamplingOptions());
                image = rgba_surface->makeImageSnapshot();
                if (!image) {
                    LOG_CERR("[ERROR] Failed to convert image for frame " << frame_idx) << std::endl;
                    failed_frames++;
                    continue;
                }
                if (frame_idx == 0 && thread_id == 0) {
                    LOG_DEBUG("Converted image to RGBA_8888 with kUnpremul_SkAlphaType for encoding");
                    SkImageInfo newInfo = image->imageInfo();
                    LOG_DEBUG("New image color type: " << newInfo.colorType() << ", alpha type: " << newInfo.alphaType());
                }
            }

            // Encode to PNG if requested (with faster compression)
            sk_sp<SkData> png_data;
            if (output_png) {
                SkPngEncoder::Options png_options;
                png_options.fZLibLevel = 1;  // Faster compression (was 6)
                png_data = SkPngEncoder::Encode(nullptr, image.get(), png_options);
                if (!png_data) {
                    LOG_CERR("[ERROR] Failed to encode PNG for frame " << frame_idx) << std::endl;
                    if (!output_webp) {
                        failed_frames++;
                        continue;
                    }
                } else if (frame_idx == 0 && thread_id == 0) {
                    LOG_DEBUG("PNG encoded: " << png_data->size() << " bytes");
                }
            }

            // Encode to WebP if requested
            sk_sp<SkData> webp_data;
            if (output_webp) {
                SkWebpEncoder::Options webp_options;
                webp_options.fCompression = SkWebpEncoder::Compression::kLossless;
                webp_options.fQuality = 100;
                webp_data = SkWebpEncoder::Encode(nullptr, image.get(), webp_options);
                if (!webp_data) {
                    LOG_CERR("[ERROR] Failed to encode WebP for frame " << frame_idx) << std::endl;
                    if (!output_png || !png_data) {
                        failed_frames++;
                        continue;
                    }
                } else if (frame_idx == 0 && thread_id == 0) {
                    LOG_DEBUG("WebP encoded: " << webp_data->size() << " bytes");
                }
            }

            // Write files or buffer for streaming
            if (stream_mode) {
                // Buffer frame for sequential output
                {
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    frame_buffer[frame_idx].frame_idx = frame_idx;
                    if (output_png && png_data) {
                        frame_buffer[frame_idx].png_data = png_data;
                    }
                    if (output_webp && webp_data) {
                        frame_buffer[frame_idx].webp_data = webp_data;
                    }
                    frame_buffer[frame_idx].ready = true;
                }
                buffer_cv.notify_all();
            } else {
                // Write files (synchronous I/O - could be optimized further with async I/O)
                if (output_png && png_data) {
                    snprintf(filename, sizeof(filename), "%s%05d.png", filename_base.c_str(), frame_idx);
                    
                    SkFILEWStream png_file_stream(filename);
                    if (!png_file_stream.isValid()) {
                        LOG_CERR("[ERROR] Could not open PNG output file: " << filename) << std::endl;
                        failed_frames++;
                        continue;
                    }
                    
                    if (!png_file_stream.write(png_data->data(), png_data->size())) {
                        LOG_CERR("[ERROR] Failed to write PNG data for frame " << frame_idx) << std::endl;
                        failed_frames++;
                        continue;
                    } else if (frame_idx == 0 && thread_id == 0) {
                        LOG_DEBUG("Frame " << frame_idx << " PNG written to " << filename);
                    }
                }

                if (output_webp && webp_data) {
                    snprintf(filename, sizeof(filename), "%s%05d.webp", filename_base.c_str(), frame_idx);
                    
                    SkFILEWStream webp_file_stream(filename);
                    if (!webp_file_stream.isValid()) {
                        LOG_CERR("[ERROR] Could not open WebP output file: " << filename) << std::endl;
                        if (!output_png || !png_data) {
                            failed_frames++;
                            continue;
                        }
                    } else {
                        if (!webp_file_stream.write(webp_data->data(), webp_data->size())) {
                            LOG_CERR("[ERROR] Failed to write WebP data for frame " << frame_idx) << std::endl;
                            if (!output_png || !png_data) {
                                failed_frames++;
                                continue;
                            }
                        } else if (frame_idx == 0 && thread_id == 0) {
                            LOG_DEBUG("Frame " << frame_idx << " WebP written to " << filename);
                        }
                    }
                }
            }

            // Progress reporting (thread-safe to prevent interleaved output)
            local_completed++;
            if (local_completed % 10 == 0) {
                int done = completed_frames.fetch_add(10) + 10;
                if (done % 10 == 0 || done == num_frames) {
                    std::lock_guard<std::mutex> lock(progress_mutex);
                    LOG_DEBUG("Rendered frame " << done << "/" << num_frames);
                }
            }
        }
        
        // Update final count for remaining frames in this thread
        int remainder = local_completed % 10;
        if (remainder > 0) {
            int done = completed_frames.fetch_add(remainder) + remainder;
            if (done == num_frames) {
                std::lock_guard<std::mutex> lock(progress_mutex);
                LOG_DEBUG("Rendered frame " << done << "/" << num_frames);
            }
        }
    };

    // Sequential writer thread for streaming mode
    std::thread writer_thread;
    if (stream_mode) {
        writer_thread = std::thread([&]() {
            // Only support PNG streaming for now (ffmpeg image2pipe expects PNG)
            if (!output_png) {
                LOG_CERR("[ERROR] Streaming mode requires --png (WebP streaming not supported)") << std::endl;
                return;
            }
            
            for (int i = 0; i < num_frames; i++) {
                std::unique_lock<std::mutex> lock(buffer_mutex);
                // Wait for next frame to be ready or all frames completed
                while (!frame_buffer[next_frame_to_write].ready && 
                       (completed_frames.load() + failed_frames.load() < num_frames)) {
                    buffer_cv.wait(lock);
                }
                
                // Check if frame is ready
                if (frame_buffer[next_frame_to_write].ready) {
                    auto& frame = frame_buffer[next_frame_to_write];
                    lock.unlock();  // Release lock before I/O
                    
                    if (frame.png_data) {
                        // Write PNG data to stdout
                        std::cout.write(reinterpret_cast<const char*>(frame.png_data->data()), 
                                       frame.png_data->size());
                        std::cout.flush();
                    } else {
                        LOG_CERR("[ERROR] Frame " << next_frame_to_write << " has no PNG data") << std::endl;
                        failed_frames++;
                    }
                    next_frame_to_write++;
                } else {
                    // Frame not ready and all workers done - might be a failure
                    // Skip this frame and continue
                    LOG_CERR("[WARNING] Frame " << next_frame_to_write << " was not rendered") << std::endl;
                    failed_frames++;
                    next_frame_to_write++;
                }
            }
            streaming_complete = true;
            buffer_cv.notify_all();
        });
    }

    // Launch worker threads
    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads; t++) {
        workers.emplace_back(render_frame_worker, t);
    }

    // Wait for all threads to complete
    for (auto& worker : workers) {
        worker.join();
    }
    
    // Wait for writer thread to complete
    if (stream_mode && writer_thread.joinable()) {
        writer_thread.join();
    }

    // Check for failures
    if (failed_frames > 0) {
        LOG_CERR("[WARNING] " << failed_frames << " frames failed to render") << std::endl;
    }

    if (!stream_mode) {
        std::ostringstream success_msg;
        success_msg << "[INFO] Successfully rendered " << num_frames << " frames to " << output_dir;
        if (output_png && output_webp) {
            success_msg << " (PNG and WebP formats)";
        } else if (output_png) {
            success_msg << " (PNG format)";
        } else {
            success_msg << " (WebP format)";
        }
        LOG_COUT(success_msg.str()) << std::endl;
    } else {
        // In stream mode, log to stderr to avoid interfering with stdout PNG data
        LOG_CERR("[INFO] Successfully streamed " << num_frames << " frames to stdout") << std::endl;
    }
    return 0;
}

