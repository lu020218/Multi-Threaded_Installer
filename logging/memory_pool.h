#pragma once

#include "log_message.h"
#include <memory>
#include <vector>
#include <stack>
#include <mutex>
#include <atomic>
#include <string>
#include <array>

namespace logging {

/**
 * 高性能内存池，用于LogMessage对象复用
 * 减少频繁的内存分配和释放开销
 */
class LogMessagePool {
public:
    /**
     * 获取单例实例
     * @return LogMessagePool实例的引用
     */
    static LogMessagePool& getInstance();
    
    /**
     * 从池中获取LogMessage对象
     * @return 可复用的LogMessage对象的智能指针
     */
    std::unique_ptr<LogMessage> acquire();
    
    /**
     * 将LogMessage对象归还到池中
     * @param message 要归还的LogMessage对象
     */
    void release(std::unique_ptr<LogMessage> message);
    
    /**
     * 获取池的统计信息
     * @return 包含池大小、命中率等信息的结构
     */
    struct PoolStats {
        size_t poolSize;
        size_t totalAcquired;
        size_t totalReleased;
        size_t cacheHits;
        size_t cacheMisses;
        double hitRate;
    };
    
    PoolStats getStats() const;
    
    /**
     * 清空池（用于测试和清理）
     */
    void clear();
    
    /**
     * 设置池的最大大小
     * @param maxSize 最大池大小
     */
    void setMaxPoolSize(size_t maxSize);

private:
    LogMessagePool();
    ~LogMessagePool() = default;
    
    // 禁用拷贝和移动
    LogMessagePool(const LogMessagePool&) = delete;
    LogMessagePool& operator=(const LogMessagePool&) = delete;
    
    mutable std::mutex poolMutex_;
    std::stack<std::unique_ptr<LogMessage>> pool_;
    size_t maxPoolSize_;
    
    // 统计信息
    mutable std::atomic<size_t> totalAcquired_{0};
    mutable std::atomic<size_t> totalReleased_{0};
    mutable std::atomic<size_t> cacheHits_{0};
    mutable std::atomic<size_t> cacheMisses_{0};
    
    /**
     * 重置LogMessage对象到初始状态
     * @param message 要重置的LogMessage对象
     */
    void resetMessage(LogMessage& message);
};

/**
 * 字符串缓冲区池，用于减少字符串分配开销
 * 提供不同大小的预分配字符串缓冲区
 */
class StringBufferPool {
public:
    /**
     * 缓冲区大小类别
     */
    enum class BufferSize {
        SMALL = 0,   // 64 bytes
        MEDIUM = 1,  // 256 bytes  
        LARGE = 2,   // 1024 bytes
        XLARGE = 3   // 4096 bytes
    };
    
    /**
     * 获取单例实例
     * @return StringBufferPool实例的引用
     */
    static StringBufferPool& getInstance();
    
    /**
     * 获取指定大小的字符串缓冲区
     * @param size 缓冲区大小类别
     * @return 预分配的字符串对象
     */
    std::string acquireBuffer(BufferSize size);
    
    /**
     * 归还字符串缓冲区到池中
     * @param buffer 要归还的字符串缓冲区
     * @param size 缓冲区大小类别
     */
    void releaseBuffer(std::string&& buffer, BufferSize size);
    
    /**
     * 根据所需容量自动选择合适的缓冲区大小
     * @param requiredCapacity 所需容量
     * @return 合适的缓冲区大小类别
     */
    static BufferSize selectBufferSize(size_t requiredCapacity);
    
    /**
     * 获取缓冲区大小的字节数
     * @param size 缓冲区大小类别
     * @return 对应的字节数
     */
    static size_t getBufferSizeBytes(BufferSize size);
    
    /**
     * 获取池的统计信息
     */
    struct BufferPoolStats {
        std::array<size_t, 4> poolSizes;
        std::array<size_t, 4> totalAcquired;
        std::array<size_t, 4> totalReleased;
        std::array<size_t, 4> cacheHits;
        std::array<size_t, 4> cacheMisses;
        std::array<double, 4> hitRates;
    };
    
    BufferPoolStats getStats() const;
    
    /**
     * 清空所有缓冲区池
     */
    void clear();

private:
    StringBufferPool();
    ~StringBufferPool() = default;
    
    // 禁用拷贝和移动
    StringBufferPool(const StringBufferPool&) = delete;
    StringBufferPool& operator=(const StringBufferPool&) = delete;
    
    static constexpr size_t BUFFER_SIZES[] = {64, 256, 1024, 4096};
    static constexpr size_t MAX_POOL_SIZE_PER_CATEGORY = 100;
    
    std::array<std::mutex, 4> poolMutexes_;
    std::array<std::stack<std::string>, 4> pools_;
    
    // 统计信息
    mutable std::array<std::atomic<size_t>, 4> totalAcquired_;
    mutable std::array<std::atomic<size_t>, 4> totalReleased_;
    mutable std::array<std::atomic<size_t>, 4> cacheHits_;
    mutable std::array<std::atomic<size_t>, 4> cacheMisses_;
    
    /**
     * 重置字符串缓冲区
     * @param buffer 要重置的字符串缓冲区
     * @param targetCapacity 目标容量
     */
    void resetBuffer(std::string& buffer, size_t targetCapacity);
};

/**
 * RAII风格的LogMessage获取器
 * 自动从池中获取和归还LogMessage对象
 */
class PooledLogMessage {
public:
    /**
     * 构造函数，从池中获取LogMessage对象
     */
    PooledLogMessage();
    
    /**
     * 析构函数，自动归还LogMessage对象到池中
     */
    ~PooledLogMessage();
    
    /**
     * 禁用拷贝构造和拷贝赋值
     */
    PooledLogMessage(const PooledLogMessage&) = delete;
    PooledLogMessage& operator=(const PooledLogMessage&) = delete;
    
    /**
     * 支持移动构造和移动赋值
     */
    PooledLogMessage(PooledLogMessage&& other) noexcept;
    PooledLogMessage& operator=(PooledLogMessage&& other) noexcept;
    
    /**
     * 获取LogMessage对象的引用
     * @return LogMessage对象的引用
     */
    LogMessage& get();
    
    /**
     * 获取LogMessage对象的指针
     * @return LogMessage对象的指针
     */
    LogMessage* operator->();
    
    /**
     * 获取LogMessage对象的引用
     * @return LogMessage对象的引用
     */
    LogMessage& operator*();
    
    /**
     * 检查是否持有有效的LogMessage对象
     * @return 如果持有有效对象则返回true
     */
    bool isValid() const;

private:
    std::unique_ptr<LogMessage> message_;
};

/**
 * RAII风格的字符串缓冲区获取器
 * 自动从池中获取和归还字符串缓冲区
 */
class PooledStringBuffer {
public:
    /**
     * 构造函数，从池中获取指定大小的字符串缓冲区
     * @param size 缓冲区大小类别
     */
    explicit PooledStringBuffer(StringBufferPool::BufferSize size);
    
    /**
     * 构造函数，根据所需容量自动选择合适的缓冲区
     * @param requiredCapacity 所需容量
     */
    explicit PooledStringBuffer(size_t requiredCapacity);
    
    /**
     * 析构函数，自动归还字符串缓冲区到池中
     */
    ~PooledStringBuffer();
    
    /**
     * 禁用拷贝构造和拷贝赋值
     */
    PooledStringBuffer(const PooledStringBuffer&) = delete;
    PooledStringBuffer& operator=(const PooledStringBuffer&) = delete;
    
    /**
     * 支持移动构造和移动赋值
     */
    PooledStringBuffer(PooledStringBuffer&& other) noexcept;
    PooledStringBuffer& operator=(PooledStringBuffer&& other) noexcept;
    
    /**
     * 获取字符串缓冲区的引用
     * @return 字符串缓冲区的引用
     */
    std::string& get();
    
    /**
     * 获取字符串缓冲区的指针
     * @return 字符串缓冲区的指针
     */
    std::string* operator->();
    
    /**
     * 获取字符串缓冲区的引用
     * @return 字符串缓冲区的引用
     */
    std::string& operator*();

private:
    std::string buffer_;
    StringBufferPool::BufferSize size_;
    bool released_;
};

} // namespace logging