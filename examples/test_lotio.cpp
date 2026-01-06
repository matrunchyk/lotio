#include <iostream>
#include <lotio/core/animation_setup.h>
#include <lotio/core/renderer.h>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.json> <output_dir>" << std::endl;
        return 1;
    }

    std::string input_file = argv[1];
    std::string output_dir = argv[2];
    std::string layer_overrides = (argc > 3) ? argv[3] : "";

    std::cout << "Loading animation from: " << input_file << std::endl;

    // Setup and create animation
    AnimationSetupResult result = setupAndCreateAnimation(input_file, layer_overrides);
    
    if (!result.success()) {
        std::cerr << "Failed to load animation" << std::endl;
        return 1;
    }

    std::cout << "Animation loaded successfully!" << std::endl;
    std::cout << "Duration: " << result.animation->duration() << " seconds" << std::endl;
    std::cout << "FPS: " << result.animation->fps() << std::endl;
    
    SkSize size = result.animation->size();
    std::cout << "Size: " << size.width() << "x" << size.height() << std::endl;

    // Configure rendering
    RenderConfig config;
    config.stream_mode = false;
    config.output_dir = output_dir;
    config.fps = 30.0f;

    std::cout << "Rendering frames to: " << output_dir << std::endl;

    // Render frames
    int render_result = renderFrames(
        result.animation,
        result.builder,
        result.processed_json,
        config
    );

    if (render_result == 0) {
        std::cout << "Rendering completed successfully!" << std::endl;
        return 0;
    } else {
        std::cerr << "Rendering failed" << std::endl;
        return 1;
    }
}
