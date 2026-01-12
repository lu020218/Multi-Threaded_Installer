#pragma once

// Undefine Windows macros that might interfere
#ifdef DEBUG
#undef DEBUG
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef INFO
#undef INFO
#endif

#include <string>

namespace logging {

/**
 * Log level enumeration
 * Defines five log levels for categorizing message importance
 */
enum class LogLevel : int {
    DEBUG = 0,      // Debug information
    INFO = 1,       // General information  
    WARNING = 2,    // Warning information
    ERROR = 3,      // Error information
    CRITICAL = 4    // Critical error
};

/**
 * Convert log level to string
 * @param level Log level
 * @return Corresponding string representation
 */
const char* logLevelToString(LogLevel level);

/**
 * Convert string to log level
 * @param levelStr Log level string
 * @return Corresponding log level, returns INFO if invalid
 */
LogLevel stringToLogLevel(const std::string& levelStr);

/**
 * Check if specified level should be logged
 * @param current Current message level
 * @param threshold Threshold level
 * @return true if current >= threshold
 */
bool isLevelEnabled(LogLevel current, LogLevel threshold);

} // namespace logging