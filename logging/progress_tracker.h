#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace logging {

/**
 * @brief 进度追踪器 - 用于追踪长时间运行操作的进度
 * 
 * 该类提供了多操作并行追踪功能，支持自动显示和更新机制。
 * 线程安全，支持多个线程同时创建和更新不同的进度追踪。
 */
class ProgressTracker {
public:
    /**
     * @brief 进度信息结构
     */
    struct ProgressInfo {
        std::string operationName;      ///< 操作名称
        std::string currentItem;        ///< 当前处理项目
        size_t totalItems;              ///< 总项目数
        size_t completedItems;          ///< 已完成项目数
        std::chrono::steady_clock::time_point startTime;    ///< 开始时间
        std::chrono::steady_clock::time_point lastUpdate;   ///< 最后更新时间
        bool isCompleted;               ///< 是否已完成
        
        /**
         * @brief 获取完成百分比
         * @return 完成百分比 (0.0 - 1.0)
         */
        float getPercentage() const;
        
        /**
         * @brief 获取已经过时间
         * @return 已经过时间（毫秒）
         */
        std::chrono::milliseconds getElapsedTime() const;
        
        /**
         * @brief 获取预估剩余时间
         * @return 预估剩余时间（毫秒）
         */
        std::chrono::milliseconds getEstimatedTimeRemaining() const;
    };
    
    using ProgressId = uint64_t;
    
    /**
     * @brief 获取单例实例
     * @return ProgressTracker单例引用
     */
    static ProgressTracker& getInstance();
    
    // 进度管理
    
    /**
     * @brief 开始新的进度追踪
     * @param operationName 操作名称
     * @param totalItems 总项目数
     * @return 进度追踪ID
     */
    ProgressId startProgress(const std::string& operationName, size_t totalItems);
    
    /**
     * @brief 更新进度
     * @param id 进度追踪ID
     * @param completedItems 已完成项目数
     * @param currentItem 当前处理项目名称（可选）
     */
    void updateProgress(ProgressId id, size_t completedItems, const std::string& currentItem = "");
    
    /**
     * @brief 完成进度追踪
     * @param id 进度追踪ID
     */
    void completeProgress(ProgressId id);
    
    /**
     * @brief 取消进度追踪
     * @param id 进度追踪ID
     */
    void cancelProgress(ProgressId id);
    
    // 进度查询
    
    /**
     * @brief 获取指定进度信息
     * @param id 进度追踪ID
     * @return 进度信息（如果存在）
     */
    std::optional<ProgressInfo> getProgress(ProgressId id) const;
    
    /**
     * @brief 获取所有活跃的进度信息
     * @return 活跃进度信息列表
     */
    std::vector<ProgressInfo> getAllActiveProgress() const;
    
    // 显示控制
    
    /**
     * @brief 设置自动显示模式
     * @param enabled 是否启用自动显示
     */
    void setAutoDisplay(bool enabled);
    
    /**
     * @brief 设置显示间隔
     * @param interval 显示间隔时间
     */
    void setDisplayInterval(std::chrono::milliseconds interval);
    
    /**
     * @brief 显示指定进度
     * @param id 进度追踪ID
     */
    void displayProgress(ProgressId id);
    
    /**
     * @brief 显示所有活跃进度
     */
    void displayAllProgress();
    
private:
    ProgressTracker();
    ~ProgressTracker();
    
    // 禁用拷贝和赋值
    ProgressTracker(const ProgressTracker&) = delete;
    ProgressTracker& operator=(const ProgressTracker&) = delete;
    
    std::atomic<ProgressId> nextId_;                                    ///< 下一个进度ID
    std::unordered_map<ProgressId, ProgressInfo> progressMap_;          ///< 进度映射表
    mutable std::shared_mutex progressMutex_;                           ///< 进度映射表读写锁
    
    bool autoDisplay_;                                                  ///< 自动显示标志
    std::chrono::milliseconds displayInterval_;                         ///< 显示间隔
    std::thread displayThread_;                                         ///< 显示线程
    std::atomic<bool> shouldStop_;                                      ///< 停止标志
    
    /**
     * @brief 显示线程函数
     */
    void displayThreadFunc();
    
    /**
     * @brief 格式化进度条
     * @param info 进度信息
     * @param width 进度条宽度
     * @return 格式化的进度条字符串
     */
    std::string formatProgressBar(const ProgressInfo& info, int width = 50);
};

} // namespace logging