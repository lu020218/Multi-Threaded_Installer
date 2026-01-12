#include "common/logging/performance_monitor.h"
#include "common/logging/logger.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace logging {

// PerformanceSnapshot 实现

double PerformanceMonitor::PerformanceSnapshot::getAverageLogTime() const {
    if (totalLogCount == 0) return 0.0;
    return static_cast<double>(totalLogTime.count()) / totalLogCount;
}

double PerformanceMonitor::PerformanceSnapshot::getLogRate() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime);
    if (elapsed.count() == 0) return 0.0;
    return static_cast<double>(totalLogCount) / elapsed.count();
}

double PerformanceMonitor::PerformanceSnapshot::getDropRate() const {
    if (totalLogCount + droppedLogCount == 0) return 0.0;
    return static_cast<double>(droppedLogCount) / (totalLogCount + droppedLogCount) * 100.0;
}

// PerformanceMetrics 实现 (removed since methods moved to PerformanceSnapshot)

// OperationTimer 实现

PerformanceMonitor::OperationTimer::OperationTimer(std::string name)
    : operationName(std::move(name))
    , startTime(std::chrono::steady_clock::now()) {
}

PerformanceMonitor::OperationTimer::~OperationTimer() {
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    PerformanceMonitor::getInstance().recordOperation(operationName, duration);
}

// PerformanceMonitor 实现

PerformanceMonitor& PerformanceMonitor::getInstance() {
    static PerformanceMonitor instance;
    return instance;
}

PerformanceMonitor::PerformanceMonitor()
    : memoryThreshold_(100 * 1024 * 1024)  // 默认100MB阈值
    , shouldStop_(false)
    , reportingInterval_(std::chrono::seconds(60)) {  // 默认60秒报告间隔
    
    metrics_.startTime = std::chrono::steady_clock::now();
    
    // 初始化内存使用量
    updateMemoryUsage(getCurrentMemoryUsage());
}

PerformanceMonitor::~PerformanceMonitor() {
    disablePeriodicReporting();
}

void PerformanceMonitor::recordLogOperation(std::chrono::microseconds duration) {
    std::unique_lock<std::shared_mutex> lock(statsMutex_);
    
    metrics_.totalLogTime += duration;
    metrics_.totalLogCount.fetch_add(1);
    
    // 更新内存使用量
    size_t currentMemory = getCurrentMemoryUsage();
    updateMemoryUsage(currentMemory);
}

void PerformanceMonitor::recordDroppedLog() {
    metrics_.droppedLogCount.fetch_add(1);
}

void PerformanceMonitor::updateMemoryUsage(size_t currentUsage) {
    metrics_.currentMemoryUsage.store(currentUsage);
    
    // 更新峰值内存使用量
    size_t currentPeak = metrics_.peakMemoryUsage.load();
    while (currentUsage > currentPeak) {
        if (metrics_.peakMemoryUsage.compare_exchange_weak(currentPeak, currentUsage)) {
            break;
        }
    }
    
    // 检查是否超过阈值
    if (currentUsage > memoryThreshold_) {
        // 记录内存使用警告（需求6.3）
        try {
            Logger::getInstance().warningf("PerformanceMonitor", 
                "内存使用超过阈值: 当前 %.2f MB, 阈值 %.2f MB",
                currentUsage / (1024.0 * 1024.0),
                memoryThreshold_ / (1024.0 * 1024.0));
        } catch (...) {
            // 避免在日志系统中产生递归调用
        }
    }
}

std::unique_ptr<PerformanceMonitor::OperationTimer> PerformanceMonitor::startTimer(const std::string& operationName) {
    return std::make_unique<OperationTimer>(operationName);
}

void PerformanceMonitor::recordOperation(const std::string& operationName, std::chrono::microseconds duration) {
    std::unique_lock<std::shared_mutex> lock(statsMutex_);
    
    auto it = operationStats_.find(operationName);
    if (it != operationStats_.end()) {
        // 计算平均值（简单的移动平均）
        it->second = std::chrono::microseconds((it->second.count() + duration.count()) / 2);
    } else {
        operationStats_[operationName] = duration;
    }
}

PerformanceMonitor::PerformanceSnapshot PerformanceMonitor::getMetrics() const {
    std::shared_lock<std::shared_mutex> lock(statsMutex_);
    
    PerformanceSnapshot snapshot;
    snapshot.totalLogTime = metrics_.totalLogTime;
    snapshot.totalLogCount = metrics_.totalLogCount.load();
    snapshot.droppedLogCount = metrics_.droppedLogCount.load();
    snapshot.currentMemoryUsage = metrics_.currentMemoryUsage.load();
    snapshot.peakMemoryUsage = metrics_.peakMemoryUsage.load();
    snapshot.startTime = metrics_.startTime;
    
    return snapshot;
}

std::unordered_map<std::string, std::chrono::microseconds> PerformanceMonitor::getOperationStats() const {
    std::shared_lock<std::shared_mutex> lock(statsMutex_);
    return operationStats_;
}

void PerformanceMonitor::setMemoryThreshold(size_t threshold) {
    memoryThreshold_ = threshold;
}

void PerformanceMonitor::enablePeriodicReporting(std::chrono::seconds interval) {
    disablePeriodicReporting();  // 先停止现有的报告线程
    
    reportingInterval_ = interval;
    shouldStop_.store(false);
    reportingThread_ = std::thread(&PerformanceMonitor::reportingThreadFunc, this);
}

void PerformanceMonitor::disablePeriodicReporting() {
    shouldStop_.store(true);
    if (reportingThread_.joinable()) {
        reportingThread_.join();
    }
}

void PerformanceMonitor::generateReport() {
    std::string report = getPerformanceReport();
    
    try {
        // 输出性能报告到日志（需求6.2）
        Logger::getInstance().infof("PerformanceMonitor", "=== 性能报告 ===");
        
        std::istringstream iss(report);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                Logger::getInstance().infof("PerformanceMonitor", "%s", line.c_str());
            }
        }
    } catch (...) {
        // 避免在日志系统中产生递归调用
    }
}

std::string PerformanceMonitor::getPerformanceReport() const {
    std::ostringstream oss;
    
    auto metrics = getMetrics();
    auto operationStats = getOperationStats();
    
    // 计算运行时间
    auto now = std::chrono::steady_clock::now();
    auto totalRuntime = std::chrono::duration_cast<std::chrono::milliseconds>(now - metrics.startTime);
    
    oss << "运行时间: " << totalRuntime.count() << "ms\n";
    oss << "总日志数量: " << metrics.totalLogCount << "\n";
    oss << "丢弃日志数量: " << metrics.droppedLogCount << "\n";
    oss << "日志丢弃率: " << std::fixed << std::setprecision(2) << metrics.getDropRate() << "%\n";
    oss << "平均日志时间: " << std::fixed << std::setprecision(2) << metrics.getAverageLogTime() << "μs\n";
    oss << "日志记录速率: " << std::fixed << std::setprecision(1) << metrics.getLogRate() << " logs/s\n";
    oss << "当前内存使用: " << std::fixed << std::setprecision(2) 
        << metrics.currentMemoryUsage / (1024.0 * 1024.0) << "MB\n";
    oss << "峰值内存使用: " << std::fixed << std::setprecision(2) 
        << metrics.peakMemoryUsage / (1024.0 * 1024.0) << "MB\n";
    
    if (!operationStats.empty()) {
        oss << "\n操作统计:\n";
        for (const auto& [name, duration] : operationStats) {
            oss << "  " << name << ": " << std::fixed << std::setprecision(2) 
                << duration.count() / 1000.0 << "ms (平均)\n";
        }
    }
    
    return oss.str();
}

void PerformanceMonitor::reportingThreadFunc() {
    while (!shouldStop_.load()) {
        // 等待报告间隔或停止信号
        auto start = std::chrono::steady_clock::now();
        while (!shouldStop_.load()) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= reportingInterval_) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        if (!shouldStop_.load()) {
            // 生成定期报告（需求6.5）
            try {
                Logger::getInstance().infof("PerformanceMonitor", "定期性能报告:");
                
                auto metrics = getMetrics();
                Logger::getInstance().infof("PerformanceMonitor", 
                    "内存使用: %.2f MB, 日志速率: %.1f logs/s, 丢弃率: %.2f%%",
                    metrics.currentMemoryUsage / (1024.0 * 1024.0),
                    metrics.getLogRate(),
                    metrics.getDropRate());
            } catch (...) {
                // 避免在日志系统中产生递归调用
            }
        }
    }
}

size_t PerformanceMonitor::getCurrentMemoryUsage() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // Linux返回的是KB，转换为字节
        return usage.ru_maxrss * 1024;
    }
    return 0;
#endif
}

} // namespace logging