#ifndef FRAME_ENCODER_H
#define FRAME_ENCODER_H

#include "include/core/SkImage.h"
#include "include/core/SkData.h"
#include <string>

// Frame encoding result
struct EncodedFrame {
    sk_sp<SkData> png_data;
    sk_sp<SkData> webp_data;
    bool has_png = false;
    bool has_webp = false;
};

// Encode frame image to PNG and/or WebP
EncodedFrame encodeFrame(
    sk_sp<SkImage> image,
    bool output_png,
    bool output_webp
);

// Write encoded frame to file
// Returns 0 on success, 1 on failure
int writeFrameToFile(
    const EncodedFrame& frame,
    int frame_idx,
    const std::string& filename_base,
    bool output_png,
    bool output_webp
);

#endif // FRAME_ENCODER_H

