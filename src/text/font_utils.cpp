#include "font_utils.h"
#include "../utils/string_utils.h"
#include "../utils/logging.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkSurface.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkColor.h"
#include <regex>
#include <algorithm>
#include <cmath>

SkFontStyle getSkFontStyle(const std::string& styleStr) {
    if (styleStr.find("Bold") != std::string::npos && styleStr.find("Italic") != std::string::npos) {
        return SkFontStyle::BoldItalic();
    } else if (styleStr.find("Bold") != std::string::npos) {
        return SkFontStyle::Bold();
    } else if (styleStr.find("Italic") != std::string::npos) {
        return SkFontStyle::Italic();
    }
    return SkFontStyle::Normal();
}

// Helper function to measure rendered text width by scanning pixels (PIXEL_PERFECT mode)
static SkScalar measureRenderedTextWidth(sk_sp<SkTextBlob> blob, const SkFont& font, const SkRect& blobBounds) {
    // Create an off-screen surface to render the text
    // Use a surface that matches the rendering characteristics
    int padding = 20;  // Extra padding for anti-aliasing
    int surfaceWidth = static_cast<int>(std::ceil(blobBounds.width())) + padding * 2;
    int surfaceHeight = static_cast<int>(std::ceil(blobBounds.height())) + padding * 2;
    
    if (surfaceWidth <= 0 || surfaceHeight <= 0) {
        return blobBounds.width();
    }
    
    SkImageInfo info = SkImageInfo::MakeN32(
        surfaceWidth,
        surfaceHeight,
        kPremul_SkAlphaType
    );
    
    auto surface = SkSurfaces::Raster(info);
    if (!surface) {
        // Fallback to blob bounds if surface creation fails
        return blobBounds.width();
    }
    
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    
    // Render the text
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorBLACK);
    
    canvas->drawTextBlob(
        blob,
        padding - blobBounds.left(),
        padding - blobBounds.top(),
        paint
    );
    
    // Get the image and find actual non-transparent bounds
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        return blobBounds.width();
    }
    
    // Read pixels and find actual rendered bounds
    SkPixmap pixmap;
    if (!image->peekPixels(&pixmap)) {
        return blobBounds.width();
    }
    
    // Find leftmost and rightmost non-transparent pixels
    int left = pixmap.width();
    int right = 0;
    bool foundPixel = false;
    
    for (int y = 0; y < pixmap.height(); y++) {
        const uint32_t* row = pixmap.addr32(0, y);
        for (int x = 0; x < pixmap.width(); x++) {
            SkColor color = row[x];
            if (SkColorGetA(color) > 0) {  // Non-transparent pixel
                left = std::min(left, x);
                right = std::max(right, x);
                foundPixel = true;
            }
        }
    }
    
    if (foundPixel && right >= left) {
        // Return actual rendered width + small safety margin
        return static_cast<SkScalar>(right - left + 1) + 1.0f;  // +1px safety
    }
    
    // Fallback to blob bounds
    return blobBounds.width();
}

SkScalar measureTextWidth(
    SkFontMgr* fontMgr,
    const std::string& fontFamily,
    const std::string& fontStyle,
    const std::string& fontName,  // Full name like "SegoeUI-Bold"
    float fontSize,
    const std::string& text,
    TextMeasurementMode mode
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
    
    // Enable proper text rendering settings for ACCURATE and PIXEL_PERFECT modes
    if (mode != TextMeasurementMode::FAST) {
        font.setEdging(SkFont::Edging::kAntiAlias);
        font.setSubpixel(true);
        font.setHinting(SkFontHinting::kNormal);
    }
    
    // Split text by newlines (\r or \n) and measure each line
    // Return the width of the longest line
    SkScalar maxWidth = 0.0f;
    std::string currentLine;
    
    for (size_t i = 0; i <= text.length(); i++) {
        if (i == text.length() || text[i] == '\r' || text[i] == '\n') {
            // Measure current line
            if (!currentLine.empty()) {
                SkScalar width = 0.0f;
                
                if (mode == TextMeasurementMode::FAST) {
                    // FAST mode: Use measureText() with bounds (current implementation)
                    SkRect bounds;
                    font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
                    width = bounds.width();
                } else {
                    // ACCURATE or PIXEL_PERFECT mode: Use SkTextBlob
                    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(
                        currentLine.c_str(),
                        font
                    );
                    
                    if (blob) {
                        SkRect blobBounds = blob->bounds();
                        
                        if (mode == TextMeasurementMode::ACCURATE) {
                            // ACCURATE mode: Use blob bounds
                            width = blobBounds.width();
                        } else {
                            // PIXEL_PERFECT mode: Render and measure actual pixels
                            width = measureRenderedTextWidth(blob, font, blobBounds);
                        }
                    } else {
                        // Fallback if blob creation fails
                        SkRect bounds;
                        font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
                        width = bounds.width();
                    }
                }
                
                maxWidth = std::max(maxWidth, width);
                if (g_debug_mode && (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)) {
                    LOG_COUT("[DEBUG] Measured line: \"" << currentLine << "\" width: " << width << " (mode: " << (mode == TextMeasurementMode::FAST ? "FAST" : (mode == TextMeasurementMode::ACCURATE ? "ACCURATE" : "PIXEL_PERFECT")) << ")") << std::endl;
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
        SkScalar width = 0.0f;
        
        if (mode == TextMeasurementMode::FAST) {
            SkRect bounds;
            font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
            width = bounds.width();
        } else {
            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(
                currentLine.c_str(),
                font
            );
            
            if (blob) {
                SkRect blobBounds = blob->bounds();
                
                if (mode == TextMeasurementMode::ACCURATE) {
                    width = blobBounds.width();
                } else {
                    width = measureRenderedTextWidth(blob, font, blobBounds);
                }
            } else {
                SkRect bounds;
                font.measureText(currentLine.c_str(), currentLine.length(), SkTextEncoding::kUTF8, &bounds);
                width = bounds.width();
            }
        }
        
        maxWidth = std::max(maxWidth, width);
        if (g_debug_mode && (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)) {
            LOG_COUT("[DEBUG] Measured line: \"" << currentLine << "\" width: " << width << " (mode: " << (mode == TextMeasurementMode::FAST ? "FAST" : (mode == TextMeasurementMode::ACCURATE ? "ACCURATE" : "PIXEL_PERFECT")) << ")") << std::endl;
        }
    }
    
    if (g_debug_mode && (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos)) {
        LOG_COUT("[DEBUG] Multiline text - longest line width: " << maxWidth) << std::endl;
    }
    
    return maxWidth;
}

FontInfo extractFontInfoFromJson(const std::string& json, const std::string& layerName) {
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

