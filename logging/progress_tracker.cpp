#include "common/logging/progress_tracker.h"
#include "common/logging/logger.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace logging {

// ProgressInfo 方法实现

float ProgressTracker::ProgressInfo::getPercentage() const {
    if (totalItems == 0) {
        return isCompleted ? 1.0f : 0.0f;
    }
    return static_cast<float>(completedItems) / static_cast<float>(totalItems);
}

std::chrono::milliseconds ProgressTracker::ProgressInfo::getElapsedTime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
}

std::chrono::milliseconds ProgressTracker::ProgressInfo::getEstimatedTimeRemaining() const {
    if (isCompleted || completedItems == 0) {
        return std::chrono::milliseconds(0);
    }
    
    auto elapsed = getElapsedTime();
    float progress = getPercentage();
    
    if (progress <= 0.0f) {
        return std::chrono::milliseconds(0);
    }
    
    // 估算剩余时间 = (已用时间 / 完成比例) - 已用时间
    auto totalEstimated = std::chrono::duration_cast<std::chrono::milliseconds>(
        elapsed / progress
    );
    
    return totalEstimated - elapsed;
}

// ProgressTracker 方法实现

ProgressTracker& ProgressTracker::getInstance() {
    static ProgressTracker instance;
    return instance;
}

ProgressTracker::ProgressTracker() 
    : nextId_(1)
    , autoDisplay_(false)
    , displayInterval_(1000)  // 默认1秒更新间隔
    , shouldStop_(false) {
}

ProgressTracker::~ProgressTracker() {
    shouldStop_ = true;
    if (displayThread_.joinable()) {
        displayThread_.join();
    }
}

ProgressTracker::ProgressId ProgressTracker::startProgress(const std::string& operationName, size_t totalItems) {
    ProgressId id = nextId_.fetch_add(1);
    
    ProgressInfo info;
    info.operationName = operationName;
    info.totalItems = totalItems;
    info.completedItems = 0;
    info.startTime = std::chrono::steady_clock::now();
    info.lastUpdate = info.startTime;
    info.isCompleted = false;
    
    {
        std::unique_lock<std::shared_mutex> lock(progressMutex_);
        progressMap_[id] = std::move(info);
    }
    
    // 记录操作开始日志
    Logger::getInstance().infof("ProgressTracker", "Start operation '%s' (ID: %llu, total items: %zu)", 
                               operationName.c_str(), id, totalItems);
    
    return id;
}

void ProgressTracker::updateProgress(ProgressId id, size_t completedItems, const std::string& currentItem) {
    std::unique_lock<std::shared_mutex> lock(progressMutex_);
    
    auto it = progressMap_.find(id);
    if (it == progressMap_.end() || it->second.isCompleted) {
        return;
    }
    
    // 更新进度信息
    it->second.completedItems = std::min(completedItems, it->second.totalItems);
    it->second.currentItem = currentItem;
    it->second.lastUpdate = std::chrono::steady_clock::now();
    
    // 记录进度更新日志（DEBUG级别）
    float percentage = it->second.getPercentage() * 100.0f;
    Logger::getInstance().debugf("ProgressTracker", "Update progress '%s' (ID: %llu): %.1f%% (%zu/%zu) - %s",
                                it->second.operationName.c_str(), id, percentage,
                                it->second.completedItems, it->second.totalItems,
                                currentItem.empty() ? "Processing" : currentItem.c_str());
}

void ProgressTracker::completeProgress(ProgressId id) {
    std::unique_lock<std::shared_mutex> lock(progressMutex_);
    
    auto it = progressMap_.find(id);
    if (it == progressMap_.end()) {
        return;
    }
    
    // 标记为完成
    it->second.isCompleted = true;
    it->second.completedItems = it->second.totalItems;
    it->second.lastUpdate = std::chrono::steady_clock::now();
    
    // 计算总耗时
    auto totalTime = it->second.getElapsedTime();
    
    // 记录操作完成日志
    Logger::getInstance().infof("ProgressTracker", "Complete operation '%s' (ID: %llu) - total time: %lld ms, processed items: %zu",
                               it->second.operationName.c_str(), id, totalTime.count(), it->second.totalItems);
    
    // 从映射表中移除已完成的进度
    progressMap_.erase(it);
}

void ProgressTracker::cancelProgress(ProgressId id) {
    std::unique_lock<std::shared_mutex> lock(progressMutex_);
    
    auto it = progressMap_.find(id);
    if (it == progressMap_.end()) {
        return;
    }
    
    // 记录取消日志
    auto elapsedTime = it->second.getElapsedTime();
    Logger::getInstance().warningf("ProgressTracker", "Cancel operation '%s' (ID: %llu) - completed: %zu/%zu, elapsed: %lld ms",
                                  it->second.operationName.c_str(), id, 
                                  it->second.completedItems, it->second.totalItems, elapsedTime.count());
    
    // 从映射表中移除
    progressMap_.erase(it);
}

std::optional<ProgressTracker::ProgressInfo> ProgressTracker::getProgress(ProgressId id) const {
    std::shared_lock<std::shared_mutex> lock(progressMutex_);
    
    auto it = progressMap_.find(id);
    if (it == progressMap_.end()) {
        return std::nullopt;
    }
    
    return it->second;
}

std::vector<ProgressTracker::ProgressInfo> ProgressTracker::getAllActiveProgress() const {
    std::shared_lock<std::shared_mutex> lock(progressMutex_);
    
    std::vector<ProgressInfo> result;
    result.reserve(progressMap_.size());
    
    for (const auto& pair : progressMap_) {
        result.push_back(pair.second);
    }
    
    return result;
}

void ProgressTracker::setAutoDisplay(bool enabled) {
    autoDisplay_ = enabled;
    
    if (enabled && !displayThread_.joinable()) {
        shouldStop_ = false;
        displayThread_ = std::thread(&ProgressTracker::displayThreadFunc, this);
    } else if (!enabled && displayThread_.joinable()) {
        shouldStop_ = true;
        displayThread_.join();
    }
}

void ProgressTracker::setDisplayInterval(std::chrono::milliseconds interval) {
    displayInterval_ = interval;
}

void ProgressTracker::displayProgress(ProgressId id) {
    auto progress = getProgress(id);
    if (!progress) {
        return;
    }
    
    std::string progressBar = formatProgressBar(*progress);
    std::cout << progressBar << std::endl;
}

void ProgressTracker::displayAllProgress() {
    auto allProgress = getAllActiveProgress();
    
    if (allProgress.empty()) {
        return;
    }
    
    std::cout << "\n=== Active Operation Progress ===" << std::endl;
    for (const auto& progress : allProgress) {
        std::string progressBar = formatProgressBar(progress);
        std::cout << progressBar << std::endl;
    }
    std::cout << "=================================" << std::endl;
}

void ProgressTracker::displayThreadFunc() {
    while (!shouldStop_) {
        if (autoDisplay_) {
            displayAllProgress();
        }
        
        std::this_thread::sleep_for(displayInterval_);
    }
}

std::string ProgressTracker::formatProgressBar(const ProgressInfo& info, int width) {
    std::ostringstream oss;
    
    // 操作名称
    oss << info.operationName << ": ";
    
    // 进度条
    float percentage = info.getPercentage();
    int filledWidth = static_cast<int>(percentage * width);
    
    oss << "[";
    for (int i = 0; i < width; ++i) {
        if (i < filledWidth) {
            oss << "#";
        } else {
            oss << "-";
        }
    }
    oss << "] ";
    
    // 百分比
    oss << std::fixed << std::setprecision(1) << (percentage * 100.0f) << "% ";
    
    // 项目计数
    oss << "(" << info.completedItems << "/" << info.totalItems << ")";
    
    // 当前项目
    if (!info.currentItem.empty()) {
        oss << " - " << info.currentItem;
    }
    
    // 时间信息
    auto elapsed = info.getElapsedTime();
    oss << " [elapsed: " << elapsed.count() << "ms";
    
    if (!info.isCompleted && info.completedItems > 0) {
        auto remaining = info.getEstimatedTimeRemaining();
        oss << ", remaining: " << remaining.count() << "ms";
    }
    
    oss << "]";
    
    return oss.str();
}

} // namespace logging