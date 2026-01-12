#include "common/logging/memory_pool.h"
#include <algorithm>
#include <cstring>

namespace logging {

// ========== LogMessagePool Implementation ==========

LogMessagePool& LogMessagePool::getInstance() {
    static LogMessagePool instance;
    return instance;
}

LogMessagePool::LogMessagePool() : maxPoolSize_(1000) {
    // 预分配一些LogMessage对象
    std::lock_guard<std::mutex> lock(poolMutex_);
    for (size_t i = 0; i < 50; ++i) {
        pool_.push(std::make_unique<LogMessage>());
    }
}

std::unique_ptr<LogMessage> LogMessagePool::acquire() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    totalAcquired_.fetch_add(1, std::memory_order_relaxed);
    
    if (!pool_.empty()) {
        auto message = std::move(pool_.top());
        pool_.pop();
        cacheHits_.fetch_add(1, std::memory_order_relaxed);
        
        // 重置消息到初始状态
        resetMessage(*message);
        return message;
    }
    
    // 池为空，创建新对象
    cacheMisses_.fetch_add(1, std::memory_order_relaxed);
    return std::make_unique<LogMessage>();
}

void LogMessagePool::release(std::unique_ptr<LogMessage> message) {
    if (!message) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(poolMutex_);
    totalReleased_.fetch_add(1, std::memory_order_relaxed);
    
    // 如果池未满，归还对象
    if (pool_.size() < maxPoolSize_) {
        pool_.push(std::move(message));
    }
    // 如果池已满，让对象自然销毁
}

LogMessagePool::PoolStats LogMessagePool::getStats() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    PoolStats stats;
    stats.poolSize = pool_.size();
    stats.totalAcquired = totalAcquired_.load(std::memory_order_relaxed);
    stats.totalReleased = totalReleased_.load(std::memory_order_relaxed);
    stats.cacheHits = cacheHits_.load(std::memory_order_relaxed);
    stats.cacheMisses = cacheMisses_.load(std::memory_order_relaxed);
    
    if (stats.totalAcquired > 0) {
        stats.hitRate = static_cast<double>(stats.cacheHits) / stats.totalAcquired;
    } else {
        stats.hitRate = 0.0;
    }
    
    return stats;
}

void LogMessagePool::clear() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    while (!pool_.empty()) {
        pool_.pop();
    }
    
    // 重置统计信息
    totalAcquired_.store(0, std::memory_order_relaxed);
    totalReleased_.store(0, std::memory_order_relaxed);
    cacheHits_.store(0, std::memory_order_relaxed);
    cacheMisses_.store(0, std::memory_order_relaxed);
}

void LogMessagePool::setMaxPoolSize(size_t maxSize) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    maxPoolSize_ = maxSize;
    
    // 如果当前池大小超过新的最大值，移除多余的对象
    while (pool_.size() > maxPoolSize_) {
        pool_.pop();
    }
}

void LogMessagePool::resetMessage(LogMessage& message) {
    // 重置所有字段到默认状态
    message.level = LogLevel::INFO;
    message.timestamp = std::chrono::system_clock::now();
    message.threadId = std::this_thread::get_id();
    message.moduleName.clear();
    message.fileName.clear();
    message.lineNumber = 0;
    message.functionName.clear();
    message.message.clear();
    message.duration.reset();
    message.memoryUsage.reset();
    
    // 保留字符串的容量以避免重新分配
    message.moduleName.reserve(64);
    message.fileName.reserve(128);
    message.functionName.reserve(64);
    message.message.reserve(256);
}

// ========== StringBufferPool Implementation ==========

constexpr size_t StringBufferPool::BUFFER_SIZES[];

StringBufferPool& StringBufferPool::getInstance() {
    static StringBufferPool instance;
    return instance;
}

StringBufferPool::StringBufferPool() {
    // 初始化统计计数器
    for (size_t i = 0; i < 4; ++i) {
        totalAcquired_[i].store(0, std::memory_order_relaxed);
        totalReleased_[i].store(0, std::memory_order_relaxed);
        cacheHits_[i].store(0, std::memory_order_relaxed);
        cacheMisses_[i].store(0, std::memory_order_relaxed);
    }
    
    // 预分配一些缓冲区
    for (size_t category = 0; category < 4; ++category) {
        std::lock_guard<std::mutex> lock(poolMutexes_[category]);
        for (size_t i = 0; i < 20; ++i) {
            std::string buffer;
            buffer.reserve(BUFFER_SIZES[category]);
            pools_[category].push(std::move(buffer));
        }
    }
}

std::string StringBufferPool::acquireBuffer(BufferSize size) {
    size_t category = static_cast<size_t>(size);
    std::lock_guard<std::mutex> lock(poolMutexes_[category]);
    
    totalAcquired_[category].fetch_add(1, std::memory_order_relaxed);
    
    if (!pools_[category].empty()) {
        auto buffer = std::move(pools_[category].top());
        pools_[category].pop();
        cacheHits_[category].fetch_add(1, std::memory_order_relaxed);
        
        // 重置缓冲区
        resetBuffer(buffer, BUFFER_SIZES[category]);
        return buffer;
    }
    
    // 池为空，创建新缓冲区
    cacheMisses_[category].fetch_add(1, std::memory_order_relaxed);
    std::string buffer;
    buffer.reserve(BUFFER_SIZES[category]);
    return buffer;
}

void StringBufferPool::releaseBuffer(std::string&& buffer, BufferSize size) {
    size_t category = static_cast<size_t>(size);
    std::lock_guard<std::mutex> lock(poolMutexes_[category]);
    
    totalReleased_[category].fetch_add(1, std::memory_order_relaxed);
    
    // 如果池未满，归还缓冲区
    if (pools_[category].size() < MAX_POOL_SIZE_PER_CATEGORY) {
        pools_[category].push(std::move(buffer));
    }
    // 如果池已满，让缓冲区自然销毁
}

StringBufferPool::BufferSize StringBufferPool::selectBufferSize(size_t requiredCapacity) {
    if (requiredCapacity <= BUFFER_SIZES[0]) {
        return BufferSize::SMALL;
    } else if (requiredCapacity <= BUFFER_SIZES[1]) {
        return BufferSize::MEDIUM;
    } else if (requiredCapacity <= BUFFER_SIZES[2]) {
        return BufferSize::LARGE;
    } else {
        return BufferSize::XLARGE;
    }
}

size_t StringBufferPool::getBufferSizeBytes(BufferSize size) {
    return BUFFER_SIZES[static_cast<size_t>(size)];
}

StringBufferPool::BufferPoolStats StringBufferPool::getStats() const {
    BufferPoolStats stats;
    
    for (size_t i = 0; i < 4; ++i) {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(poolMutexes_[i]));
        
        stats.poolSizes[i] = pools_[i].size();
        stats.totalAcquired[i] = totalAcquired_[i].load(std::memory_order_relaxed);
        stats.totalReleased[i] = totalReleased_[i].load(std::memory_order_relaxed);
        stats.cacheHits[i] = cacheHits_[i].load(std::memory_order_relaxed);
        stats.cacheMisses[i] = cacheMisses_[i].load(std::memory_order_relaxed);
        
        if (stats.totalAcquired[i] > 0) {
            stats.hitRates[i] = static_cast<double>(stats.cacheHits[i]) / stats.totalAcquired[i];
        } else {
            stats.hitRates[i] = 0.0;
        }
    }
    
    return stats;
}

void StringBufferPool::clear() {
    for (size_t i = 0; i < 4; ++i) {
        std::lock_guard<std::mutex> lock(poolMutexes_[i]);
        while (!pools_[i].empty()) {
            pools_[i].pop();
        }
        
        // 重置统计信息
        totalAcquired_[i].store(0, std::memory_order_relaxed);
        totalReleased_[i].store(0, std::memory_order_relaxed);
        cacheHits_[i].store(0, std::memory_order_relaxed);
        cacheMisses_[i].store(0, std::memory_order_relaxed);
    }
}

void StringBufferPool::resetBuffer(std::string& buffer, size_t targetCapacity) {
    buffer.clear();
    if (buffer.capacity() < targetCapacity) {
        buffer.reserve(targetCapacity);
    }
}

// ========== PooledLogMessage Implementation ==========

PooledLogMessage::PooledLogMessage() 
    : message_(LogMessagePool::getInstance().acquire()) {
}

PooledLogMessage::~PooledLogMessage() {
    if (message_) {
        LogMessagePool::getInstance().release(std::move(message_));
    }
}

PooledLogMessage::PooledLogMessage(PooledLogMessage&& other) noexcept 
    : message_(std::move(other.message_)) {
}

PooledLogMessage& PooledLogMessage::operator=(PooledLogMessage&& other) noexcept {
    if (this != &other) {
        if (message_) {
            LogMessagePool::getInstance().release(std::move(message_));
        }
        message_ = std::move(other.message_);
    }
    return *this;
}

LogMessage& PooledLogMessage::get() {
    return *message_;
}

LogMessage* PooledLogMessage::operator->() {
    return message_.get();
}

LogMessage& PooledLogMessage::operator*() {
    return *message_;
}

bool PooledLogMessage::isValid() const {
    return message_ != nullptr;
}

// ========== PooledStringBuffer Implementation ==========

PooledStringBuffer::PooledStringBuffer(StringBufferPool::BufferSize size) 
    : buffer_(StringBufferPool::getInstance().acquireBuffer(size))
    , size_(size)
    , released_(false) {
}

PooledStringBuffer::PooledStringBuffer(size_t requiredCapacity) 
    : size_(StringBufferPool::selectBufferSize(requiredCapacity))
    , released_(false) {
    buffer_ = StringBufferPool::getInstance().acquireBuffer(size_);
}

PooledStringBuffer::~PooledStringBuffer() {
    if (!released_) {
        StringBufferPool::getInstance().releaseBuffer(std::move(buffer_), size_);
    }
}

PooledStringBuffer::PooledStringBuffer(PooledStringBuffer&& other) noexcept 
    : buffer_(std::move(other.buffer_))
    , size_(other.size_)
    , released_(other.released_) {
    other.released_ = true;
}

PooledStringBuffer& PooledStringBuffer::operator=(PooledStringBuffer&& other) noexcept {
    if (this != &other) {
        if (!released_) {
            StringBufferPool::getInstance().releaseBuffer(std::move(buffer_), size_);
        }
        
        buffer_ = std::move(other.buffer_);
        size_ = other.size_;
        released_ = other.released_;
        other.released_ = true;
    }
    return *this;
}

std::string& PooledStringBuffer::get() {
    return buffer_;
}

std::string* PooledStringBuffer::operator->() {
    return &buffer_;
}

std::string& PooledStringBuffer::operator*() {
    return buffer_;
}

} // namespace logging