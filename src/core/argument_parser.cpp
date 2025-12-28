#include "argument_parser.h"
#include "../utils/logging.h"
#include <filesystem>
#include <string>
#include <iostream>

void printUsage(const char* program_name) {
    LOG_CERR("Usage: " << program_name << " [--png] [--webp] [--stream] [--debug] [--text-config <config.json>] <input.json> <output_dir> [fps]") << std::endl;
    LOG_CERR("  --png:         Output frames as PNG files") << std::endl;
    LOG_CERR("  --webp:        Output frames as WebP files") << std::endl;
    LOG_CERR("  --stream:      Stream frames to stdout (for piping to ffmpeg)") << std::endl;
    LOG_CERR("  --debug:       Enable debug output") << std::endl;
    LOG_CERR("  --text-config: Path to text configuration JSON (for auto-fit and dynamic text values)") << std::endl;
    LOG_CERR("  fps:           Frames per second for output (default: 25)") << std::endl;
    LOG_CERR("") << std::endl;
    LOG_CERR("At least one of --png or --webp must be specified.") << std::endl;
    LOG_CERR("When --stream is used, output_dir can be '-' or any value (ignored).") << std::endl;
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
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 1;
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

