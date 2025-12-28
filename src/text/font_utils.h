#ifndef FONT_UTILS_H
#define FONT_UTILS_H

#include "include/core/SkFontMgr.h"
#include "include/core/SkScalar.h"
#include <string>

// Extract font info from Lottie JSON for a text layer
struct FontInfo {
    std::string family;
    std::string style;
    std::string name;
    float size;
    std::string text;
    float textBoxWidth;  // From sz[0] if available
};

// Get font style from Lottie font info
SkFontStyle getSkFontStyle(const std::string& styleStr);

// Measure text width with given font
SkScalar measureTextWidth(
    SkFontMgr* fontMgr,
    const std::string& fontFamily,
    const std::string& fontStyle,
    const std::string& fontName,  // Full name like "SegoeUI-Bold"
    float fontSize,
    const std::string& text
);

// Extract font info from Lottie JSON for a text layer
FontInfo extractFontInfoFromJson(const std::string& json, const std::string& layerName);

#endif // FONT_UTILS_H

