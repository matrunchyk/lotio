#ifndef TEXT_SIZING_H
#define TEXT_SIZING_H

#include "font_utils.h"
#include "layer_overrides.h"
#include "include/core/SkFontMgr.h"
#include <string>

// Calculate optimal font size for text to fit
float calculateOptimalFontSize(
    SkFontMgr* fontMgr,
    const FontInfo& fontInfo,
    const LayerOverride& config,
    const std::string& text,
    float targetWidth,
    TextMeasurementMode mode = TextMeasurementMode::ACCURATE
);

#endif // TEXT_SIZING_H

