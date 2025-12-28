#include "string_utils.h"

size_t replaceAllInPlace(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return 0;
    size_t count = 0;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
        count++;
    }
    return count;
}

size_t replaceCharInPlace(std::string& s, char from, char to) {
    size_t count = 0;
    for (auto& ch : s) {
        if (ch == from) {
            ch = to;
            count++;
        }
    }
    return count;
}

std::string escapeRegex(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '\\' || c == '^' || c == '$' || c == '.' || c == '|' || 
            c == '?' || c == '*' || c == '+' || c == '(' || c == ')' || 
            c == '[' || c == '{') {
            result += '\\';
        }
        result += c;
    }
    return result;
}

