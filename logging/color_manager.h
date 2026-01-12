#pragma once

#include <string>
#include "log_level.h"

namespace logging {

/**
 * Color Manager Class
 * Manages console color output, including ANSI escape sequence generation, 
 * color support detection, and predefined color schemes
 */
class ColorManager {
public:
    /**
     * Color enumeration
     * Defines the standard 16-color palette
     */
    enum class Color {
        BLACK = 0, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE,
        BRIGHT_BLACK, BRIGHT_RED, BRIGHT_GREEN, BRIGHT_YELLOW,
        BRIGHT_BLUE, BRIGHT_MAGENTA, BRIGHT_CYAN, BRIGHT_WHITE
    };
    
    /**
     * Style enumeration
     * Defines text style options
     */
    enum class Style {
        RESET = 0, BOLD, DIM, ITALIC, UNDERLINE, BLINK, REVERSE, STRIKETHROUGH
    };
    
    /**
     * Get ColorManager singleton instance
     * @return Reference to ColorManager singleton
     */
    static ColorManager& getInstance();
    
    // Color support detection
    /**
     * Check if current terminal supports color output
     * @return true if color output is supported
     */
    bool isColorSupported();
    
    /**
     * Set whether to enable color output
     * @param enabled Whether to enable color output
     */
    void setColorEnabled(bool enabled);
    
    /**
     * Check if color output is enabled
     * @return true if color output is enabled
     */
    bool isColorEnabled() const;
    
    // ANSI escape sequence generation
    /**
     * Generate ANSI escape sequence for foreground and background colors
     * @param foreground Foreground color
     * @param background Background color (default is black)
     * @return ANSI escape sequence string
     */
    std::string colorCode(Color foreground, Color background = Color::BLACK);
    
    /**
     * Generate ANSI escape sequence for text style
     * @param style Text style
     * @return ANSI escape sequence string
     */
    std::string styleCode(Style style);
    
    /**
     * Generate ANSI escape sequence to reset all formatting
     * @return Reset sequence string
     */
    std::string resetCode();
    
    // Predefined color schemes
    /**
     * Get color code corresponding to log level
     * @param level Log level
     * @return Corresponding color ANSI sequence
     */
    std::string getLogLevelColor(LogLevel level);
    
    /**
     * Get progress bar color based on percentage
     * @param percentage Progress percentage (0.0-1.0)
     * @return Corresponding color ANSI sequence
     */
    std::string getProgressColor(float percentage);
    
    /**
     * Get color code for success messages
     * @return Success color ANSI sequence
     */
    std::string getSuccessColor();
    
    /**
     * Get color code for error messages
     * @return Error color ANSI sequence
     */
    std::string getErrorColor();
    
private:
    ColorManager();
    ~ColorManager() = default;
    
    // Disable copy constructor and assignment
    ColorManager(const ColorManager&) = delete;
    ColorManager& operator=(const ColorManager&) = delete;
    
    bool colorEnabled_;      // User-set color enable status
    bool colorSupported_;    // Terminal color support detection result
    
    /**
     * Detect terminal color support
     * @return true if terminal supports color
     */
    bool detectColorSupport();
    
    /**
     * Generate ANSI escape sequence
     * @param code ANSI color code
     * @return Complete ANSI escape sequence
     */
    std::string generateAnsiCode(int code);
};

} // namespace logging