#include "argument_parser.h"
#include "../utils/logging.h"
#include <filesystem>
#include <string>
#include <iostream>
#include <algorithm>
#include <cctype>

void printUsage(const char* program_name) {
    LOG_CERR("Usage: " << program_name << " [--png] [--webp] [--stream] [--debug] [--text-config <config.json>] [--text-padding <0.0-1.0>] [--text-measurement-mode <fast|accurate|pixel-perfect>] <input.json> <output_dir> [fps]") << std::endl;
    LOG_CERR("  --png:                  Output frames as PNG files") << std::endl;
    LOG_CERR("  --webp:                 Output frames as WebP files") << std::endl;
    LOG_CERR("  --stream:               Stream frames to stdout (for piping to ffmpeg)") << std::endl;
    LOG_CERR("  --debug:                Enable debug output") << std::endl;
    LOG_CERR("  --text-config:          Path to text configuration JSON (for auto-fit and dynamic text values)") << std::endl;
    LOG_CERR("  --text-padding:         Text padding factor (0.0-1.0, default: 0.97 = 3% padding)") << std::endl;
    LOG_CERR("  --text-measurement-mode: Text measurement mode (fast|accurate|pixel-perfect, default: accurate)") << std::endl;
    LOG_CERR("                          fast: Fastest, basic accuracy") << std::endl;
    LOG_CERR("                          accurate: Good balance, accounts for kerning and glyph metrics") << std::endl;
    LOG_CERR("                          pixel-perfect: Most accurate, accounts for anti-aliasing") << std::endl;
    LOG_CERR("  --version:              Print version information and exit") << std::endl;
    LOG_CERR("  --help, -h:             Show this help message") << std::endl;
    LOG_CERR("  fps:                    Frames per second for output (default: 25)") << std::endl;
    LOG_CERR("") << std::endl;
    LOG_CERR("At least one of --png or --webp must be specified.") << std::endl;
    LOG_CERR("When --stream is used, output_dir can be '-' or any value (ignored).") << std::endl;
}

void printVersion() {
    #ifdef VERSION
    std::cout << "lotio version " << VERSION << std::endl;
    #else
    std::cout << "lotio version dev" << std::endl;
    #endif
}

int parseArguments(int argc, char* argv[], Arguments& args) {
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--png") {
            args.output_png = true;
        } else if (arg == "--webp") {
            args.output_webp = true;
        } else if (arg == "--stream") {
            args.stream_mode = true;
        } else if (arg == "--debug") {
            args.debug_mode = true;
        } else if (arg == "--text-config") {
            if (i + 1 < argc) {
                args.text_config_file = argv[++i];
            } else {
                LOG_CERR("Error: --text-config requires a file path") << std::endl;
                return 1;
            }
        } else if (arg == "--text-padding") {
            if (i + 1 < argc) {
                try {
                    args.text_padding = std::stof(argv[++i]);
                    if (args.text_padding < 0.0f || args.text_padding > 1.0f) {
                        LOG_CERR("Error: --text-padding must be between 0.0 and 1.0") << std::endl;
                        return 1;
                    }
                } catch (...) {
                    LOG_CERR("Error: Invalid --text-padding value: " << argv[i]) << std::endl;
                    return 1;
                }
            } else {
                LOG_CERR("Error: --text-padding requires a value") << std::endl;
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
                    LOG_CERR("Error: Invalid --text-measurement-mode value: " << argv[i]) << std::endl;
                    LOG_CERR("  Valid values: fast, accurate, pixel-perfect") << std::endl;
                    return 1;
                }
            } else {
                LOG_CERR("Error: --text-measurement-mode requires a value") << std::endl;
                return 1;
            }
        } else if (arg == "--version") {
            args.show_version = true;
            // Return a special value to indicate version was shown
            return 3;  // Version shown - should exit successfully
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
                } catch (...) {
                    LOG_CERR("Error: Invalid fps value: " << arg) << std::endl;
                    return 1;
                }
            }
        } else {
            LOG_CERR("Error: Unknown option: " << arg) << std::endl;
            LOG_CERR("Use --help for usage information.") << std::endl;
            return 1;
        }
    }

    // Handle version flag early (before validation)
    if (args.show_version) {
        printVersion();
        return 3;  // Version shown - should exit successfully
    }

    // Validate arguments
    if (!args.output_png && !args.output_webp) {
        LOG_CERR("Error: At least one of --png or --webp must be specified.") << std::endl;
        LOG_CERR("Use --help for usage information.") << std::endl;
        return 1;
    }
    
    if (args.stream_mode && !args.output_png) {
        LOG_CERR("Error: Streaming mode requires --png (ffmpeg image2pipe expects PNG format).") << std::endl;
        LOG_CERR("Use --help for usage information.") << std::endl;
        return 1;
    }

    if (args.input_file.empty()) {
        LOG_CERR("Error: Missing input file.") << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Validate input file exists and is a file (not a directory)
    std::filesystem::path input_path(args.input_file);
    if (!std::filesystem::exists(input_path)) {
        LOG_CERR("Error: Input file does not exist: " << args.input_file) << std::endl;
        return 1;
    }
    if (!std::filesystem::is_regular_file(input_path)) {
        LOG_CERR("Error: Input path is not a file (is it a directory?): " << args.input_file) << std::endl;
        return 1;
    }

    // Handle output directory (not needed in stream mode)
    if (!args.stream_mode) {
        if (args.output_dir.empty()) {
            LOG_CERR("Error: Missing output directory (use '-' for streaming mode).") << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        
        // Create output directory if it doesn't exist
        std::filesystem::path output_path(args.output_dir);
        if (!std::filesystem::exists(output_path)) {
            std::error_code ec;
            if (!std::filesystem::create_directories(output_path, ec)) {
                LOG_CERR("Error: Could not create output directory: " << args.output_dir) << std::endl;
                LOG_CERR("  " << ec.message()) << std::endl;
                return 1;
            }
            LOG_DEBUG("Created output directory: " << args.output_dir);
        } else if (!std::filesystem::is_directory(output_path)) {
            LOG_CERR("Error: Output path exists but is not a directory: " << args.output_dir) << std::endl;
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

