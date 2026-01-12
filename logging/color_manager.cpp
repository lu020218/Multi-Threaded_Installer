#include "common/logging/color_manager.h"
#include <iostream>
#include <cstdlib>
#include <unordered_map>

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

ColorManager& ColorManager::getInstance() {
    static ColorManager instance;
    return instance;
}

ColorManager::ColorManager() : colorEnabled_(true), colorSupported_(false) {
    colorSupported_ = detectColorSupport();
}

bool ColorManager::isColorSupported() {
    return colorSupported_;
}

void ColorManager::setColorEnabled(bool enabled) {
    colorEnabled_ = enabled;
}

bool ColorManager::isColorEnabled() const {
    return colorEnabled_ && colorSupported_;
}

std::string ColorManager::colorCode(Color foreground, Color background) {
    if (!isColorEnabled()) {
        return "";
    }
    
    // ANSI color code mapping
    static const std::unordered_map<Color, int> foregroundCodes = {
        {Color::BLACK, 30}, {Color::RED, 31}, {Color::GREEN, 32}, {Color::YELLOW, 33},
        {Color::BLUE, 34}, {Color::MAGENTA, 35}, {Color::CYAN, 36}, {Color::WHITE, 37},
        {Color::BRIGHT_BLACK, 90}, {Color::BRIGHT_RED, 91}, {Color::BRIGHT_GREEN, 92}, 
        {Color::BRIGHT_YELLOW, 93}, {Color::BRIGHT_BLUE, 94}, {Color::BRIGHT_MAGENTA, 95}, 
        {Color::BRIGHT_CYAN, 96}, {Color::BRIGHT_WHITE, 97}
    };
    
    static const std::unordered_map<Color, int> backgroundCodes = {
        {Color::BLACK, 40}, {Color::RED, 41}, {Color::GREEN, 42}, {Color::YELLOW, 43},
        {Color::BLUE, 44}, {Color::MAGENTA, 45}, {Color::CYAN, 46}, {Color::WHITE, 47},
        {Color::BRIGHT_BLACK, 100}, {Color::BRIGHT_RED, 101}, {Color::BRIGHT_GREEN, 102}, 
        {Color::BRIGHT_YELLOW, 103}, {Color::BRIGHT_BLUE, 104}, {Color::BRIGHT_MAGENTA, 105}, 
        {Color::BRIGHT_CYAN, 106}, {Color::BRIGHT_WHITE, 107}
    };
    
    std::string result = "\033[";
    
    auto fgIt = foregroundCodes.find(foreground);
    if (fgIt != foregroundCodes.end()) {
        result += std::to_string(fgIt->second);
    }
    
    if (background != Color::BLACK) {
        auto bgIt = backgroundCodes.find(background);
        if (bgIt != backgroundCodes.end()) {
            if (fgIt != foregroundCodes.end()) {
                result += ";";
            }
            result += std::to_string(bgIt->second);
        }
    }
    
    result += "m";
    return result;
}

std::string ColorManager::styleCode(Style style) {
    if (!isColorEnabled()) {
        return "";
    }
    
    static const std::unordered_map<Style, int> styleCodes = {
        {Style::RESET, 0}, {Style::BOLD, 1}, {Style::DIM, 2}, {Style::ITALIC, 3},
        {Style::UNDERLINE, 4}, {Style::BLINK, 5}, {Style::REVERSE, 7}, {Style::STRIKETHROUGH, 9}
    };
    
    auto it = styleCodes.find(style);
    if (it != styleCodes.end()) {
        return generateAnsiCode(it->second);
    }
    
    return "";
}

std::string ColorManager::resetCode() {
    if (!isColorEnabled()) {
        return "";
    }
    return "\033[0m";
}

std::string ColorManager::getLogLevelColor(LogLevel level) {
    if (!isColorEnabled()) {
        return "";
    }
    
    switch (level) {
        case LogLevel::DEBUG:
            return colorCode(Color::BRIGHT_BLACK);
        case LogLevel::INFO:
            return colorCode(Color::WHITE);
        case LogLevel::WARNING:
            return colorCode(Color::YELLOW);
        case LogLevel::ERROR:
            return colorCode(Color::RED);
        case LogLevel::CRITICAL:
            return colorCode(Color::BRIGHT_RED) + styleCode(Style::BOLD);
        default:
            return colorCode(Color::WHITE);
    }
}

std::string ColorManager::getProgressColor(float percentage) {
    if (!isColorEnabled()) {
        return "";
    }
    
    if (percentage >= 1.0f) {
        return colorCode(Color::BRIGHT_GREEN);
    } else if (percentage >= 0.8f) {
        return colorCode(Color::GREEN);
    } else if (percentage >= 0.5f) {
        return colorCode(Color::YELLOW);
    } else {
        return colorCode(Color::CYAN);
    }
}

std::string ColorManager::getSuccessColor() {
    if (!isColorEnabled()) {
        return "";
    }
    return colorCode(Color::BRIGHT_GREEN);
}

std::string ColorManager::getErrorColor() {
    if (!isColorEnabled()) {
        return "";
    }
    return colorCode(Color::BRIGHT_RED);
}

bool ColorManager::detectColorSupport() {
#ifdef _WIN32
    // Windows console color support detection
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    DWORD mode = 0;
    if (!GetConsoleMode(hConsole, &mode)) {
        return false;
    }
    
    // Try to enable virtual terminal processing (Windows 10+ supports ANSI escape sequences)
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(hConsole, mode)) {
        return true;
    }
    
    // Check if running in ANSI-capable terminal (like Windows Terminal, ConEmu, etc.)
    const char* term = std::getenv("TERM");
    if (term && (std::string(term).find("xterm") != std::string::npos ||
                 std::string(term).find("color") != std::string::npos)) {
        return true;
    }
    
    return false;
#else
    // Unix/Linux system color support detection
    
    // Check if connected to terminal
    if (!isatty(STDOUT_FILENO)) {
        return false;
    }
    
    // Check TERM environment variable
    const char* term = std::getenv("TERM");
    if (!term) {
        return false;
    }
    
    std::string termStr(term);
    
    // Check common color-capable terminal types
    if (termStr.find("xterm") != std::string::npos ||
        termStr.find("color") != std::string::npos ||
        termStr.find("ansi") != std::string::npos ||
        termStr.find("linux") != std::string::npos ||
        termStr.find("screen") != std::string::npos ||
        termStr.find("tmux") != std::string::npos) {
        return true;
    }
    
    // Check COLORTERM environment variable
    const char* colorterm = std::getenv("COLORTERM");
    if (colorterm) {
        return true;
    }
    
    // Check NO_COLOR environment variable (used to disable color output)
    const char* noColor = std::getenv("NO_COLOR");
    if (noColor && strlen(noColor) > 0) {
        return false;
    }
    
    return false;
#endif
}

std::string ColorManager::generateAnsiCode(int code) {
    return "\033[" + std::to_string(code) + "m";
}

} // namespace logging