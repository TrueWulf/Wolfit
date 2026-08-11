#include "detect.h"

#include <cstring>

const char *detect_language(const std::string &path) {
    const std::string::size_type dot = path.find_last_of('.');
    if (dot == std::string::npos) return "Plain Text";
    const char *extension = path.c_str() + dot;
    if (!std::strcmp(extension, ".cpp") || !std::strcmp(extension, ".cc") ||
        !std::strcmp(extension, ".cxx") || !std::strcmp(extension, ".hpp") || !std::strcmp(extension, ".h")) return "C++";
    if (!std::strcmp(extension, ".c")) return "C";
    if (!std::strcmp(extension, ".zig")) return "Zig";
    if (!std::strcmp(extension, ".rs")) return "Rust";
    if (!std::strcmp(extension, ".py")) return "Python";
    if (!std::strcmp(extension, ".sh")) return "Shell";
    if (!std::strcmp(extension, ".json")) return "JSON";
    if (!std::strcmp(extension, ".md")) return "Markdown";
    return "Plain Text";
}
