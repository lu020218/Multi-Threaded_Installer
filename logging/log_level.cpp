#include "common/logging/log_level.h"
#include <algorithm>
#include <cctype>

namespace logging {

const char* logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARNING";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

LogLevel stringToLogLevel(const std::string& levelStr) {
    // Convert to uppercase for comparison
    std::string upperStr = levelStr;
    std::transform(upperStr.begin(), upperStr.end(), upperStr.begin(), 
                   [](unsigned char c) { return std::toupper(c); });
    
    if (upperStr == "DEBUG") {
        return LogLevel::DEBUG;
    } else if (upperStr == "INFO") {
        return LogLevel::INFO;
    } else if (upperStr == "WARNING" || upperStr == "WARN") {
        return LogLevel::WARNING;
    } else if (upperStr == "ERROR" || upperStr == "ERR") {
        return LogLevel::ERROR;
    } else if (upperStr == "CRITICAL" || upperStr == "CRIT") {
        return LogLevel::CRITICAL;
    } else {
        // Default to INFO level
        return LogLevel::INFO;
    }
}

bool isLevelEnabled(LogLevel current, LogLevel threshold) {
    return static_cast<int>(current) >= static_cast<int>(threshold);
}

} // namespace logging