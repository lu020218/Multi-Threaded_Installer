#include "common/logging/console_sink.h"
#include <iostream>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#undef ERROR  // Undefine Windows ERROR macro
#undef min
#undef max
#else
#include <unistd.h>
#endif

namespace logging {

ConsoleSink::ConsoleSink() 
    : LogSink("ConsoleSink")
    , colorEnabled_(true)
    , outputStream_(&std::cout)
    , errorStream_(&std::cerr)
    , colorManager_(ColorManager::getInstance()) {
    colorEnabled_ = detectColorSupport();
}

ConsoleSink::ConsoleSink(bool enableColor)
    : LogSink("ConsoleSink")
    , colorEnabled_(enableColor && detectColorSupport())
    , outputStream_(&std::cout)
    , errorStream_(&std::cerr)
    , colorManager_(ColorManager::getInstance()) {
}

ConsoleSink::ConsoleSink(const std::string& name, bool enableColor)
    : LogSink(name)
    , colorEnabled_(enableColor && detectColorSupport())
    , outputStream_(&std::cout)
    , errorStream_(&std::cerr)
    , colorManager_(ColorManager::getInstance()) {
}

void ConsoleSink::write(const LogMessage& message) {
    // 检查是否应该记录此级别的日志
    if (!shouldLog(message.level)) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(writeMutex_);
    
    // 选择合适的输出流
    std::ostream* stream = selectOutputStream(message.level);
    
    // 格式化消息
    std::string formattedMessage = formatter_.formatForConsole(message, colorEnabled_);
    
    // 写入消息
    *stream << formattedMessage << std::endl;
}

void ConsoleSink::flush() {
    std::lock_guard<std::mutex> lock(writeMutex_);
    
    if (outputStream_) {
        outputStream_->flush();
    }
    if (errorStream_ && errorStream_ != outputStream_) {
        errorStream_->flush();
    }
}

void ConsoleSink::setColorEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    colorEnabled_ = enabled && detectColorSupport();
}

bool ConsoleSink::isColorEnabled() const {
    std::lock_guard<std::mutex> lock(writeMutex_);
    return colorEnabled_;
}

void ConsoleSink::setOutputStream(std::ostream* stream) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    outputStream_ = stream ? stream : &std::cout;
}

void ConsoleSink::setErrorStream(std::ostream* stream) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    errorStream_ = stream ? stream : &std::cerr;
}

bool ConsoleSink::detectColorSupport() {
#ifdef _WIN32
    // Windows 控制台彩色支持检测
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    DWORD mode = 0;
    if (!GetConsoleMode(hConsole, &mode)) {
        return false;
    }
    
    // 尝试启用虚拟终端处理
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(hConsole, mode)) {
        return true;
    }
    
    // 检查是否在支持ANSI的终端中运行
    const char* term = std::getenv("TERM");
    if (term) {
        std::string termStr(term);
        if (termStr.find("xterm") != std::string::npos ||
            termStr.find("color") != std::string::npos) {
            return true;
        }
    }
    
    return false;
#else
    // Unix/Linux 系统彩色支持检测
    
    // 检查是否连接到终端
    if (!isatty(STDOUT_FILENO)) {
        return false;
    }
    
    // 检查TERM环境变量
    const char* term = std::getenv("TERM");
    if (!term) {
        return false;
    }
    
    std::string termStr(term);
    
    // 检查常见的支持彩色的终端类型
    if (termStr.find("xterm") != std::string::npos ||
        termStr.find("color") != std::string::npos ||
        termStr.find("ansi") != std::string::npos ||
        termStr.find("screen") != std::string::npos ||
        termStr.find("tmux") != std::string::npos ||
        termStr == "linux") {
        return true;
    }
    
    // 检查COLORTERM环境变量
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm) {
        return true;
    }
    
    return false;
#endif
}

std::ostream* ConsoleSink::selectOutputStream(LogLevel level) {
    // ERROR 和 CRITICAL 级别使用错误流，其他使用标准输出流
    if (level >= LogLevel::ERROR) {
        return errorStream_;
    }
    return outputStream_;
}

std::string ConsoleSink::getColorCode(LogLevel level) {
    if (!colorEnabled_) {
        return "";
    }
    return colorManager_.getLogLevelColor(level);
}

std::string ConsoleSink::getResetCode() {
    if (!colorEnabled_) {
        return "";
    }
    return colorManager_.resetCode();
}

} // namespace logging