#pragma once

#include "log_message.h"
#include "log_level.h"
#include <string>
#include <memory>

namespace logging {

/**
 * 日志输出基类
 * 定义了所有日志输出器的通用接口
 * 支持过滤器和线程安全标识
 */
class LogSink {
public:
    /**
     * 构造函数
     * @param name 输出器名称
     */
    explicit LogSink(std::string name) : name_(std::move(name)) {}
    
    /**
     * 虚析构函数
     */
    virtual ~LogSink() = default;
    
    /**
     * 写入日志消息（纯虚函数）
     * @param message 要写入的日志消息
     */
    virtual void write(const LogMessage& message) = 0;
    
    /**
     * 刷新输出缓冲区（纯虚函数）
     */
    virtual void flush() = 0;
    
    /**
     * 检查输出器是否线程安全
     * @return 如果线程安全则返回true，默认为false
     */
    virtual bool isThreadSafe() const { return false; }
    
    /**
     * 获取输出器名称
     * @return 输出器名称
     */
    const std::string& getName() const { return name_; }
    
    /**
     * 设置最小日志级别
     * @param level 最小日志级别
     */
    void setMinLevel(LogLevel level) { minLevel_ = level; }
    
    /**
     * 获取最小日志级别
     * @return 最小日志级别
     */
    LogLevel getMinLevel() const { return minLevel_; }
    
    /**
     * 检查是否应该记录指定级别的日志
     * @param level 日志级别
     * @return 如果应该记录则返回true
     */
    bool shouldLog(LogLevel level) const { return level >= minLevel_; }

protected:
    std::string name_;                    // 输出器名称
    LogLevel minLevel_ = LogLevel::DEBUG; // 最小日志级别
};

} // namespace logging