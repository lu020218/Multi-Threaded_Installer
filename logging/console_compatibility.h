#pragma once

#include "common/logging/logger.h"
#include "common/logging/progress_tracker.h"
#include "common/logging/configuration_manager.h"
#include "installer/console_interface.h"
#include <string>
#include <memory>

namespace MultiThreadedInstaller {

/**
 * 控制台兼容性层
 * 提供与现有ConsoleInterface兼容的接口，同时集成新的日志系统
 */
class ConsoleCompatibility {
public:
    ConsoleCompatibility();
    ~ConsoleCompatibility() = default;

    // 初始化日志系统
    void initializeLogging(bool verbose = false, bool silent = false, 
                          const std::string& logFile = "");

    // 兼容现有ConsoleInterface的方法
    void showError(const std::string& message);
    void showWarning(const std::string& message);
    void showInfo(const std::string& message);
    bool confirmAction(const std::string& prompt);

    // 增强的进度显示方法
    void showPackagingProgress(const std::string& currentFolder, float progress);
    void showInstallationProgress(const std::string& currentFolder, float progress);
    void showInstallationResult(bool success, const std::vector<std::string>& errors);

    // 进度追踪集成
    logging::ProgressTracker::ProgressId startOperation(const std::string& operationName, size_t totalItems);
    void updateOperation(logging::ProgressTracker::ProgressId id, size_t completedItems, 
                        const std::string& currentItem = "");
    void completeOperation(logging::ProgressTracker::ProgressId id);

    // 性能报告
    void generatePerformanceReport();
    void showPerformanceStats();

    // 配置管理
    void configureFromArgs(const ConsoleInterface::PackagerArgs& args);
    void configureFromArgs(const ConsoleInterface::InstallerArgs& args);

    // 获取原始ConsoleInterface实例（用于向后兼容）
    ConsoleInterface& getConsoleInterface() { return consoleInterface_; }

private:
    ConsoleInterface consoleInterface_;
    bool loggingInitialized_;
    bool verboseMode_;
    bool silentMode_;
    
    // 当前活动的进度追踪ID
    std::optional<logging::ProgressTracker::ProgressId> currentProgressId_;
    
    void setupLogLevel();
    void setupOutputSinks(const std::string& logFile);
    std::string formatProgressMessage(const std::string& operation, 
                                    const std::string& currentItem, float progress);
};

/**
 * 全局便利函数，用于快速访问兼容性层
 */
class LoggingHelper {
public:
    static void initialize(bool verbose = false, bool silent = false, 
                          const std::string& logFile = "");
    static void showProgress(const std::string& operation, const std::string& item, float progress);
    static void showError(const std::string& message);
    static void showWarning(const std::string& message);
    static void showInfo(const std::string& message);
    static void generateReport();
    
    static ConsoleCompatibility& getInstance();

private:
    static std::unique_ptr<ConsoleCompatibility> instance_;
};

} // namespace MultiThreadedInstaller

// 全局便利宏，用于快速访问日志功能
#define INIT_LOGGING(verbose, silent, logFile) \
    MultiThreadedInstaller::LoggingHelper::initialize(verbose, silent, logFile)

#define SHOW_PROGRESS(operation, item, progress) \
    MultiThreadedInstaller::LoggingHelper::showProgress(operation, item, progress)

#define SHOW_ERROR(message) \
    MultiThreadedInstaller::LoggingHelper::showError(message)

#define SHOW_WARNING(message) \
    MultiThreadedInstaller::LoggingHelper::showWarning(message)

#define SHOW_INFO(message) \
    MultiThreadedInstaller::LoggingHelper::showInfo(message)

#define GENERATE_REPORT() \
    MultiThreadedInstaller::LoggingHelper::generateReport()