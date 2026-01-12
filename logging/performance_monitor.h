#pragma once

#include <chrono>
#include <atomic>
#include <unordered_map>
#include <memory>
#include <string>
#include <thread>
#include <shared_mutex>

namespace logging {

/**
 * @brief 性能监控器类，用于收集和报告系统性能指标
 * 
 * 该类提供操作计时、内存监控、性能统计等功能，支持多线程环境下的安全使用。
 * 采用单例模式，确保全局唯一的性能监控实例。
 */
class PerformanceMonitor {
public:
    /**
     * @brief 性能指标快照结构体（用于返回值）
     */
    struct PerformanceSnapshot {
        std::chrono::microseconds totalLogTime{0};      ///< 总日志记录时间
        uint64_t totalLogCount{0};                      ///< 总日志记录数量
        uint64_t droppedLogCount{0};                    ///< 丢弃的日志数量
        size_t currentMemoryUsage{0};                   ///< 当前内存使用量
        size_t peakMemoryUsage{0};                      ///< 峰值内存使用量
        std::chrono::steady_clock::time_point startTime; ///< 监控开始时间
        
        /**
         * @brief 获取平均日志记录时间
         * @return 平均时间（微秒）
         */
        double getAverageLogTime() const;
        
        /**
         * @brief 获取日志记录速率
         * @return 每秒日志数量
         */
        double getLogRate() const;
        
        /**
         * @brief 获取日志丢弃率
         * @return 丢弃率百分比
         */
        double getDropRate() const;
    };

    /**
     * @brief 性能指标结构体
     */
    struct PerformanceMetrics {
        std::chrono::microseconds totalLogTime{0};      ///< 总日志记录时间
        std::atomic<uint64_t> totalLogCount{0};         ///< 总日志记录数量
        std::atomic<uint64_t> droppedLogCount{0};       ///< 丢弃的日志数量
        std::atomic<size_t> currentMemoryUsage{0};      ///< 当前内存使用量
        std::atomic<size_t> peakMemoryUsage{0};         ///< 峰值内存使用量
        std::chrono::steady_clock::time_point startTime; ///< 监控开始时间
        
        // 禁用拷贝构造和赋值（因为包含atomic成员）
        PerformanceMetrics() = default;
        PerformanceMetrics(const PerformanceMetrics&) = delete;
        PerformanceMetrics& operator=(const PerformanceMetrics&) = delete;
        PerformanceMetrics(PerformanceMetrics&&) = default;
        PerformanceMetrics& operator=(PerformanceMetrics&&) = default;
    };

    /**
     * @brief 操作计时器类，用于自动计时操作
     * 
     * 使用RAII模式，在构造时开始计时，析构时自动记录操作时间
     */
    struct OperationTimer {
        std::string operationName;                      ///< 操作名称
        std::chrono::steady_clock::time_point startTime; ///< 开始时间
        
        /**
         * @brief 构造函数，开始计时
         * @param name 操作名称
         */
        OperationTimer(std::string name);
        
        /**
         * @brief 析构函数，自动记录操作时间
         */
        ~OperationTimer();
    };

    /**
     * @brief 获取单例实例
     * @return PerformanceMonitor实例引用
     */
    static PerformanceMonitor& getInstance();

    // 性能指标记录
    
    /**
     * @brief 记录日志操作性能
     * @param duration 操作耗时
     */
    void recordLogOperation(std::chrono::microseconds duration);
    
    /**
     * @brief 记录丢弃的日志
     */
    void recordDroppedLog();
    
    /**
     * @brief 更新内存使用量
     * @param currentUsage 当前内存使用量（字节）
     */
    void updateMemoryUsage(size_t currentUsage);

    // 操作计时
    
    /**
     * @brief 开始操作计时
     * @param operationName 操作名称
     * @return 计时器智能指针
     */
    std::unique_ptr<OperationTimer> startTimer(const std::string& operationName);
    
    /**
     * @brief 记录操作时间
     * @param operationName 操作名称
     * @param duration 操作耗时
     */
    void recordOperation(const std::string& operationName, std::chrono::microseconds duration);

    // 指标查询
    
    /**
     * @brief 获取性能指标快照
     * @return 性能指标快照
     */
    PerformanceSnapshot getMetrics() const;
    
    /**
     * @brief 获取操作统计信息
     * @return 操作名称到平均耗时的映射
     */
    std::unordered_map<std::string, std::chrono::microseconds> getOperationStats() const;

    // 监控控制
    
    /**
     * @brief 设置内存使用阈值
     * @param threshold 阈值（字节）
     */
    void setMemoryThreshold(size_t threshold);
    
    /**
     * @brief 启用定期报告
     * @param interval 报告间隔
     */
    void enablePeriodicReporting(std::chrono::seconds interval);
    
    /**
     * @brief 禁用定期报告
     */
    void disablePeriodicReporting();

    // 报告生成
    
    /**
     * @brief 生成性能报告（输出到日志）
     */
    void generateReport();
    
    /**
     * @brief 获取性能报告字符串
     * @return 格式化的性能报告
     */
    std::string getPerformanceReport() const;

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    PerformanceMonitor();
    
    /**
     * @brief 析构函数
     */
    ~PerformanceMonitor();
    
    // 禁用拷贝和赋值
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

    PerformanceMetrics metrics_;                        ///< 性能指标
    std::unordered_map<std::string, std::chrono::microseconds> operationStats_; ///< 操作统计
    mutable std::shared_mutex statsMutex_;              ///< 统计数据互斥锁
    
    size_t memoryThreshold_;                            ///< 内存使用阈值
    std::thread reportingThread_;                       ///< 报告线程
    std::atomic<bool> shouldStop_;                      ///< 停止标志
    std::chrono::seconds reportingInterval_;            ///< 报告间隔
    
    /**
     * @brief 报告线程函数
     */
    void reportingThreadFunc();
    
    /**
     * @brief 获取当前内存使用量
     * @return 内存使用量（字节）
     */
    size_t getCurrentMemoryUsage();
};

} // namespace logging