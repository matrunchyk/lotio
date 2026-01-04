#ifndef ANIMATION_SETUP_H
#define ANIMATION_SETUP_H

#include <skia/modules/skottie/include/Skottie.h>
#include <skia/core/SkFontMgr.h>
#include <string>
#include <memory>
#include "../text/font_utils.h"

// Animation setup result
struct AnimationSetupResult {
    sk_sp<skottie::Animation> animation;
    skottie::Animation::Builder builder{};  // Default construct in place
    std::string processed_json;
    
    bool success() const { return animation != nullptr; }
};

// Setup Skottie animation builder and create animation
// Reads JSON file, applies text processing, and creates animation
// Returns result with animation, builder, and processed JSON on success
// textPadding: padding factor (0.0-1.0), default 0.97 means 97% of target width (3% padding)
// textMeasurementMode: measurement accuracy mode (default: ACCURATE for good balance)
AnimationSetupResult setupAndCreateAnimation(
    const std::string& input_file,
    const std::string& text_config_file,
    float textPadding = 0.97f,
    TextMeasurementMode textMeasurementMode = TextMeasurementMode::ACCURATE
);

#endif // ANIMATION_SETUP_H

