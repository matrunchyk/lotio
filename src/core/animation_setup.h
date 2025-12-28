#ifndef ANIMATION_SETUP_H
#define ANIMATION_SETUP_H

#include "modules/skottie/include/Skottie.h"
#include "include/core/SkFontMgr.h"
#include <string>
#include <memory>

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
AnimationSetupResult setupAndCreateAnimation(
    const std::string& input_file,
    const std::string& text_config_file
);

#endif // ANIMATION_SETUP_H

