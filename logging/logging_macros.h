#pragma once

#include "common/logging/logger.h"
#include "common/logging/progress_tracker.h"
#include "common/logging/performance_monitor.h"
#include <string>
#include <sstream>

// ========== 编译时日志级别优化 ==========

// 编译时日志级别控制宏
// 可以通过 -DCOMPILE_TIME_LOG_LEVEL=X 来设置编译时最小日志级别
#ifndef COMPILE_TIME_LOG_LEVEL
#define COMPILE_TIME_LOG_LEVEL 0  // 默认允许所有级别
#endif

// 编译时日志级别常量
#define COMPILE_LOG_LEVEL_DEBUG 0
#define COMPILE_LOG_LEVEL_INFO 1
#define COMPILE_LOG_LEVEL_WARNING 2
#define COMPILE_LOG_LEVEL_ERROR 3
#define COMPILE_LOG_LEVEL_CRITICAL 4

// 编译时级别检查宏
#define LOG_LEVEL_ENABLED(level) ((level) >= COMPILE_TIME_LOG_LEVEL)

// 零开销日志宏 - 在编译时完全移除不需要的日志
#define LOG_COMPILE_TIME_CHECK(level, code) \
    do { \
        if constexpr (LOG_LEVEL_ENABLED(level)) { \
            code; \
        } \
    } while(0)

// 优化的格式化字符串处理
namespace MultiThreadedInstaller {
namespace detail {

// 编译时字符串长度计算
constexpr size_t strlen_constexpr(const char* str) {
    return *str ? 1 + strlen_constexpr(str + 1) : 0;
}

// 编译时格式字符串验证（简化版）
constexpr bool is_valid_format_string(const char* format) {
    // 简单的格式字符串验证，可以在编译时进行
    return format != nullptr && strlen_constexpr(format) > 0;
}

// 快速格式化缓冲区大小估算
constexpr size_t estimate_format_buffer_size(const char* format) {
    size_t base_size = strlen_constexpr(format);
    // 为参数预留额外空间（启发式估算）
    return base_size + 256;
}

} // namespace detail
} // namespace MultiThreadedInstaller

// 便利宏定义，用于简化日志记录
#define LOG_MODULE_NAME __FILE__

// ========== 优化的基础日志记录宏 ==========

// 零开销DEBUG日志宏
#define LOG_DEBUG(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_DEBUG, \
        logging::Logger::getInstance().debug(module, message))

#define LOG_INFO(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_INFO, \
        logging::Logger::getInstance().info(module, message))

#define LOG_WARNING(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_WARNING, \
        logging::Logger::getInstance().warning(module, message))

#define LOG_ERROR(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_ERROR, \
        logging::Logger::getInstance().error(module, message))

#define LOG_CRITICAL(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_CRITICAL, \
        logging::Logger::getInstance().critical(module, message))

// ========== 优化的格式化日志记录宏 ==========

// 编译时优化的格式化宏，包含格式字符串验证
#define LOG_DEBUGF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_DEBUG, \
        do { \
            static_assert(MultiThreadedInstaller::detail::is_valid_format_string(format), \
                         "Invalid format string"); \
            logging::Logger::getInstance().debugf(module, format, __VA_ARGS__); \
        } while(0))

#define LOG_INFOF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_INFO, \
        do { \
            static_assert(MultiThreadedInstaller::detail::is_valid_format_string(format), \
                         "Invalid format string"); \
            logging::Logger::getInstance().infof(module, format, __VA_ARGS__); \
        } while(0))

#define LOG_WARNINGF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_WARNING, \
        do { \
            static_assert(MultiThreadedInstaller::detail::is_valid_format_string(format), \
                         "Invalid format string"); \
            logging::Logger::getInstance().warningf(module, format, __VA_ARGS__); \
        } while(0))

#define LOG_ERRORF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_ERROR, \
        do { \
            static_assert(MultiThreadedInstaller::detail::is_valid_format_string(format), \
                         "Invalid format string"); \
            logging::Logger::getInstance().errorf(module, format, __VA_ARGS__); \
        } while(0))

#define LOG_CRITICALF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_CRITICAL, \
        do { \
            static_assert(MultiThreadedInstaller::detail::is_valid_format_string(format), \
                         "Invalid format string"); \
            logging::Logger::getInstance().criticalf(module, format, __VA_ARGS__); \
        } while(0))

// ========== 优化的模块特定日志宏 ==========

// 模块特定的日志宏（自动使用当前文件名作为模块名）
#define MLOG_DEBUG(message) LOG_DEBUG(LOG_MODULE_NAME, message)
#define MLOG_INFO(message) LOG_INFO(LOG_MODULE_NAME, message)
#define MLOG_WARNING(message) LOG_WARNING(LOG_MODULE_NAME, message)
#define MLOG_ERROR(message) LOG_ERROR(LOG_MODULE_NAME, message)
#define MLOG_CRITICAL(message) LOG_CRITICAL(LOG_MODULE_NAME, message)

#define MLOG_DEBUGF(format, ...) LOG_DEBUGF(LOG_MODULE_NAME, format, __VA_ARGS__)
#define MLOG_INFOF(format, ...) LOG_INFOF(LOG_MODULE_NAME, format, __VA_ARGS__)
#define MLOG_WARNINGF(format, ...) LOG_WARNINGF(LOG_MODULE_NAME, format, __VA_ARGS__)
#define MLOG_ERRORF(format, ...) LOG_ERRORF(LOG_MODULE_NAME, format, __VA_ARGS__)
#define MLOG_CRITICALF(format, ...) LOG_CRITICALF(LOG_MODULE_NAME, format, __VA_ARGS__)

// ========== 高性能日志宏 ==========

// 快速日志宏，跳过运行时级别检查（仅依赖编译时检查）
#define FAST_LOG_DEBUG(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_DEBUG, \
        logging::Logger::getInstance().log( \
            logging::LogLevel::DEBUG, module, message))

#define FAST_LOG_INFO(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_INFO, \
        logging::Logger::getInstance().log( \
            logging::LogLevel::INFO, module, message))

#define FAST_LOG_WARNING(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_WARNING, \
        logging::Logger::getInstance().log( \
            logging::LogLevel::WARNING, module, message))

#define FAST_LOG_ERROR(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_ERROR, \
        logging::Logger::getInstance().log( \
            logging::LogLevel::ERROR, module, message))

#define FAST_LOG_CRITICAL(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_CRITICAL, \
        logging::Logger::getInstance().log( \
            logging::LogLevel::CRITICAL, module, message))

// 进度追踪便利宏
#define START_PROGRESS(name, total) \
    logging::ProgressTracker::getInstance().startProgress(name, total)

#define UPDATE_PROGRESS(id, completed, current) \
    logging::ProgressTracker::getInstance().updateProgress(id, completed, current)

#define COMPLETE_PROGRESS(id) \
    logging::ProgressTracker::getInstance().completeProgress(id)

// 性能监控便利宏
#define START_TIMER(name) \
    logging::PerformanceMonitor::getInstance().startTimer(name)

#define RECORD_OPERATION(name, duration) \
    logging::PerformanceMonitor::getInstance().recordOperation(name, duration)

// RAII性能计时器宏
#define PERF_TIMER(name) \
    auto _perf_timer_##__LINE__ = logging::PerformanceMonitor::getInstance().startTimer(name)

// 条件日志宏（仅在条件为真时记录）
#define LOG_DEBUG_IF(condition, module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_DEBUG, \
        do { if (condition) LOG_DEBUG(module, message); } while(0))

#define LOG_INFO_IF(condition, module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_INFO, \
        do { if (condition) LOG_INFO(module, message); } while(0))

#define LOG_WARNING_IF(condition, module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_WARNING, \
        do { if (condition) LOG_WARNING(module, message); } while(0))

#define LOG_ERROR_IF(condition, module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_ERROR, \
        do { if (condition) LOG_ERROR(module, message); } while(0))

// 流式日志宏（支持 << 操作符）
#define LOG_STREAM(level, module) \
    MultiThreadedInstaller::LogStream(logging::LogLevel::level, module)

// 编译时优化的流式日志宏
#define LOG_DEBUG_STREAM(module) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_DEBUG, LOG_STREAM(DEBUG, module))

#define LOG_INFO_STREAM(module) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_INFO, LOG_STREAM(INFO, module))

#define LOG_WARNING_STREAM(module) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_WARNING, LOG_STREAM(WARNING, module))

#define LOG_ERROR_STREAM(module) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_ERROR, LOG_STREAM(ERROR, module))

#define LOG_CRITICAL_STREAM(module) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_CRITICAL, LOG_STREAM(CRITICAL, module))

namespace MultiThreadedInstaller {

// Bring logging namespace into MultiThreadedInstaller for compatibility
using namespace logging;

// 优化的流式日志辅助类
class LogStream {
public:
    LogStream(logging::LogLevel level, const std::string& module) 
        : level_(level), module_(module) {
        // 预分配缓冲区以减少重新分配
        stream_.str().reserve(256);
    }
    
    ~LogStream() {
        if constexpr (COMPILE_TIME_LOG_LEVEL <= static_cast<int>(logging::LogLevel::DEBUG)) {
            logging::Logger::getInstance().log(level_, module_, stream_.str());
        }
    }
    
    template<typename T>
    LogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

private:
    logging::LogLevel level_;
    std::string module_;
    std::ostringstream stream_;
};

// 优化的格式化字符串处理类
class OptimizedFormatter {
public:
    // 使用预分配缓冲区的格式化方法
    template<typename... Args>
    static std::string format(const char* format, Args&&... args) {
        // 使用标准的snprintf进行格式化
        int size = std::snprintf(nullptr, 0, format, std::forward<Args>(args)...);
        if (size <= 0) {
            return std::string(format);
        }
        
        std::string result(size + 1, '\0');
        std::snprintf(&result[0], size + 1, format, std::forward<Args>(args)...);
        result.resize(size);
        
        return result;
    }
};

} // namespace MultiThreadedInstaller

// 流式日志便利宏（已在上面定义，这里移除重复定义）

// ========== 编译时优化的便利宏 ==========

// 零开销调试宏 - 在Release构建中完全移除
#ifdef NDEBUG
#define DEBUG_ONLY(code) // 在Release模式下完全移除
#else
#define DEBUG_ONLY(code) code
#endif

// 性能关键路径的日志宏（最小开销）
#define PERF_LOG_DEBUG(module, message) \
    DEBUG_ONLY(FAST_LOG_DEBUG(module, message))

#define PERF_LOG_INFO(module, message) \
    FAST_LOG_INFO(module, message)

// 编译时字符串连接优化
#define LOG_WITH_CONTEXT(level, module, context, message) \
    LOG_##level(module, "[" context "] " message)

// 条件编译的详细日志
#define VERBOSE_LOG_DEBUG(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_DEBUG, \
        do { \
            if (MultiThreadedInstaller::Logger::getInstance().isVerboseMode()) { \
                LOG_DEBUG(module, message); \
            } \
        } while(0))

// 编译时优化的格式化宏（使用优化的格式化器）
#define OPTIMIZED_LOGF(level, module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_##level, \
        do { \
            static_assert(MultiThreadedInstaller::detail::is_valid_format_string(format), \
                         "Invalid format string"); \
            auto formatted = MultiThreadedInstaller::OptimizedFormatter::format(format, __VA_ARGS__); \
            MultiThreadedInstaller::Logger::getInstance().log( \
                MultiThreadedInstaller::LogLevel::level, module, formatted); \
        } while(0))

#define OPTIMIZED_DEBUGF(module, format, ...) OPTIMIZED_LOGF(DEBUG, module, format, __VA_ARGS__)
#define OPTIMIZED_INFOF(module, format, ...) OPTIMIZED_LOGF(INFO, module, format, __VA_ARGS__)
#define OPTIMIZED_WARNINGF(module, format, ...) OPTIMIZED_LOGF(WARNING, module, format, __VA_ARGS__)
#define OPTIMIZED_ERRORF(module, format, ...) OPTIMIZED_LOGF(ERROR, module, format, __VA_ARGS__)
#define OPTIMIZED_CRITICALF(module, format, ...) OPTIMIZED_LOGF(CRITICAL, module, format, __VA_ARGS__)

// ========== 内存池优化的日志宏 ==========

// 使用内存池的高性能日志宏
#define POOLED_LOG_DEBUG(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_DEBUG, \
        logging::Logger::getInstance().logPooled( \
            logging::LogLevel::DEBUG, module, message))

#define POOLED_LOG_INFO(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_INFO, \
        logging::Logger::getInstance().logPooled( \
            logging::LogLevel::INFO, module, message))

#define POOLED_LOG_WARNING(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_WARNING, \
        logging::Logger::getInstance().logPooled( \
            logging::LogLevel::WARNING, module, message))

#define POOLED_LOG_ERROR(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_ERROR, \
        logging::Logger::getInstance().logPooled( \
            logging::LogLevel::ERROR, module, message))

#define POOLED_LOG_CRITICAL(module, message) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_CRITICAL, \
        logging::Logger::getInstance().logPooled( \
            logging::LogLevel::CRITICAL, module, message))

// 使用内存池的格式化日志宏
#define POOLED_LOG_DEBUGF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_DEBUG, \
        logging::Logger::getInstance().logPooledF( \
            logging::LogLevel::DEBUG, module, format, __VA_ARGS__))

#define POOLED_LOG_INFOF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_INFO, \
        logging::Logger::getInstance().logPooledF( \
            logging::LogLevel::INFO, module, format, __VA_ARGS__))

#define POOLED_LOG_WARNINGF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_WARNING, \
        logging::Logger::getInstance().logPooledF( \
            logging::LogLevel::WARNING, module, format, __VA_ARGS__))

#define POOLED_LOG_ERRORF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_ERROR, \
        logging::Logger::getInstance().logPooledF( \
            logging::LogLevel::ERROR, module, format, __VA_ARGS__))

#define POOLED_LOG_CRITICALF(module, format, ...) \
    LOG_COMPILE_TIME_CHECK(COMPILE_LOG_LEVEL_CRITICAL, \
        logging::Logger::getInstance().logPooledF( \
            logging::LogLevel::CRITICAL, module, format, __VA_ARGS__))

// 高性能模块日志宏（使用内存池）
#define POOLED_MLOG_DEBUG(message) POOLED_LOG_DEBUG(LOG_MODULE_NAME, message)
#define POOLED_MLOG_INFO(message) POOLED_LOG_INFO(LOG_MODULE_NAME, message)
#define POOLED_MLOG_WARNING(message) POOLED_LOG_WARNING(LOG_MODULE_NAME, message)
#define POOLED_MLOG_ERROR(message) POOLED_LOG_ERROR(LOG_MODULE_NAME, message)
#define POOLED_MLOG_CRITICAL(message) POOLED_LOG_CRITICAL(LOG_MODULE_NAME, message)

#define POOLED_MLOG_DEBUGF(format, ...) POOLED_LOG_DEBUGF(LOG_MODULE_NAME, format, __VA_ARGS__)
#define POOLED_MLOG_INFOF(format, ...) POOLED_LOG_INFOF(LOG_MODULE_NAME, format, __VA_ARGS__)
#define POOLED_MLOG_WARNINGF(format, ...) POOLED_LOG_WARNINGF(LOG_MODULE_NAME, format, __VA_ARGS__)
#define POOLED_MLOG_ERRORF(format, ...) POOLED_LOG_ERRORF(LOG_MODULE_NAME, format, __VA_ARGS__)
#define POOLED_MLOG_CRITICALF(format, ...) POOLED_LOG_CRITICALF(LOG_MODULE_NAME, format, __VA_ARGS__)