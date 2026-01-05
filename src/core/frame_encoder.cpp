#include "frame_encoder.h"
#include "../utils/logging.h"
#include "include/encode/SkPngEncoder.h"
#include "include/core/SkStream.h"
#include <cstdio>

EncodedFrame encodeFrame(sk_sp<SkImage> image) {
    EncodedFrame result;
    
    // Encode to PNG (with faster compression)
    SkPngEncoder::Options png_options;
    png_options.fZLibLevel = 1;  // Faster compression (was 6)
    result.png_data = SkPngEncoder::Encode(nullptr, image.get(), png_options);
    result.has_png = (result.png_data != nullptr);
    
    return result;
}

int writeFrameToFile(
    const EncodedFrame& frame,
    int frame_idx,
    const std::string& filename_base
) {
    char filename[512];
    int errors = 0;
    
    // Write PNG file
    if (!frame.has_png) {
        LOG_CERR("[ERROR] Frame " << frame_idx << " has no PNG data") << std::endl;
        return 1;
    }
    
    snprintf(filename, sizeof(filename), "%s%05d.png", filename_base.c_str(), frame_idx);
    
    SkFILEWStream png_file_stream(filename);
    if (!png_file_stream.isValid()) {
        LOG_CERR("[ERROR] Could not open PNG output file: " << filename) << std::endl;
        errors++;
    } else {
        if (!png_file_stream.write(frame.png_data->data(), frame.png_data->size())) {
            LOG_CERR("[ERROR] Failed to write PNG data for frame " << frame_idx) << std::endl;
            errors++;
        } else if (frame_idx == 0) {
            LOG_DEBUG("Frame " << frame_idx << " PNG written to " << filename);
        }
    }
    
    return errors;
}

