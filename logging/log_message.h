#pragma once

#include "log_level.h"
#include <chrono>
#include <thread>
#include <string>
#include <optional>

namespace logging {

/**
 * 日志消息结构
 * 包含完整的日志信息，支持移动语义以提高性能
 */
struct LogMessage {
    LogLevel level;                                           // 日志级别
    std::chrono::system_clock::time_point timestamp;         // 时间戳
    std::thread::id threadId;                                // 线程ID
    std::string moduleName;                                  // 模块名称
    std::string fileName;                                    // 文件名
    int lineNumber;                                          // 行号
    std::string functionName;                                // 函数名
    std::string message;                                     // 日志消息内容
    
    // 性能相关字段
    std::optional<std::chrono::microseconds> duration;       // 操作耗时
    std::optional<size_t> memoryUsage;                      // 内存使用量
    
    /**
     * 默认构造函数
     */
    LogMessage() = default;
    
    /**
     * 基础构造函数
     * @param lvl 日志级别
     * @param module 模块名称
     * @param msg 日志消息
     */
    LogMessage(LogLevel lvl, std::string module, std::string msg);
    
    /**
     * 完整构造函数
     * @param lvl 日志级别
     * @param module 模块名称
     * @param msg 日志消息
     * @param file 文件名
     * @param line 行号
     * @param func 函数名
     */
    LogMessage(LogLevel lvl, std::string module, std::string msg, 
               std::string file, int line, std::string func);
    
    // 支持移动语义
    LogMessage(LogMessage&&) = default;
    LogMessage& operator=(LogMessage&&) = default;
    
    // 禁用拷贝构造和拷贝赋值以避免不必要的性能开销
    LogMessage(const LogMessage&) = delete;
    LogMessage& operator=(const LogMessage&) = delete;
    
    /**
     * 设置性能信息
     * @param dur 操作耗时
     * @param mem 内存使用量
     */
    void setPerformanceInfo(std::chrono::microseconds dur, size_t mem);
    
    /**
     * 检查是否包含性能信息
     * @return 如果包含性能信息则返回true
     */
    bool hasPerformanceInfo() const;
};

} // namespace logging