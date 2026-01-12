#pragma once

#include <atomic>
#include <array>
#include <type_traits>

namespace logging {

/**
 * 无锁环形缓冲区
 * 
 * 高性能的单生产者单消费者（SPSC）无锁环形缓冲区实现
 * 使用原子操作和内存对齐优化，避免false sharing
 * 
 * @tparam T 缓冲区元素类型
 * @tparam Capacity 缓冲区容量（必须是2的幂）
 */
template<typename T, size_t Capacity>
class LockFreeRingBuffer {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, 
                  "Capacity must be power of 2");
    static_assert(std::is_move_constructible_v<T>, 
                  "T must be move constructible");
    
public:
    /**
     * 构造函数
     */
    LockFreeRingBuffer() = default;
    
    /**
     * 析构函数
     */
    ~LockFreeRingBuffer() = default;
    
    // 禁用拷贝构造和拷贝赋值
    LockFreeRingBuffer(const LockFreeRingBuffer&) = delete;
    LockFreeRingBuffer& operator=(const LockFreeRingBuffer&) = delete;
    
    // 禁用移动构造和移动赋值
    LockFreeRingBuffer(LockFreeRingBuffer&&) = delete;
    LockFreeRingBuffer& operator=(LockFreeRingBuffer&&) = delete;
    
    /**
     * 尝试将元素入队
     * @param item 要入队的元素（移动语义）
     * @return 如果成功入队返回true，缓冲区满时返回false
     */
    bool tryEnqueue(T&& item);
    
    /**
     * 尝试将元素出队
     * @param item 用于接收出队元素的引用
     * @return 如果成功出队返回true，缓冲区空时返回false
     */
    bool tryDequeue(T& item);
    
    /**
     * 获取当前缓冲区中的元素数量
     * @return 元素数量
     */
    size_t size() const;
    
    /**
     * 检查缓冲区是否为空
     * @return 如果为空返回true
     */
    bool empty() const;
    
    /**
     * 检查缓冲区是否已满
     * @return 如果已满返回true
     */
    bool full() const;
    
    /**
     * 获取缓冲区容量
     * @return 缓冲区容量
     */
    constexpr size_t capacity() const { return Capacity; }
    
    /**
     * 获取可用空间数量
     * @return 可用空间数量
     */
    size_t availableSpace() const;
    
    /**
     * 清空缓冲区
     * 注意：此操作不是线程安全的，只应在单线程环境下调用
     */
    void clear();

private:
    // 使用64字节对齐避免false sharing
    alignas(64) std::atomic<size_t> writeIndex_{0};
    alignas(64) std::atomic<size_t> readIndex_{0};
    alignas(64) std::array<T, Capacity> buffer_;
    
    /**
     * 获取下一个索引位置
     * @param current 当前索引
     * @return 下一个索引位置
     */
    constexpr size_t nextIndex(size_t current) const {
        return (current + 1) & (Capacity - 1);
    }
};

// ========== 模板方法实现 ==========

template<typename T, size_t Capacity>
bool LockFreeRingBuffer<T, Capacity>::tryEnqueue(T&& item) {
    const size_t currentWrite = writeIndex_.load(std::memory_order_relaxed);
    const size_t nextWrite = nextIndex(currentWrite);
    
    // 检查缓冲区是否已满
    if (nextWrite == readIndex_.load(std::memory_order_acquire)) {
        return false; // 缓冲区已满
    }
    
    // 移动构造元素到缓冲区
    buffer_[currentWrite] = std::move(item);
    
    // 更新写索引
    writeIndex_.store(nextWrite, std::memory_order_release);
    
    return true;
}

template<typename T, size_t Capacity>
bool LockFreeRingBuffer<T, Capacity>::tryDequeue(T& item) {
    const size_t currentRead = readIndex_.load(std::memory_order_relaxed);
    
    // 检查缓冲区是否为空
    if (currentRead == writeIndex_.load(std::memory_order_acquire)) {
        return false; // 缓冲区为空
    }
    
    // 移动元素到输出参数
    item = std::move(buffer_[currentRead]);
    
    // 更新读索引
    readIndex_.store(nextIndex(currentRead), std::memory_order_release);
    
    return true;
}

template<typename T, size_t Capacity>
size_t LockFreeRingBuffer<T, Capacity>::size() const {
    const size_t write = writeIndex_.load(std::memory_order_acquire);
    const size_t read = readIndex_.load(std::memory_order_acquire);
    
    return (write - read) & (Capacity - 1);
}

template<typename T, size_t Capacity>
bool LockFreeRingBuffer<T, Capacity>::empty() const {
    return readIndex_.load(std::memory_order_acquire) == 
           writeIndex_.load(std::memory_order_acquire);
}

template<typename T, size_t Capacity>
bool LockFreeRingBuffer<T, Capacity>::full() const {
    const size_t write = writeIndex_.load(std::memory_order_acquire);
    const size_t read = readIndex_.load(std::memory_order_acquire);
    
    return nextIndex(write) == read;
}

template<typename T, size_t Capacity>
size_t LockFreeRingBuffer<T, Capacity>::availableSpace() const {
    return Capacity - 1 - size(); // -1 因为我们保留一个位置来区分满和空
}

template<typename T, size_t Capacity>
void LockFreeRingBuffer<T, Capacity>::clear() {
    // 注意：此操作不是线程安全的
    readIndex_.store(0, std::memory_order_relaxed);
    writeIndex_.store(0, std::memory_order_relaxed);
}

} // namespace logging