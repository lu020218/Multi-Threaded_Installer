#pragma once

#include "log_message.h"
#include <string>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace logging {

/**
 * 消息格式器类
 * 负责将LogMessage格式化为可读的字符串输出
 * 支持控制台和文件两种不同的格式化方式
 */
class MessageFormatter {
public:
    /**
     * 格式化配置结构
     */
    struct FormatConfig {
        std::string timestampFormat = "%Y-%m-%d %H:%M:%S.%f";  // 时间戳格式
        bool includeThreadId = true;                           // 是否包含线程ID
        bool includeModuleName = true;                         // 是否包含模块名称
        bool includeSourceLocation = false;                    // 是否包含源码位置
        int fieldWidth = 80;                                   // 字段宽度
        char paddingChar = ' ';                                // 填充字符
    };
    
    /**
     * 构造函数
     * @param config 格式化配置，使用默认配置如果未提供
     */
    explicit MessageFormatter(const FormatConfig& config = {});
    
    /**
     * 格式化日志消息（通用格式）
     * @param message 要格式化的日志消息
     * @return 格式化后的字符串
     */
    std::string format(const LogMessage& message);
    
    /**
     * 为控制台格式化日志消息
     * @param message 要格式化的日志消息
     * @param useColor 是否使用彩色输出
     * @return 格式化后的字符串
     */
    std::string formatForConsole(const LogMessage& message, bool useColor = false);
    
    /**
     * 为文件格式化日志消息
     * @param message 要格式化的日志消息
     * @return 格式化后的字符串
     */
    std::string formatForFile(const LogMessage& message);
    
    /**
     * 设置格式化配置
     * @param config 新的格式化配置
     */
    void setConfig(const FormatConfig& config);
    
    /**
     * 获取当前格式化配置
     * @return 当前的格式化配置
     */
    const FormatConfig& getConfig() const { return config_; }
    
private:
    FormatConfig config_;  // 格式化配置
    
    /**
     * 格式化时间戳
     * @param timestamp 时间戳
     * @return 格式化后的时间戳字符串
     */
    std::string formatTimestamp(const std::chrono::system_clock::time_point& timestamp);
    
    /**
     * 格式化日志级别
     * @param level 日志级别
     * @param useColor 是否使用彩色输出
     * @return 格式化后的日志级别字符串
     */
    std::string formatLogLevel(LogLevel level, bool useColor = false);
    
    /**
     * 格式化线程ID
     * @param threadId 线程ID
     * @return 格式化后的线程ID字符串
     */
    std::string formatThreadId(std::thread::id threadId);
    
    /**
     * 填充字符串到指定宽度
     * @param str 要填充的字符串
     * @param width 目标宽度
     * @param padChar 填充字符
     * @return 填充后的字符串
     */
    std::string padString(const std::string& str, size_t width, char padChar = ' ');
    
    /**
     * 处理多行消息的对齐
     * @param message 原始消息
     * @param prefix 每行的前缀
     * @return 对齐后的多行消息
     */
    std::string alignMultilineMessage(const std::string& message, const std::string& prefix);
    
    /**
     * 获取日志级别的彩色代码
     * @param level 日志级别
     * @return ANSI彩色代码
     */
    std::string getLogLevelColorCode(LogLevel level);
    
    /**
     * 获取重置彩色代码
     * @return ANSI重置代码
     */
    std::string getResetColorCode();
};

} // namespace logging