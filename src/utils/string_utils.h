#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>

// Helper functions for string manipulation
size_t replaceAllInPlace(std::string& s, const std::string& from, const std::string& to);
size_t replaceCharInPlace(std::string& s, char from, char to);
std::string escapeRegex(const std::string& str);

#endif // STRING_UTILS_H

