#ifndef LOGGING_H
#define LOGGING_H

#include <string>
#include <iostream>

// Global flags
extern bool g_stream_mode;
extern bool g_debug_mode;

// Helper macros for timestamped output
// In stream mode, LOG_COUT uses stderr to avoid corrupting stdout PNG data
#define LOG_COUT(msg) (g_stream_mode ? std::cerr : std::cout) << "[" << getTimestamp() << "] " << msg
#define LOG_CERR(msg) std::cerr << "[" << getTimestamp() << "] " << msg
#define LOG_DEBUG(msg) if (g_debug_mode) { LOG_COUT("[DEBUG] " << msg) << std::endl; }

// Get current timestamp as string in format [YYYY-MM-DD HH:MM:SS.nnnnnnnnn]
std::string getTimestamp();

#endif // LOGGING_H

