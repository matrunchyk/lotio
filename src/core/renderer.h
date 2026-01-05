#ifndef RENDERER_H
#define RENDERER_H

#include <skia/modules/skottie/include/Skottie.h>
#include <skia/core/SkSurface.h>
#include <string>
#include <atomic>

// Render configuration
struct RenderConfig {
    bool stream_mode = false;
    std::string output_dir;
    float fps = 25.0f;
};

// Render all frames of the animation
// Returns 0 on success, 1 on failure
int renderFrames(
    sk_sp<skottie::Animation> animation,
    skottie::Animation::Builder& builder,
    const std::string& json_data,
    const RenderConfig& config
);

#endif // RENDERER_H

