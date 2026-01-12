#pragma once

#include "log_sink.h"
#include "message_formatter.h"
#include "color_manager.h"
#include <iostream>
#include <mutex>

namespace logging {

/**
 * 控制台输出器
 * 实现线程安全的控制台日志输出，支持彩色输出和流重定向
 */
class ConsoleSink : public LogSink {
public:
    /**
     * 默认构造函数
     * 使用默认设置创建控制台输出器
     */
    ConsoleSink();
    
    /**
     * 构造函数
     * @param enableColor 是否启用彩色输出
     */
    explicit ConsoleSink(bool enableColor);
    
    /**
     * 构造函数
     * @param name 输出器名称
     * @param enableColor 是否启用彩色输出
     */
    ConsoleSink(const std::string& name, bool enableColor);
    
    /**
     * 析构函数
     */
    ~ConsoleSink() override = default;
    
    /**
     * 写入日志消息
     * @param message 要写入的日志消息
     */
    void write(const LogMessage& message) override;
    
    /**
     * 刷新输出缓冲区
     */
    void flush() override;
    
    /**
     * 检查是否线程安全
     * @return 始终返回true，因为ConsoleSink是线程安全的
     */
    bool isThreadSafe() const override { return true; }
    
    /**
     * 设置是否启用彩色输出
     * @param enabled 是否启用彩色输出
     */
    void setColorEnabled(bool enabled);
    
    /**
     * 检查是否启用了彩色输出
     * @return 如果启用了彩色输出则返回true
     */
    bool isColorEnabled() const;
    
    /**
     * 设置输出流（用于普通消息）
     * @param stream 输出流指针
     */
    void setOutputStream(std::ostream* stream);
    
    /**
     * 设置错误流（用于错误和警告消息）
     * @param stream 错误流指针
     */
    void setErrorStream(std::ostream* stream);
    
    /**
     * 获取当前输出流
     * @return 输出流指针
     */
    std::ostream* getOutputStream() const { return outputStream_; }
    
    /**
     * 获取当前错误流
     * @return 错误流指针
     */
    std::ostream* getErrorStream() const { return errorStream_; }

private:
    bool colorEnabled_;                    // 彩色输出启用状态
    std::ostream* outputStream_;           // 普通消息输出流
    std::ostream* errorStream_;            // 错误消息输出流
    mutable std::mutex writeMutex_;        // 写入互斥锁
    MessageFormatter formatter_;           // 消息格式器
    ColorManager& colorManager_;           // 彩色管理器引用
    
    /**
     * 检测彩色输出支持
     * @return 如果支持彩色输出则返回true
     */
    bool detectColorSupport();
    
    /**
     * 根据日志级别选择输出流
     * @param level 日志级别
     * @return 对应的输出流指针
     */
    std::ostream* selectOutputStream(LogLevel level);
    
    /**
     * 获取日志级别对应的彩色代码
     * @param level 日志级别
     * @return ANSI彩色代码字符串
     */
    std::string getColorCode(LogLevel level);
    
    /**
     * 获取重置彩色代码
     * @return ANSI重置代码字符串
     */
    std::string getResetCode();
};

} // namespace logging