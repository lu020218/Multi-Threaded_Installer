#include "common/logging/logger.h"
#include "common/logging/console_sink.h"
#include "common/logging/file_sink.h"
#include "common/logging/memory_pool.h"
#include <algorithm>
#include <iostream>

namespace logging {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() 
    : globalLogLevel_(LogLevel::INFO)
    , verboseMode_(false)
    , shutdown_(false)
    , initialized_(false) {
    // 不在构造函数中初始化任何东西，延迟到第一次使用
}

void Logger::ensureInitialized() {
    if (!initialized_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(initMutex_);
        if (!initialized_.load(std::memory_order_relaxed)) {
            // 默认添加控制台输出器
            enableConsoleSink(true);
            initialized_.store(true, std::memory_order_release);
        }
    }
}

Logger::~Logger() {
    shutdown();
}

// ========== 基本日志记录接口 ==========

void Logger::log(LogLevel level, const std::string& module, const std::string& message) {
    ensureInitialized();
    
    if (shutdown_.load() || !shouldLog(level, module)) {
        return;
    }
    
    LogMessage logMessage = createLogMessage(level, module, message);
    dispatchMessage(logMessage);
}

void Logger::log(LogMessage&& message) {
    ensureInitialized();
    
    if (shutdown_.load() || !shouldLog(message.level, message.moduleName)) {
        return;
    }
    
    dispatchMessage(message);
}

// ========== 便利方法 ==========

void Logger::debug(const std::string& module, const std::string& message) {
    log(LogLevel::DEBUG, module, message);
}

void Logger::info(const std::string& module, const std::string& message) {
    log(LogLevel::INFO, module, message);
}

void Logger::warning(const std::string& module, const std::string& message) {
    log(LogLevel::WARNING, module, message);
}

void Logger::error(const std::string& module, const std::string& message) {
    log(LogLevel::ERROR, module, message);
}

void Logger::critical(const std::string& module, const std::string& message) {
    log(LogLevel::CRITICAL, module, message);
}

// ========== 配置管理 ==========

void Logger::setLogLevel(LogLevel level) {
    globalLogLevel_.store(level);
}

LogLevel Logger::getLogLevel() const {
    return globalLogLevel_.load();
}

void Logger::setVerboseMode(bool enabled) {
    verboseMode_.store(enabled);
}

bool Logger::isVerboseMode() const {
    return verboseMode_.load();
}

// ========== 输出目标管理 ==========

void Logger::addSink(std::unique_ptr<LogSink> sink) {
    if (!sink || shutdown_.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(sinksMutex_);
    
    // 检查是否已存在同名的输出器
    auto it = std::find_if(sinks_.begin(), sinks_.end(),
        [&sink](const std::unique_ptr<LogSink>& existingSink) {
            return existingSink->getName() == sink->getName();
        });
    
    if (it != sinks_.end()) {
        // 替换现有的输出器
        *it = std::move(sink);
    } else {
        // 添加新的输出器
        sinks_.push_back(std::move(sink));
    }
}

void Logger::removeSink(const std::string& sinkName) {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    
    sinks_.erase(
        std::remove_if(sinks_.begin(), sinks_.end(),
            [&sinkName](const std::unique_ptr<LogSink>& sink) {
                return sink->getName() == sinkName;
            }),
        sinks_.end());
}

void Logger::enableConsoleSink(bool enabled) {
    if (enabled) {
        auto consoleSink = std::make_unique<ConsoleSink>(CONSOLE_SINK_NAME, true);
        addSink(std::move(consoleSink));
    } else {
        removeSink(CONSOLE_SINK_NAME);
    }
}

void Logger::enableFileSink(const std::string& filePath, bool enabled) {
    if (enabled) {
        FileSink::Config config;
        config.filePath = filePath;
        config.maxFileSize = 10 * 1024 * 1024; // 10MB
        config.maxFiles = 5;
        config.autoFlush = true;
        
        auto fileSink = std::make_unique<FileSink>(FILE_SINK_NAME, config);
        addSink(std::move(fileSink));
    } else {
        removeSink(FILE_SINK_NAME);
    }
}

std::vector<std::string> Logger::getSinkNames() const {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    
    std::vector<std::string> names;
    names.reserve(sinks_.size());
    
    for (const auto& sink : sinks_) {
        names.push_back(sink->getName());
    }
    
    return names;
}

// ========== 过滤器管理 ==========

void Logger::addModuleFilter(const std::string& moduleName, LogLevel minLevel) {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    moduleFilters_[moduleName] = minLevel;
}

void Logger::removeModuleFilter(const std::string& moduleName) {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    moduleFilters_.erase(moduleName);
}

void Logger::clearModuleFilters() {
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    moduleFilters_.clear();
}

std::unordered_map<std::string, LogLevel> Logger::getModuleFilters() const {
    std::shared_lock<std::shared_mutex> lock(configMutex_);
    return moduleFilters_;
}

// ========== 生命周期管理 ==========

void Logger::flush() {
    if (shutdown_.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(sinksMutex_);
    
    for (auto& sink : sinks_) {
        try {
            sink->flush();
        } catch (const std::exception& e) {
            // 如果刷新失败，输出到stderr（避免递归调用）
            std::cerr << "Logger: Failed to flush sink '" << sink->getName() 
                      << "': " << e.what() << std::endl;
        }
    }
}

void Logger::shutdown() {
    if (shutdown_.exchange(true)) {
        return; // 已经关闭
    }
    
    // 刷新所有输出器
    flush();
    
    // 清空输出器列表
    std::lock_guard<std::mutex> lock(sinksMutex_);
    sinks_.clear();
}

bool Logger::isShutdown() const {
    return shutdown_.load();
}

// ========== 内部实现方法 ==========

bool Logger::shouldLog(LogLevel level, const std::string& module) const {
    // 如果处于详细模式，记录所有级别的消息
    if (verboseMode_.load()) {
        return true;
    }
    
    // 检查全局日志级别
    if (level < globalLogLevel_.load()) {
        return false;
    }
    
    // 检查模块过滤器
    std::shared_lock<std::shared_mutex> lock(configMutex_);
    auto it = moduleFilters_.find(module);
    if (it != moduleFilters_.end()) {
        return level >= it->second;
    }
    
    return true;
}

LogMessage Logger::createLogMessage(LogLevel level, const std::string& module, const std::string& message) {
    LogMessage logMessage;
    logMessage.level = level;
    logMessage.timestamp = std::chrono::system_clock::now();
    logMessage.threadId = std::this_thread::get_id();
    logMessage.moduleName = module;
    logMessage.message = message;
    
    // 可以在这里添加文件名、行号、函数名等信息
    // 但需要使用宏来获取这些信息
    
    return logMessage;
}

void Logger::dispatchMessage(const LogMessage& message) {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    
    for (auto& sink : sinks_) {
        try {
            // 检查输出器是否应该记录这个级别的消息
            if (sink->shouldLog(message.level)) {
                sink->write(message);
            }
        } catch (const std::exception& e) {
            // 如果写入失败，输出到stderr（避免递归调用）
            std::cerr << "Logger: Failed to write to sink '" << sink->getName() 
                      << "': " << e.what() << std::endl;
        }
    }
}

// ========== 高性能日志记录接口 ==========

void Logger::logPooled(LogLevel level, const std::string& module, const std::string& message) {
    if (shutdown_.load() || !shouldLog(level, module)) {
        return;
    }
    
    // 使用内存池获取LogMessage对象
    PooledLogMessage pooledMessage;
    
    // 设置消息内容
    pooledMessage->level = level;
    pooledMessage->timestamp = std::chrono::system_clock::now();
    pooledMessage->threadId = std::this_thread::get_id();
    pooledMessage->moduleName = module;
    pooledMessage->message = message;
    
    // 分发消息
    dispatchMessage(*pooledMessage);
    
    // PooledLogMessage析构时会自动归还到池中
}

Logger::MemoryPoolStats Logger::getMemoryPoolStats() const {
    MemoryPoolStats stats;
    
    // Get message pool stats
    auto msgPoolStats = LogMessagePool::getInstance().getStats();
    stats.messagePool.poolSize = msgPoolStats.poolSize;
    stats.messagePool.totalAcquired = msgPoolStats.totalAcquired;
    stats.messagePool.totalReleased = msgPoolStats.totalReleased;
    stats.messagePool.cacheHits = msgPoolStats.cacheHits;
    stats.messagePool.cacheMisses = msgPoolStats.cacheMisses;
    stats.messagePool.hitRate = msgPoolStats.hitRate;
    
    // Get string pool stats
    auto strPoolStats = StringBufferPool::getInstance().getStats();
    stats.stringPool.poolSizes = strPoolStats.poolSizes;
    stats.stringPool.totalAcquired = strPoolStats.totalAcquired;
    stats.stringPool.totalReleased = strPoolStats.totalReleased;
    stats.stringPool.cacheHits = strPoolStats.cacheHits;
    stats.stringPool.cacheMisses = strPoolStats.cacheMisses;
    stats.stringPool.hitRates = strPoolStats.hitRates;
    
    return stats;
}

} // namespace logging