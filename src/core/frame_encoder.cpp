#include "frame_encoder.h"
#include "../utils/logging.h"
#include "include/encode/SkPngEncoder.h"
#include "include/encode/SkWebpEncoder.h"
#include "include/core/SkStream.h"
#include <cstdio>

EncodedFrame encodeFrame(
    sk_sp<SkImage> image,
    bool output_png,
    bool output_webp
) {
    EncodedFrame result;
    
    // Encode to PNG if requested (with faster compression)
    if (output_png) {
        SkPngEncoder::Options png_options;
        png_options.fZLibLevel = 1;  // Faster compression (was 6)
        result.png_data = SkPngEncoder::Encode(nullptr, image.get(), png_options);
        result.has_png = (result.png_data != nullptr);
    }
    
    // Encode to WebP if requested
    if (output_webp) {
        SkWebpEncoder::Options webp_options;
        webp_options.fCompression = SkWebpEncoder::Compression::kLossless;
        webp_options.fQuality = 100;
        result.webp_data = SkWebpEncoder::Encode(nullptr, image.get(), webp_options);
        result.has_webp = (result.webp_data != nullptr);
    }
    
    return result;
}

int writeFrameToFile(
    const EncodedFrame& frame,
    int frame_idx,
    const std::string& filename_base,
    bool output_png,
    bool output_webp
) {
    char filename[512];
    int errors = 0;
    
    // Write PNG file
    if (output_png && frame.has_png) {
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
    }
    
    // Write WebP file
    if (output_webp && frame.has_webp) {
        snprintf(filename, sizeof(filename), "%s%05d.webp", filename_base.c_str(), frame_idx);
        
        SkFILEWStream webp_file_stream(filename);
        if (!webp_file_stream.isValid()) {
            LOG_CERR("[ERROR] Could not open WebP output file: " << filename) << std::endl;
            if (!output_png || !frame.has_png) {
                errors++;
            }
        } else {
            if (!webp_file_stream.write(frame.webp_data->data(), frame.webp_data->size())) {
                LOG_CERR("[ERROR] Failed to write WebP data for frame " << frame_idx) << std::endl;
                if (!output_png || !frame.has_png) {
                    errors++;
                }
            } else if (frame_idx == 0) {
                LOG_DEBUG("Frame " << frame_idx << " WebP written to " << filename);
            }
        }
    }
    
    return errors;
}

