#pragma once

#include "log_level.h"
#include "log_message.h"
#include "log_sink.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <string>
#include <sstream>
#include <array>

namespace logging {

// Forward declarations
class LogMessagePool;
class StringBufferPool;
class OptimizedFormatter;
class PooledLogMessage;

/**
 * 主日志器类
 * 实现单例模式的线程安全日志系统
 * 支持多输出器、级别过滤、模块过滤和格式化日志记录
 */
class Logger {
public:
    /**
     * 获取Logger单例实例
     * @return Logger实例的引用
     */
    static Logger& getInstance();
    
    // ========== 基本日志记录接口 ==========
    
    /**
     * 记录日志消息
     * @param level 日志级别
     * @param module 模块名称
     * @param message 日志消息
     */
    void log(LogLevel level, const std::string& module, const std::string& message);
    
    /**
     * 记录日志消息（移动语义）
     * @param message 日志消息对象
     */
    void log(LogMessage&& message);
    
    // ========== 便利方法 ==========
    
    /**
     * 记录DEBUG级别日志
     * @param module 模块名称
     * @param message 日志消息
     */
    void debug(const std::string& module, const std::string& message);
    
    /**
     * 记录INFO级别日志
     * @param module 模块名称
     * @param message 日志消息
     */
    void info(const std::string& module, const std::string& message);
    
    /**
     * 记录WARNING级别日志
     * @param module 模块名称
     * @param message 日志消息
     */
    void warning(const std::string& module, const std::string& message);
    
    /**
     * 记录ERROR级别日志
     * @param module 模块名称
     * @param message 日志消息
     */
    void error(const std::string& module, const std::string& message);
    
    /**
     * 记录CRITICAL级别日志
     * @param module 模块名称
     * @param message 日志消息
     */
    void critical(const std::string& module, const std::string& message);
    
    // ========== 格式化日志记录接口 ==========
    
    /**
     * 格式化日志记录
     * @param level 日志级别
     * @param module 模块名称
     * @param format 格式字符串
     * @param args 格式参数
     */
    template<typename... Args>
    void logf(LogLevel level, const std::string& module, const std::string& format, Args&&... args);
    
    /**
     * 格式化DEBUG日志记录
     * @param module 模块名称
     * @param format 格式字符串
     * @param args 格式参数
     */
    template<typename... Args>
    void debugf(const std::string& module, const std::string& format, Args&&... args);
    
    /**
     * 格式化INFO日志记录
     * @param module 模块名称
     * @param format 格式字符串
     * @param args 格式参数
     */
    template<typename... Args>
    void infof(const std::string& module, const std::string& format, Args&&... args);
    
    /**
     * 格式化WARNING日志记录
     * @param module 模块名称
     * @param format 格式字符串
     * @param args 格式参数
     */
    template<typename... Args>
    void warningf(const std::string& module, const std::string& format, Args&&... args);
    
    /**
     * 格式化ERROR日志记录
     * @param module 模块名称
     * @param format 格式字符串
     * @param args 格式参数
     */
    template<typename... Args>
    void errorf(const std::string& module, const std::string& format, Args&&... args);
    
    /**
     * 格式化CRITICAL日志记录
     * @param module 模块名称
     * @param format 格式字符串
     * @param args 格式参数
     */
    template<typename... Args>
    void criticalf(const std::string& module, const std::string& format, Args&&... args);
    
    // ========== 配置管理 ==========
    
    /**
     * 设置全局日志级别
     * @param level 日志级别
     */
    void setLogLevel(LogLevel level);
    
    /**
     * 获取全局日志级别
     * @return 当前全局日志级别
     */
    LogLevel getLogLevel() const;
    
    /**
     * 设置详细模式
     * @param enabled 是否启用详细模式
     */
    void setVerboseMode(bool enabled);
    
    /**
     * 检查是否处于详细模式
     * @return 如果处于详细模式则返回true
     */
    bool isVerboseMode() const;
    
    // ========== 输出目标管理 ==========
    
    /**
     * 添加日志输出器
     * @param sink 输出器智能指针
     */
    void addSink(std::unique_ptr<LogSink> sink);
    
    /**
     * 移除日志输出器
     * @param sinkName 输出器名称
     */
    void removeSink(const std::string& sinkName);
    
    /**
     * 启用控制台输出器
     * @param enabled 是否启用
     */
    void enableConsoleSink(bool enabled = true);
    
    /**
     * 启用文件输出器
     * @param filePath 文件路径
     * @param enabled 是否启用
     */
    void enableFileSink(const std::string& filePath, bool enabled = true);
    
    /**
     * 获取所有输出器名称
     * @return 输出器名称列表
     */
    std::vector<std::string> getSinkNames() const;
    
    // ========== 过滤器管理 ==========
    
    /**
     * 添加模块过滤器
     * @param moduleName 模块名称
     * @param minLevel 最小日志级别
     */
    void addModuleFilter(const std::string& moduleName, LogLevel minLevel);
    
    /**
     * 移除模块过滤器
     * @param moduleName 模块名称
     */
    void removeModuleFilter(const std::string& moduleName);
    
    /**
     * 清除所有模块过滤器
     */
    void clearModuleFilters();
    
    /**
     * 获取模块过滤器
     * @return 模块过滤器映射
     */
    std::unordered_map<std::string, LogLevel> getModuleFilters() const;
    
    // ========== 生命周期管理 ==========
    
    /**
     * 刷新所有输出器
     */
    void flush();
    
    /**
     * 关闭日志系统
     */
    void shutdown();
    
    /**
     * 检查日志系统是否已关闭
     * @return 如果已关闭则返回true
     */
    bool isShutdown() const;
    
    // ========== 高性能日志记录接口 ==========
    
    /**
     * 使用内存池的高性能日志记录
     * @param level 日志级别
     * @param module 模块名称
     * @param message 日志消息
     */
    void logPooled(LogLevel level, const std::string& module, const std::string& message);
    
    /**
     * 使用内存池的高性能格式化日志记录
     * @param level 日志级别
     * @param module 模块名称
     * @param format 格式字符串
     * @param args 格式参数
     */
    template<typename... Args>
    void logPooledF(LogLevel level, const std::string& module, const std::string& format, Args&&... args);
    
    /**
     * 获取内存池统计信息
     * @return 内存池统计信息
     */
    struct MemoryPoolStats {
        struct MessagePoolStats {
            size_t poolSize;
            size_t totalAcquired;
            size_t totalReleased;
            size_t cacheHits;
            size_t cacheMisses;
            double hitRate;
        } messagePool;
        
        struct StringPoolStats {
            std::array<size_t, 4> poolSizes;
            std::array<size_t, 4> totalAcquired;
            std::array<size_t, 4> totalReleased;
            std::array<size_t, 4> cacheHits;
            std::array<size_t, 4> cacheMisses;
            std::array<double, 4> hitRates;
        } stringPool;
    };
    
    MemoryPoolStats getMemoryPoolStats() const;

private:
    /**
     * 私有构造函数（单例模式）
     */
    Logger();
    
    /**
     * 析构函数
     */
    ~Logger();
    
    // 禁用拷贝构造和拷贝赋值
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    // ========== 内部实现方法 ==========
    
    /**
     * 检查消息是否应该被记录
     * @param level 日志级别
     * @param module 模块名称
     * @return 如果应该记录则返回true
     */
    bool shouldLog(LogLevel level, const std::string& module) const;
    
    /**
     * 创建日志消息
     * @param level 日志级别
     * @param module 模块名称
     * @param message 消息内容
     * @return 日志消息对象
     */
    LogMessage createLogMessage(LogLevel level, const std::string& module, const std::string& message);
    
    /**
     * 分发日志消息到所有输出器
     * @param message 日志消息
     */
    void dispatchMessage(const LogMessage& message);
    
    /**
     * 格式化字符串（内部实现）
     * @param format 格式字符串
     * @param args 格式参数
     * @return 格式化后的字符串
     */
    template<typename... Args>
    std::string formatString(const std::string& format, Args&&... args);
    
    // ========== 成员变量 ==========
    
    mutable std::shared_mutex configMutex_;                     // 配置读写锁
    mutable std::mutex sinksMutex_;                             // 输出器互斥锁
    
    std::atomic<LogLevel> globalLogLevel_;                      // 全局日志级别
    std::atomic<bool> verboseMode_;                             // 详细模式标志
    std::atomic<bool> shutdown_;                                // 关闭标志
    std::atomic<bool> initialized_;                             // 初始化标志
    std::mutex initMutex_;                                      // 初始化互斥锁
    
    std::vector<std::unique_ptr<LogSink>> sinks_;               // 输出器列表
    std::unordered_map<std::string, LogLevel> moduleFilters_;   // 模块过滤器
    
    // 确保初始化
    void ensureInitialized();
    
    // 预定义的输出器名称
    static constexpr const char* CONSOLE_SINK_NAME = "console";
    static constexpr const char* FILE_SINK_NAME = "file";
};

// ========== 模板方法实现 ==========

template<typename... Args>
void Logger::logf(LogLevel level, const std::string& module, const std::string& format, Args&&... args) {
    if (!shouldLog(level, module)) {
        return;
    }
    
    std::string formattedMessage = formatString(format, std::forward<Args>(args)...);
    log(level, module, formattedMessage);
}

template<typename... Args>
void Logger::debugf(const std::string& module, const std::string& format, Args&&... args) {
    logf(LogLevel::DEBUG, module, format, std::forward<Args>(args)...);
}

template<typename... Args>
void Logger::infof(const std::string& module, const std::string& format, Args&&... args) {
    logf(LogLevel::INFO, module, format, std::forward<Args>(args)...);
}

template<typename... Args>
void Logger::warningf(const std::string& module, const std::string& format, Args&&... args) {
    logf(LogLevel::WARNING, module, format, std::forward<Args>(args)...);
}

template<typename... Args>
void Logger::errorf(const std::string& module, const std::string& format, Args&&... args) {
    logf(LogLevel::ERROR, module, format, std::forward<Args>(args)...);
}

template<typename... Args>
void Logger::criticalf(const std::string& module, const std::string& format, Args&&... args) {
    logf(LogLevel::CRITICAL, module, format, std::forward<Args>(args)...);
}

template<typename... Args>
std::string Logger::formatString(const std::string& format, Args&&... args) {
    // 使用 snprintf 风格的格式化
    // 首先计算所需的缓冲区大小
    int size = std::snprintf(nullptr, 0, format.c_str(), args...);
    if (size <= 0) {
        return format; // 如果格式化失败，返回原始格式字符串
    }
    
    // 分配缓冲区并格式化
    std::string result(size + 1, '\0');
    std::snprintf(&result[0], size + 1, format.c_str(), args...);
    result.resize(size); // 移除末尾的null字符
    
    return result;
}

template<typename... Args>
void Logger::logPooledF(LogLevel level, const std::string& module, const std::string& format, Args&&... args) {
    if (!shouldLog(level, module)) {
        return;
    }
    
    // 使用字符串缓冲区池进行格式化
    PooledStringBuffer buffer(StringBufferPool::selectBufferSize(format.length() + 256));
    
    // 格式化到缓冲区
    int size = std::snprintf(nullptr, 0, format.c_str(), args...);
    if (size > 0) {
        buffer->resize(size);
        std::snprintf(&(*buffer)[0], size + 1, format.c_str(), args...);
    } else {
        *buffer = format;
    }
    
    logPooled(level, module, *buffer);
}

} // namespace logging