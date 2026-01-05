#ifndef FRAME_ENCODER_H
#define FRAME_ENCODER_H

#include "include/core/SkImage.h"
#include "include/core/SkData.h"
#include <string>

// Frame encoding result
struct EncodedFrame {
    sk_sp<SkData> png_data;
    bool has_png = false;
};

// Encode frame image to PNG
EncodedFrame encodeFrame(sk_sp<SkImage> image);

// Write encoded frame to file
// Returns 0 on success, 1 on failure
int writeFrameToFile(
    const EncodedFrame& frame,
    int frame_idx,
    const std::string& filename_base
);

#endif // FRAME_ENCODER_H

