#include "common/logging/async_worker.h"
#include <algorithm>
#include <cassert>
#include <future>

namespace logging {

// ========== Statistics 方法实现 ==========

double AsyncWorker::Statistics::getMessageRate() const {
    auto uptime = getUptime();
    if (uptime.count() == 0) {
        return 0.0;
    }
    
    return static_cast<double>(messagesProcessed.load()) * 1000.0 / uptime.count();
}

double AsyncWorker::Statistics::getDropRate() const {
    uint64_t processed = messagesProcessed.load();
    uint64_t dropped = messagesDropped.load();
    uint64_t total = processed + dropped;
    
    if (total == 0) {
        return 0.0;
    }
    
    return static_cast<double>(dropped) / total;
}

std::chrono::milliseconds AsyncWorker::Statistics::getUptime() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime
    );
}

// ========== AsyncWorker 方法实现 ==========

AsyncWorker::AsyncWorker(const Config& config) 
    : config_(config)
    , buffer_(std::make_unique<LockFreeRingBuffer<LogMessage, 8192>>())
    , batchBuffer_()
{
    // 预分配批处理缓冲区
    batchBuffer_.reserve(config_.batchSize);
    
    // 初始化统计信息
    statistics_.startTime = std::chrono::steady_clock::now();
}

AsyncWorker::~AsyncWorker() {
    stop(true);
}

bool AsyncWorker::start() {
    if (running_.load()) {
        return false; // 已经在运行
    }
    
    shouldStop_.store(false);
    
    try {
        workerThread_ = std::thread(&AsyncWorker::workerThreadFunc, this);
        running_.store(true);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void AsyncWorker::stop(bool waitForCompletion) {
    if (!running_.load()) {
        return; // 已经停止
    }
    
    shouldStop_.store(true);
    
    if (waitForCompletion) {
        // 通知工作线程刷新
        {
            std::lock_guard<std::mutex> lock(flushMutex_);
            flushRequested_.store(true);
        }
        flushCondition_.notify_one();
        
        waitForWorkerThread();
    } else {
        // 强制停止，不等待完成
        if (workerThread_.joinable()) {
            workerThread_.detach();
        }
        running_.store(false);
    }
}

bool AsyncWorker::isRunning() const {
    return running_.load();
}

bool AsyncWorker::logAsync(LogMessage&& message) {
    if (!running_.load()) {
        return false;
    }
    
    // 对于多生产者场景，我们需要使用互斥锁保护入队操作
    std::lock_guard<std::mutex> lock(enqueueMutex_);
    
    // 尝试将消息入队
    if (buffer_->tryEnqueue(std::move(message))) {
        return true;
    }
    
    // 缓冲区满了
    if (config_.dropOnOverflow) {
        statistics_.messagesDropped.fetch_add(1);
        return false;
    } else {
        // 阻塞模式：等待空间可用（简单的自旋等待）
        // 注意：在生产环境中可能需要更复杂的等待策略
        while (!buffer_->tryEnqueue(std::move(message))) {
            if (shouldStop_.load()) {
                statistics_.messagesDropped.fetch_add(1);
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }
}

void AsyncWorker::addSink(std::shared_ptr<LogSink> sink) {
    if (!sink) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(sinksMutex_);
    sinks_.push_back(std::move(sink));
}

void AsyncWorker::removeSink(const std::string& sinkName) {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    sinks_.erase(
        std::remove_if(sinks_.begin(), sinks_.end(),
            [&sinkName](const std::shared_ptr<LogSink>& sink) {
                return sink && sink->getName() == sinkName;
            }),
        sinks_.end()
    );
}

void AsyncWorker::clearSinks() {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    sinks_.clear();
}

std::vector<std::shared_ptr<LogSink>> AsyncWorker::getSinks() const {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    return sinks_;
}

void AsyncWorker::flush() {
    if (!running_.load()) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(flushMutex_);
        flushRequested_.store(true);
    }
    flushCondition_.notify_one();
}

AsyncWorker::Statistics AsyncWorker::getStatistics() const {
    return statistics_;
}

void AsyncWorker::resetStatistics() {
    statistics_.messagesProcessed.store(0);
    statistics_.messagesDropped.store(0);
    statistics_.batchesProcessed.store(0);
    statistics_.flushOperations.store(0);
    statistics_.startTime = std::chrono::steady_clock::now();
}

bool AsyncWorker::updateConfig(const Config& config) {
    if (running_.load()) {
        // 只允许更新某些配置项
        config_.flushInterval = config.flushInterval;
        config_.batchSize = config.batchSize;
        config_.dropOnOverflow = config.dropOnOverflow;
        return true;
    } else {
        // 停止状态下可以更新所有配置
        config_ = config;
        batchBuffer_.reserve(config_.batchSize);
        return true;
    }
}

AsyncWorker::Config AsyncWorker::getConfig() const {
    return config_;
}

// ========== 私有方法实现 ==========

void AsyncWorker::workerThreadFunc() {
    auto lastFlush = std::chrono::steady_clock::now();
    
    while (!shouldStop_.load()) {
        // 处理一批消息
        size_t processedCount = processBatch();
        
        if (processedCount > 0) {
            statistics_.batchesProcessed.fetch_add(1);
            lastFlush = std::chrono::steady_clock::now();
        }
        
        // 检查是否需要刷新
        auto now = std::chrono::steady_clock::now();
        bool shouldFlush = false;
        
        // 检查定时刷新
        if (now - lastFlush >= config_.flushInterval) {
            shouldFlush = true;
        }
        
        // 检查手动刷新请求
        if (flushRequested_.load()) {
            shouldFlush = true;
            flushRequested_.store(false);
        }
        
        if (shouldFlush) {
            flushSinks();
            statistics_.flushOperations.fetch_add(1);
            lastFlush = now;
        }
        
        // 如果没有处理任何消息，短暂等待
        if (processedCount == 0) {
            std::unique_lock<std::mutex> lock(flushMutex_);
            flushCondition_.wait_for(lock, std::chrono::milliseconds(10));
        }
    }
    
    // 关闭前处理剩余消息
    while (processBatch() > 0) {
        // 继续处理直到缓冲区为空
    }
    
    // 最终刷新
    flushSinks();
    
    running_.store(false);
}

size_t AsyncWorker::processBatch() {
    batchBuffer_.clear();
    
    // 从缓冲区中取出一批消息
    LogMessage message;
    size_t count = 0;
    
    while (count < config_.batchSize && buffer_->tryDequeue(message)) {
        batchBuffer_.emplace_back(std::move(message));
        ++count;
    }
    
    // 分发消息到所有输出器
    for (const auto& msg : batchBuffer_) {
        dispatchMessage(msg);
    }
    
    statistics_.messagesProcessed.fetch_add(count);
    return count;
}

void AsyncWorker::dispatchMessage(const LogMessage& message) {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    
    for (auto& sink : sinks_) {
        if (sink && sink->shouldLog(message.level)) {
            try {
                sink->write(message);
            } catch (const std::exception&) {
                // 忽略输出器错误，继续处理其他输出器
                // 在实际应用中可能需要记录这些错误
            }
        }
    }
}

void AsyncWorker::flushSinks() {
    std::lock_guard<std::mutex> lock(sinksMutex_);
    
    for (auto& sink : sinks_) {
        if (sink) {
            try {
                sink->flush();
            } catch (const std::exception&) {
                // 忽略刷新错误
            }
        }
    }
}

void AsyncWorker::waitForWorkerThread() {
    if (workerThread_.joinable()) {
        // 等待工作线程结束，但有超时限制
        auto future = std::async(std::launch::async, [this]() {
            workerThread_.join();
        });
        
        if (future.wait_for(config_.shutdownTimeout) == std::future_status::timeout) {
            // 超时，强制分离线程
            workerThread_.detach();
        }
    }
    
    running_.store(false);
}

} // namespace logging