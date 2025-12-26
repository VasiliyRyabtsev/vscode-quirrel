#include "utils.h"

std::string escapeJson(const char* s) {
    if (!s) return "";

    // Count length and special chars to reserve exact size
    size_t len = 0, extra = 0;
    for (const char* p = s; *p; ++p, ++len) {
        if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r' || *p == '\t')
            ++extra;
    }

    std::string result;
    result.reserve(len + extra);

    for (const char* p = s; *p; ++p) {
        switch (*p) {
            case '"':  result.append("\\\"", 2); break;
            case '\\': result.append("\\\\", 2); break;
            case '\n': result.append("\\n", 2); break;
            case '\r': result.append("\\r", 2); break;
            case '\t': result.append("\\t", 2); break;
            default:   result.push_back(*p); break;
        }
    }
    return result;
}
