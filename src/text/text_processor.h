#ifndef TEXT_PROCESSOR_H
#define TEXT_PROCESSOR_H

#include <string>

// Process JSON with text configuration (auto-fit and dynamic text values)
// Returns processed JSON string, or empty string on error
std::string processTextConfiguration(
    std::string& json_data,
    const std::string& text_config_file
);

#endif // TEXT_PROCESSOR_H

