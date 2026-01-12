#include "common/logging/message_formatter.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>

namespace logging {

MessageFormatter::MessageFormatter(const FormatConfig& config)
    : config_(config)
{
}

std::string MessageFormatter::format(const LogMessage& message) {
    return formatForConsole(message, false);
}

std::string MessageFormatter::formatForConsole(const LogMessage& message, bool useColor) {
    std::ostringstream oss;
    
    // 时间戳
    oss << formatTimestamp(message.timestamp);
    
    // 日志级别
    oss << " [" << formatLogLevel(message.level, useColor) << "]";
    
    // 模块名称（如果启用）
    if (config_.includeModuleName && !message.moduleName.empty()) {
        oss << " [" << padString(message.moduleName, 15) << "]";
    }
    
    // 线程ID（如果启用）
    if (config_.includeThreadId) {
        oss << " [" << formatThreadId(message.threadId) << "]";
    }
    
    // 源码位置（如果启用）
    if (config_.includeSourceLocation && !message.fileName.empty()) {
        oss << " [" << message.fileName << ":" << message.lineNumber;
        if (!message.functionName.empty()) {
            oss << " " << message.functionName << "()";
        }
        oss << "]";
    }
    
    // 消息内容
    oss << " ";
    
    // 处理多行消息对齐
    std::string prefix = std::string(oss.str().length(), ' ');
    std::string alignedMessage = alignMultilineMessage(message.message, prefix);
    oss << alignedMessage;
    
    // 性能信息（如果有）
    if (message.hasPerformanceInfo()) {
        oss << " [耗时: " << message.duration->count() << "μs";
        if (message.memoryUsage.has_value()) {
            oss << ", 内存: " << *message.memoryUsage << " bytes";
        }
        oss << "]";
    }
    
    return oss.str();
}

std::string MessageFormatter::formatForFile(const LogMessage& message) {
    std::ostringstream oss;
    
    // 时间戳
    oss << formatTimestamp(message.timestamp);
    
    // 日志级别
    oss << " [" << formatLogLevel(message.level, false) << "]";
    
    // 模块名称
    if (!message.moduleName.empty()) {
        oss << " [" << message.moduleName << "]";
    }
    
    // 线程ID
    oss << " [TID:" << formatThreadId(message.threadId) << "]";
    
    // 源码位置（文件日志总是包含）
    if (!message.fileName.empty()) {
        oss << " [" << message.fileName << ":" << message.lineNumber;
        if (!message.functionName.empty()) {
            oss << " " << message.functionName << "()";
        }
        oss << "]";
    }
    
    // 消息内容
    oss << " " << message.message;
    
    // 性能信息（如果有）
    if (message.hasPerformanceInfo()) {
        oss << " [Performance: duration=" << message.duration->count() << "μs";
        if (message.memoryUsage.has_value()) {
            oss << ", memory=" << *message.memoryUsage << "bytes";
        }
        oss << "]";
    }
    
    return oss.str();
}

void MessageFormatter::setConfig(const FormatConfig& config) {
    config_ = config;
}

std::string MessageFormatter::formatTimestamp(const std::chrono::system_clock::time_point& timestamp) {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

std::string MessageFormatter::formatLogLevel(LogLevel level, bool useColor) {
    std::string levelStr = logLevelToString(level);
    
    if (useColor) {
        return getLogLevelColorCode(level) + padString(levelStr, 5) + getResetColorCode();
    } else {
        return padString(levelStr, 5);
    }
}

std::string MessageFormatter::formatThreadId(std::thread::id threadId) {
    std::ostringstream oss;
    oss << threadId;
    std::string threadIdStr = oss.str();
    
    // 截取线程ID的后6位以保持简洁
    if (threadIdStr.length() > 6) {
        threadIdStr = threadIdStr.substr(threadIdStr.length() - 6);
    }
    
    return threadIdStr;
}

std::string MessageFormatter::padString(const std::string& str, size_t width, char padChar) {
    if (str.length() >= width) {
        return str;
    }
    
    size_t padding = width - str.length();
    return str + std::string(padding, padChar);
}

std::string MessageFormatter::alignMultilineMessage(const std::string& message, const std::string& prefix) {
    std::istringstream iss(message);
    std::string line;
    std::ostringstream result;
    bool firstLine = true;
    
    while (std::getline(iss, line)) {
        if (!firstLine) {
            result << "\n" << prefix;
        }
        result << line;
        firstLine = false;
    }
    
    return result.str();
}

std::string MessageFormatter::getLogLevelColorCode(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:    return "\033[90m";  // 灰色
        case LogLevel::INFO:     return "\033[37m";  // 白色
        case LogLevel::WARNING:  return "\033[33m";  // 黄色
        case LogLevel::ERROR:    return "\033[31m";  // 红色
        case LogLevel::CRITICAL: return "\033[91m";  // 亮红色
        default:                 return "\033[37m";  // 默认白色
    }
}

std::string MessageFormatter::getResetColorCode() {
    return "\033[0m";
}

} // namespace logging