#ifndef ARGUMENT_PARSER_H
#define ARGUMENT_PARSER_H

#include <string>
#include "../text/font_utils.h"

// Command-line arguments structure
struct Arguments {
    bool stream_mode = false;
    bool debug_mode = false;
    bool show_version = false;  // --version flag
    std::string input_file;
    std::string output_dir;
    std::string layer_overrides_file;
    float fps = 30.0f;
    bool fps_explicitly_set = false;  // Track if fps was provided on command line
    float text_padding = 0.97f;  // Text padding factor (0.0-1.0), default 0.97 (3% padding)
    TextMeasurementMode text_measurement_mode = TextMeasurementMode::ACCURATE;  // Text measurement mode
};

// Parse command-line arguments
// Returns 0 on success, 1 on error (and prints error message)
int parseArguments(int argc, char* argv[], Arguments& args);

// Print usage/help message
void printUsage(const char* program_name);

// Print version information
void printVersion();

#endif // ARGUMENT_PARSER_H

