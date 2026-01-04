// Render all frames of a Lottie animation to PNG and/or WebP files (frame-by-frame)
// This is used as input for video encoding with ffmpeg
// Usage: lotio [--png] [--webp] <input.json> <output_dir> [fps]

#include "utils/logging.h"
#include "utils/crash_handler.h"
#include "core/argument_parser.h"
#include "core/animation_setup.h"
#include "core/renderer.h"

int main(int argc, char* argv[]) {
    installCrashHandlers();
    installExceptionHandlers();

    // Parse command-line arguments
    Arguments args;
    int parse_result = parseArguments(argc, argv, args);
    if (parse_result == 2) {
        // Help was shown - exit successfully
        return 0;
    }
    if (parse_result != 0) {
        // Parse error - exit with error
        return 1;
    }
    
    // Set global flags (affect logging behavior)
    g_stream_mode = args.stream_mode;
    g_debug_mode = args.debug_mode;

    // Setup and create animation
    AnimationSetupResult setup_result = setupAndCreateAnimation(
        args.input_file, 
        args.text_config_file,
        args.text_padding,
        args.text_measurement_mode
    );
    if (!setup_result.success()) {
        return 1;
    }

    // Configure rendering
    RenderConfig render_config;
    render_config.output_png = args.output_png;
    render_config.output_webp = args.output_webp;
    render_config.stream_mode = args.stream_mode;
    render_config.output_dir = args.output_dir;
    render_config.fps = args.fps;

    // Render all frames
    return renderFrames(
        setup_result.animation,
        setup_result.builder,
        setup_result.processed_json,
        render_config
    );
}
