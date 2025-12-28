#ifndef TEXT_SIZING_H
#define TEXT_SIZING_H

#include "font_utils.h"
#include "text_config.h"
#include "include/core/SkFontMgr.h"
#include <string>

// Calculate optimal font size for text to fit
float calculateOptimalFontSize(
    SkFontMgr* fontMgr,
    const FontInfo& fontInfo,
    const TextLayerConfig& config,
    const std::string& text,
    float targetWidth
);

#endif // TEXT_SIZING_H

