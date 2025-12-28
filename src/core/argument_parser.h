#ifndef ARGUMENT_PARSER_H
#define ARGUMENT_PARSER_H

#include <string>

// Command-line arguments structure
struct Arguments {
    bool output_png = false;
    bool output_webp = false;
    bool stream_mode = false;
    bool debug_mode = false;
    std::string input_file;
    std::string output_dir;
    std::string text_config_file;
    float fps = 25.0f;
};

// Parse command-line arguments
// Returns 0 on success, 1 on error (and prints error message)
int parseArguments(int argc, char* argv[], Arguments& args);

// Print usage/help message
void printUsage(const char* program_name);

#endif // ARGUMENT_PARSER_H

