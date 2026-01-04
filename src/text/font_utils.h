#ifndef FONT_UTILS_H
#define FONT_UTILS_H

#include "include/core/SkFontMgr.h"
#include "include/core/SkScalar.h"
#include <string>

// Text measurement mode - controls accuracy vs performance trade-off
enum class TextMeasurementMode {
    FAST,          // Uses measureText() - fastest, basic accuracy
    ACCURATE,      // Uses SkTextBlob bounds - good balance, accounts for kerning and glyph metrics
    PIXEL_PERFECT  // Renders text and measures actual pixels - most accurate, accounts for anti-aliasing
};

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
// mode: Measurement accuracy mode (default: ACCURATE for good balance)
SkScalar measureTextWidth(
    SkFontMgr* fontMgr,
    const std::string& fontFamily,
    const std::string& fontStyle,
    const std::string& fontName,  // Full name like "SegoeUI-Bold"
    float fontSize,
    const std::string& text,
    TextMeasurementMode mode = TextMeasurementMode::ACCURATE
);

// Extract font info from Lottie JSON for a text layer
FontInfo extractFontInfoFromJson(const std::string& json, const std::string& layerName);

#endif // FONT_UTILS_H

