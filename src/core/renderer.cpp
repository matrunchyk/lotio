#include "renderer.h"
#include "frame_encoder.h"
#include "../utils/logging.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkImage.h"
#include "include/core/SkData.h"
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <iostream>

// Frame buffer for streaming mode (ensures sequential output)
struct BufferedFrame {
    int frame_idx;
    sk_sp<SkData> png_data;
    bool ready;
    
    BufferedFrame() : frame_idx(-1), ready(false) {}
};

int renderFrames(
    sk_sp<skottie::Animation> animation,
    skottie::Animation::Builder& builder,
    const std::string& json_data,
    const RenderConfig& config
) {
    // Get animation dimensions and duration
    SkSize size = animation->size();
    int width = static_cast<int>(size.width());
    int height = static_cast<int>(size.height());
    float duration = animation->duration();
    float animation_fps = animation->fps();

    LOG_DEBUG("Animation loaded: " << width << "x" << height);
    LOG_DEBUG("Duration: " << duration << " seconds");
    LOG_DEBUG("Animation FPS: " << animation_fps);
    LOG_DEBUG("Output FPS: " << config.fps);

    // Calculate number of frames to render
    int num_frames = static_cast<int>(std::ceil(duration * config.fps));
    LOG_DEBUG("Rendering " << num_frames << " frames...");

    // Create a surface to render to with transparent background
    // Use kUnpremul_SkAlphaType to preserve transparency better
    LOG_DEBUG("Creating Skia surface: " << width << "x" << height << " with kUnpremul_SkAlphaType");
    SkImageInfo info = SkImageInfo::MakeN32(width, height, kUnpremul_SkAlphaType);
    
    // CRITICAL: Allocate pixel buffer explicitly initialized to transparent
    // This ensures the surface starts with transparent pixels, not black
    size_t rowBytes = info.minRowBytes();
    size_t totalBytes = info.computeByteSize(rowBytes);

    // Create RGBA conversion surface once (reuse for all frames)
    SkImageInfo rgbaInfo = SkImageInfo::MakeN32(width, height, kUnpremul_SkAlphaType);
    auto rgbaSurface = SkSurfaces::Raster(rgbaInfo);
    if (!rgbaSurface) {
        LOG_CERR("[ERROR] Failed to create RGBA conversion surface") << std::endl;
        return 1;
    }
    LOG_DEBUG("RGBA conversion surface created (will be reused for all frames)");

    // Determine number of threads for parallel rendering
    int num_threads = std::max(1, (int)std::thread::hardware_concurrency());
    LOG_DEBUG("Using " << num_threads << " threads for parallel rendering");

    // Create per-thread animations and surfaces
    std::vector<sk_sp<skottie::Animation>> thread_animations;
    std::vector<sk_sp<SkSurface>> thread_surfaces;
    std::vector<sk_sp<SkSurface>> thread_rgba_surfaces;
    std::vector<std::vector<uint8_t>> thread_pixel_buffers;

    for (int t = 0; t < num_threads; t++) {
        // Create animation for each thread (thread-safe: each thread has its own)
        LOG_DEBUG("Creating animation for thread " << t << "...");
        auto thread_animation = builder.make(json_data.c_str(), json_data.length());
        if (!thread_animation) {
            LOG_CERR("[ERROR] Failed to create animation for thread " << t) << std::endl;
            LOG_CERR("[ERROR] This may indicate JSON parsing issues or resource loading failures") << std::endl;
            LOG_CERR("[ERROR] Check if images are accessible and JSON is valid") << std::endl;
            return 1;
        }
        thread_animations.push_back(thread_animation);
        LOG_DEBUG("Animation created successfully for thread " << t);
        
        // Create surface for each thread
        std::vector<uint8_t> thread_pixels(totalBytes, 0);
        thread_pixel_buffers.push_back(std::move(thread_pixels));
        auto thread_surface = SkSurfaces::WrapPixels(info, thread_pixel_buffers[t].data(), rowBytes, nullptr);
        if (!thread_surface) {
            LOG_CERR("[ERROR] Failed to create surface for thread " << t) << std::endl;
            LOG_CERR("[ERROR] This may indicate insufficient memory or invalid surface parameters") << std::endl;
            return 1;
        }
        thread_surfaces.push_back(thread_surface);
        
        // Create RGBA conversion surface for each thread
        auto thread_rgba_surface = SkSurfaces::Raster(rgbaInfo);
        if (!thread_rgba_surface) {
            LOG_CERR("[ERROR] Failed to create RGBA surface for thread " << t) << std::endl;
            LOG_CERR("[ERROR] This may indicate insufficient memory for image conversion") << std::endl;
            return 1;
        }
        thread_rgba_surfaces.push_back(thread_rgba_surface);
        LOG_DEBUG("Thread " << t << " setup complete - ready for rendering");
    }
    LOG_DEBUG("All " << num_threads << " threads initialized successfully");

    // Pre-compute frame times (avoid per-frame calculation)
    std::vector<float> frame_times(num_frames);
    for (int i = 0; i < num_frames; i++) {
        frame_times[i] = (i < num_frames - 1) ? (float)i / (num_frames - 1) * duration : duration;
    }

    // Pre-distribute frames to threads (round-robin for better load balancing)
    std::vector<std::vector<int>> thread_frames(num_threads);
    for (int i = 0; i < num_frames; i++) {
        thread_frames[i % num_threads].push_back(i);
    }

    // Pre-compute filename base to avoid repeated string operations
    std::string filename_base = config.stream_mode ? "" : (config.output_dir + "/frame_");

    std::atomic<int> completed_frames(0);
    std::atomic<int> failed_frames(0);
    std::mutex progress_mutex;  // Mutex for thread-safe progress reporting

    // Frame buffer for streaming mode (ensures sequential output)
    std::vector<BufferedFrame> frame_buffer;
    std::mutex buffer_mutex;
    std::condition_variable buffer_cv;
    int next_frame_to_write = 0;
    bool streaming_complete = false;
    
    if (config.stream_mode) {
        frame_buffer.resize(num_frames);
        LOG_DEBUG("Frame buffer allocated for " << num_frames << " frames");
    }

    // Worker function for parallel frame rendering
    auto render_frame_worker = [&](int thread_id) {
        auto& animation = thread_animations[thread_id];
        auto& surface = thread_surfaces[thread_id];
        auto& rgba_surface = thread_rgba_surfaces[thread_id];
        auto* canvas = surface->getCanvas();
        
        // Thread-local progress counter to reduce atomic contention
        thread_local int local_completed = 0;
        local_completed = 0;
        
        // Process pre-assigned frames
        for (int frame_idx : thread_frames[thread_id]) {
            // Use pre-computed frame time
            float t = frame_times[frame_idx];
            
            // Clear canvas with transparent background
            canvas->clear(SK_ColorTRANSPARENT);

            // Seek to the desired frame time
            animation->seekFrameTime(t);
            
            // Render the animation frame (this will render all layers including images)
            if (frame_idx == 0 && thread_id == 0) {
                LOG_DEBUG("Rendering frame " << frame_idx << " at time " << t << " seconds");
                LOG_DEBUG("Rendering animation (images will be drawn if present in layers)...");
            }
            animation->render(canvas);
            
            if (frame_idx == 0 && thread_id == 0) {
                LOG_DEBUG("Frame " << frame_idx << " rendered successfully");
            }

            // Get the image from the surface
            sk_sp<SkImage> image = surface->makeImageSnapshot();
            if (!image) {
                LOG_CERR("[ERROR] Failed to create image snapshot for frame " << frame_idx) << std::endl;
                LOG_CERR("[ERROR] This may indicate a rendering issue or memory problem") << std::endl;
                failed_frames++;
                continue;
            }
            
            // Get image info once (reuse for debug and conversion check)
            SkImageInfo imgInfo = image->imageInfo();
            
            // Debug output for first frame
            if (frame_idx == 0 && thread_id == 0) {
                LOG_DEBUG("Image snapshot created: " << image->width() << "x" << image->height());
                LOG_DEBUG("Image color type: " << imgInfo.colorType() << ", alpha type: " << imgInfo.alphaType());
                LOG_DEBUG("Image has alpha: " << (imgInfo.alphaType() != kOpaque_SkAlphaType));
                LOG_DEBUG("Rendered image ready for encoding");
            }
            
            // Periodic debug output for image snapshots
            if (frame_idx > 0 && frame_idx % 100 == 0 && thread_id == 0) {
                LOG_DEBUG("Rendered and snapped " << frame_idx << " frames (images included if present)");
            }
            
            // Check if conversion is needed (only convert if necessary)
            bool needs_conversion = (imgInfo.colorType() != kN32_SkColorType || 
                                     imgInfo.alphaType() != kUnpremul_SkAlphaType);
            
            if (needs_conversion) {
                if (frame_idx == 0 && thread_id == 0) {
                    LOG_DEBUG("Image conversion needed: colorType=" << imgInfo.colorType() << " (expected " << kN32_SkColorType << "), alphaType=" << imgInfo.alphaType() << " (expected " << kUnpremul_SkAlphaType << ")");
                }
                // Convert to RGBA_8888 with kUnpremul_SkAlphaType
                rgba_surface->getCanvas()->clear(SK_ColorTRANSPARENT);
                rgba_surface->getCanvas()->drawImage(image, 0, 0, SkSamplingOptions());
                image = rgba_surface->makeImageSnapshot();
                if (!image) {
                    LOG_CERR("[ERROR] Failed to convert image for frame " << frame_idx) << std::endl;
                    LOG_CERR("[ERROR] Image conversion failed - this may indicate a rendering surface issue") << std::endl;
                    failed_frames++;
                    continue;
                }
                if (frame_idx == 0 && thread_id == 0) {
                    LOG_DEBUG("Converted image to RGBA_8888 with kUnpremul_SkAlphaType for encoding");
                    SkImageInfo newInfo = image->imageInfo();
                    LOG_DEBUG("New image color type: " << newInfo.colorType() << ", alpha type: " << newInfo.alphaType());
                    LOG_DEBUG("Image conversion completed successfully");
                }
            } else if (frame_idx == 0 && thread_id == 0) {
                LOG_DEBUG("Image already in correct format - no conversion needed");
            }

            // Encode frame to PNG
            if (frame_idx == 0 && thread_id == 0) {
                LOG_DEBUG("Encoding rendered image to PNG format...");
            }
            EncodedFrame encoded = encodeFrame(image);
            
            // Check encoding results
            if (!encoded.has_png) {
                LOG_CERR("[ERROR] Failed to encode PNG for frame " << frame_idx) << std::endl;
                LOG_CERR("[ERROR] PNG encoding failed - image data may be invalid") << std::endl;
                failed_frames++;
                continue;
            } else if (frame_idx == 0 && thread_id == 0) {
                LOG_DEBUG("PNG encoded successfully: " << encoded.png_data->size() << " bytes");
                LOG_DEBUG("Frame " << frame_idx << " complete: rendered -> snapped -> encoded");
            }

            // Write files or buffer for streaming
            if (config.stream_mode) {
                // Buffer frame for sequential output
                {
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    frame_buffer[frame_idx].frame_idx = frame_idx;
                    frame_buffer[frame_idx].png_data = encoded.png_data;
                    frame_buffer[frame_idx].ready = true;
                }
                buffer_cv.notify_all();
            } else {
                // Write files using frame encoder
                int write_errors = writeFrameToFile(encoded, frame_idx, filename_base);
                if (write_errors > 0) {
                    failed_frames++;
                    continue;
                }
            }

            // Progress reporting (thread-safe to prevent interleaved output)
            local_completed++;
            if (local_completed % 10 == 0) {
                int done = completed_frames.fetch_add(10) + 10;
                if (done % 10 == 0 || done == num_frames) {
                    std::lock_guard<std::mutex> lock(progress_mutex);
                    LOG_DEBUG("Rendered frame " << done << "/" << num_frames);
                }
            }
        }
        
        // Update final count for remaining frames in this thread
        int remainder = local_completed % 10;
        if (remainder > 0) {
            int done = completed_frames.fetch_add(remainder) + remainder;
            if (done == num_frames) {
                std::lock_guard<std::mutex> lock(progress_mutex);
                LOG_DEBUG("Rendered frame " << done << "/" << num_frames);
            }
        }
    };

    // Sequential writer thread for streaming mode
    std::thread writer_thread;
    if (config.stream_mode) {
        writer_thread = std::thread([&]() {
            // Streaming mode outputs PNG (ffmpeg image2pipe expects PNG)
            
            for (int i = 0; i < num_frames; i++) {
                std::unique_lock<std::mutex> lock(buffer_mutex);
                // Wait for next frame to be ready or all frames completed
                while (!frame_buffer[next_frame_to_write].ready && 
                       (completed_frames.load() + failed_frames.load() < num_frames)) {
                    buffer_cv.wait(lock);
                }
                
                // Check if frame is ready
                if (frame_buffer[next_frame_to_write].ready) {
                    auto& frame = frame_buffer[next_frame_to_write];
                    lock.unlock();  // Release lock before I/O
                    
                    if (frame.png_data) {
                        size_t dataSize = frame.png_data->size();
                        if (dataSize == 0) {
                            LOG_CERR("[WARNING] Frame " << next_frame_to_write << " PNG data is empty (0 bytes)") << std::endl;
                        }
                        // Write PNG data to stdout
                        std::cout.write(reinterpret_cast<const char*>(frame.png_data->data()), dataSize);
                        if (!std::cout.good()) {
                            LOG_CERR("[ERROR] Failed to write frame " << next_frame_to_write << " to stdout") << std::endl;
                            LOG_CERR("[ERROR] Check if stdout is still connected (pipe may be broken)") << std::endl;
                            failed_frames++;
                        } else {
                            std::cout.flush();
                        }
                    } else {
                        LOG_CERR("[ERROR] Frame " << next_frame_to_write << " has no PNG data") << std::endl;
                        LOG_CERR("[ERROR] Frame was not encoded successfully - check rendering") << std::endl;
                        failed_frames++;
                    }
                    next_frame_to_write++;
                } else {
                    // Frame not ready and all workers done - might be a failure
                    // Skip this frame and continue
                    LOG_CERR("[WARNING] Frame " << next_frame_to_write << " was not rendered") << std::endl;
                    failed_frames++;
                    next_frame_to_write++;
                }
            }
            streaming_complete = true;
            buffer_cv.notify_all();
        });
    }

    // Launch worker threads
    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads; t++) {
        workers.emplace_back(render_frame_worker, t);
    }

    // Wait for all threads to complete
    for (auto& worker : workers) {
        worker.join();
    }
    
    // Wait for writer thread to complete
    if (config.stream_mode && writer_thread.joinable()) {
        writer_thread.join();
    }

    // Check for failures
    if (failed_frames > 0) {
        LOG_CERR("[WARNING] " << failed_frames << " frames failed to render") << std::endl;
        LOG_CERR("[WARNING] Failed frames may indicate missing images, rendering errors, or encoding issues") << std::endl;
    } else {
        LOG_DEBUG("All " << num_frames << " frames rendered successfully (images included if present)");
    }

    if (!config.stream_mode) {
        std::ostringstream success_msg;
        success_msg << "[INFO] Successfully rendered " << num_frames << " frames to " << config.output_dir << " (PNG format)";
        LOG_COUT(success_msg.str()) << std::endl;
    } else {
        // In stream mode, log to stderr to avoid interfering with stdout PNG data
        LOG_CERR("[INFO] Successfully streamed " << num_frames << " frames to stdout") << std::endl;
    }
    return 0;
}

