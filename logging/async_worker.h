#pragma once

#include "log_message.h"
#include "log_sink.h"
#include "lock_free_ring_buffer.h"
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace logging {

/**
 * 异步日志工作线程
 * 
 * 负责在后台线程中处理日志消息的异步输出
 * 使用无锁环形缓冲区接收日志消息，批量处理以提高性能
 */
class AsyncWorker {
public:
    /**
     * 异步工作线程配置
     */
    struct Config {
        size_t bufferSize = 8192;                                    // 缓冲区大小（必须是2的幂）
        std::chrono::milliseconds flushInterval{1000};              // 刷新间隔
        std::chrono::milliseconds shutdownTimeout{5000};            // 关闭超时
        size_t batchSize = 64;                                       // 批处理大小
        bool dropOnOverflow = true;                                  // 溢出时是否丢弃消息
    };
    
    /**
     * 构造函数
     * @param config 配置参数
     */
    explicit AsyncWorker(const Config& config = {});
    
    /**
     * 析构函数
     */
    ~AsyncWorker();
    
    // 禁用拷贝构造和拷贝赋值
    AsyncWorker(const AsyncWorker&) = delete;
    AsyncWorker& operator=(const AsyncWorker&) = delete;
    
    // 禁用移动构造和移动赋值
    AsyncWorker(AsyncWorker&&) = delete;
    AsyncWorker& operator=(AsyncWorker&&) = delete;
    
    /**
     * 启动异步工作线程
     * @return 如果成功启动返回true
     */
    bool start();
    
    /**
     * 停止异步工作线程
     * @param waitForCompletion 是否等待所有消息处理完成
     */
    void stop(bool waitForCompletion = true);
    
    /**
     * 检查工作线程是否正在运行
     * @return 如果正在运行返回true
     */
    bool isRunning() const;
    
    /**
     * 异步记录日志消息
     * @param message 日志消息
     * @return 如果成功入队返回true，缓冲区满时根据配置决定是否丢弃
     */
    bool logAsync(LogMessage&& message);
    
    /**
     * 添加日志输出器
     * @param sink 输出器智能指针
     */
    void addSink(std::shared_ptr<LogSink> sink);
    
    /**
     * 移除日志输出器
     * @param sinkName 输出器名称
     */
    void removeSink(const std::string& sinkName);
    
    /**
     * 清除所有输出器
     */
    void clearSinks();
    
    /**
     * 获取所有输出器
     * @return 输出器列表
     */
    std::vector<std::shared_ptr<LogSink>> getSinks() const;
    
    /**
     * 强制刷新所有输出器
     */
    void flush();
    
    /**
     * 获取统计信息
     */
    struct Statistics {
        std::atomic<uint64_t> messagesProcessed{0};      // 已处理消息数
        std::atomic<uint64_t> messagesDropped{0};        // 丢弃消息数
        std::atomic<uint64_t> batchesProcessed{0};       // 已处理批次数
        std::atomic<uint64_t> flushOperations{0};        // 刷新操作数
        std::chrono::steady_clock::time_point startTime; // 启动时间
        
        // 添加拷贝构造函数
        Statistics() = default;
        Statistics(const Statistics& other) 
            : messagesProcessed(other.messagesProcessed.load())
            , messagesDropped(other.messagesDropped.load())
            , batchesProcessed(other.batchesProcessed.load())
            , flushOperations(other.flushOperations.load())
            , startTime(other.startTime) {}
        
        // 添加拷贝赋值操作符
        Statistics& operator=(const Statistics& other) {
            if (this != &other) {
                messagesProcessed.store(other.messagesProcessed.load());
                messagesDropped.store(other.messagesDropped.load());
                batchesProcessed.store(other.batchesProcessed.load());
                flushOperations.store(other.flushOperations.load());
                startTime = other.startTime;
            }
            return *this;
        }
        
        /**
         * 获取消息处理速率（消息/秒）
         * @return 处理速率
         */
        double getMessageRate() const;
        
        /**
         * 获取丢弃率
         * @return 丢弃率（0.0-1.0）
         */
        double getDropRate() const;
        
        /**
         * 获取运行时间
         * @return 运行时间
         */
        std::chrono::milliseconds getUptime() const;
    };
    
    /**
     * 获取统计信息
     * @return 统计信息
     */
    Statistics getStatistics() const;
    
    /**
     * 重置统计信息
     */
    void resetStatistics();
    
    /**
     * 更新配置
     * @param config 新配置
     * @return 如果成功更新返回true
     */
    bool updateConfig(const Config& config);
    
    /**
     * 获取当前配置
     * @return 当前配置
     */
    Config getConfig() const;

private:
    /**
     * 工作线程主函数
     */
    void workerThreadFunc();
    
    /**
     * 处理一批日志消息
     * @return 处理的消息数量
     */
    size_t processBatch();
    
    /**
     * 分发消息到所有输出器
     * @param message 日志消息
     */
    void dispatchMessage(const LogMessage& message);
    
    /**
     * 刷新所有输出器
     */
    void flushSinks();
    
    /**
     * 等待工作线程结束
     */
    void waitForWorkerThread();
    
    // ========== 成员变量 ==========
    
    Config config_;                                      // 配置参数
    
    // 缓冲区（使用动态大小）
    std::unique_ptr<LockFreeRingBuffer<LogMessage, 8192>> buffer_;
    
    // 线程管理
    std::thread workerThread_;                           // 工作线程
    std::atomic<bool> running_{false};                   // 运行状态
    std::atomic<bool> shouldStop_{false};                // 停止标志
    
    // 输出器管理
    mutable std::mutex sinksMutex_;                      // 输出器互斥锁
    std::vector<std::shared_ptr<LogSink>> sinks_;        // 输出器列表
    
    // 多生产者支持
    std::mutex enqueueMutex_;                            // 入队互斥锁（多生产者支持）
    
    // 刷新控制
    std::mutex flushMutex_;                              // 刷新互斥锁
    std::condition_variable flushCondition_;             // 刷新条件变量
    std::atomic<bool> flushRequested_{false};            // 刷新请求标志
    
    // 统计信息
    mutable Statistics statistics_;                      // 统计信息
    
    // 批处理缓冲区
    std::vector<LogMessage> batchBuffer_;                // 批处理缓冲区
};

} // namespace logging