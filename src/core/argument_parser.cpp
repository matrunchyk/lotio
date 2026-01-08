#include "argument_parser.h"
#include "../utils/logging.h"
#include "../utils/version.h"
#include <string>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <filesystem>

void printUsage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [--stream] [--debug] [--layer-overrides <config.json>] [--text-padding <0.0-1.0>] [--text-measurement-mode <fast|accurate|pixel-perfect>] <input.json> <output_dir> [fps]" << std::endl;
    std::cerr << "  --stream:               Stream frames to stdout as PNG (for piping to ffmpeg)" << std::endl;
    std::cerr << "  --debug:                Enable debug output" << std::endl;
    std::cerr << "  --layer-overrides:      Path to layer overrides JSON (for text auto-fit, dynamic text values, and image path overrides)" << std::endl;
    std::cerr << "  --text-padding:         Text padding factor (0.0-1.0, default: 0.97 = 3% padding)" << std::endl;
    std::cerr << "  --text-measurement-mode: Text measurement mode (fast|accurate|pixel-perfect, default: accurate)" << std::endl;
    std::cerr << "                          fast: Fastest, basic accuracy" << std::endl;
    std::cerr << "                          accurate: Good balance, accounts for kerning and glyph metrics" << std::endl;
    std::cerr << "                          pixel-perfect: Most accurate, accounts for anti-aliasing" << std::endl;
    std::cerr << "  --version:              Print version information and exit" << std::endl;
    std::cerr << "  --help, -h:             Show this help message" << std::endl;
    std::cerr << "  fps:                    Frames per second for output (default: animation fps or 30)" << std::endl;
    std::cerr << "" << std::endl;
    std::cerr << "When --stream is used, output_dir can be '-' or any value (ignored)." << std::endl;
}

void printVersion() {
    std::cout << "lotio version " << getLotioVersion() << std::endl;
}

int parseArguments(int argc, char* argv[], Arguments& args) {
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--stream") {
            args.stream_mode = true;
        } else if (arg == "--debug") {
            args.debug_mode = true;
        } else if (arg == "--layer-overrides") {
            if (i + 1 < argc) {
                args.layer_overrides_file = argv[++i];
            } else {
                std::cerr << "Error: --layer-overrides requires a file path" << std::endl;
                return 1;
            }
        } else if (arg == "--text-padding") {
            if (i + 1 < argc) {
                try {
                    args.text_padding = std::stof(argv[++i]);
                    if (args.text_padding < 0.0f || args.text_padding > 1.0f) {
                        std::cerr << "Error: --text-padding must be between 0.0 and 1.0" << std::endl;
                        return 1;
                    }
                } catch (...) {
                    std::cerr << "Error: Invalid --text-padding value: " << argv[i] << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --text-padding requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "--text-measurement-mode") {
            if (i + 1 < argc) {
                std::string modeStr = argv[++i];
                // Convert to lowercase for case-insensitive matching
                std::transform(modeStr.begin(), modeStr.end(), modeStr.begin(), ::tolower);
                
                // Handle both "pixel-perfect" and "pixelperfect"
                if (modeStr == "fast") {
                    args.text_measurement_mode = TextMeasurementMode::FAST;
                } else if (modeStr == "accurate") {
                    args.text_measurement_mode = TextMeasurementMode::ACCURATE;
                } else if (modeStr == "pixel-perfect" || modeStr == "pixelperfect") {
                    args.text_measurement_mode = TextMeasurementMode::PIXEL_PERFECT;
                } else {
                    std::cerr << "Error: Invalid --text-measurement-mode value: " << argv[i] << std::endl;
                    std::cerr << "  Valid values: fast, accurate, pixel-perfect" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: --text-measurement-mode requires a value" << std::endl;
                return 1;
            }
        } else if (arg == "--version") {
            args.show_version = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            // Return a special value to indicate help was shown
            // We'll use 2 to distinguish from error (1) and success (0)
            return 2;  // Help shown - should exit successfully
        } else if (arg[0] != '-' || arg == "-") {
            // Positional argument (including "-" which is used for streaming/stdout)
            if (args.input_file.empty()) {
                args.input_file = argv[i];
            } else if (args.output_dir.empty()) {
                args.output_dir = argv[i];
            } else {
                // Try to parse as fps
                try {
                    args.fps = std::stof(arg);
                    args.fps_explicitly_set = true;  // Mark fps as explicitly provided
                } catch (...) {
                    std::cerr << "Error: Invalid fps value: " << arg << std::endl;
                    return 1;
                }
            }
        } else {
            std::cerr << "Error: Unknown option: " << arg << std::endl;
            std::cerr << "Use --help for usage information." << std::endl;
            return 1;
        }
    }

    // Handle version flag early (before validation)
    if (args.show_version) {
        printVersion();
        return 3;  // Version shown - should exit successfully
    }

    // Validate arguments
    if (args.input_file.empty()) {
        std::cerr << "Error: Missing input file." << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Validate input file exists and is a file (not a directory)
    std::filesystem::path input_path(args.input_file);
    if (!std::filesystem::exists(input_path)) {
        std::cerr << "Error: Input file does not exist: " << args.input_file << std::endl;
        return 1;
    }
    if (!std::filesystem::is_regular_file(input_path)) {
        std::cerr << "Error: Input path is not a file (is it a directory?): " << args.input_file << std::endl;
        return 1;
    }

    // Handle output directory (not needed in stream mode)
    if (!args.stream_mode) {
        if (args.output_dir.empty()) {
            std::cerr << "Error: Missing output directory (use '-' for streaming mode)." << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        
        // Create output directory if it doesn't exist
        std::filesystem::path output_path(args.output_dir);
        if (!std::filesystem::exists(output_path)) {
            std::error_code ec;
            if (!std::filesystem::create_directories(output_path, ec)) {
                std::cerr << "Error: Could not create output directory: " << args.output_dir << std::endl;
                std::cerr << "  " << ec.message() << std::endl;
                return 1;
            }
            LOG_DEBUG("Created output directory: " << args.output_dir);
        } else if (!std::filesystem::is_directory(output_path)) {
            std::cerr << "Error: Output path exists but is not a directory: " << args.output_dir << std::endl;
            return 1;
        }
    } else {
        // In stream mode, output_dir is optional (can be "-")
        if (args.output_dir.empty()) {
            args.output_dir = "-";  // Default to "-" for streaming
        }
        LOG_DEBUG("Stream mode enabled - frames will be written to stdout");
    }

    return 0;
}

