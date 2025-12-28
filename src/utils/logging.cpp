#include "logging.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()) % 1000000000;
    
    std::tm tm_buf;
    #ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
    #else
        localtime_r(&time_t, &tm_buf);
    #endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(9) << ns.count();
    return oss.str();
}

// Global flags
bool g_stream_mode = false;
bool g_debug_mode = false;

