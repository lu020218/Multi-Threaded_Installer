#include "common/logging/console_compatibility.h"
#include "common/logging/logging_macros.h"
#include <iostream>
#include <iomanip>

namespace MultiThreadedInstaller {

ConsoleCompatibility::ConsoleCompatibility() 
    : loggingInitialized_(false), verboseMode_(false), silentMode_(false) {
}

void ConsoleCompatibility::initializeLogging(bool verbose, bool silent, const std::string& logFile) {
    verboseMode_ = verbose;
    silentMode_ = silent;
    
    // 配置日志系统
    auto& config = logging::ConfigurationManager::getInstance().getConfig();
    
    // 设置日志级别
    if (silent) {
        config.globalLogLevel = logging::LogLevel::ERROR;
        config.consoleEnabled = false;  // 静默模式下禁用控制台输出
    } else if (verbose) {
        config.globalLogLevel = logging::LogLevel::DEBUG;
        config.verboseMode = true;
    } else {
        config.globalLogLevel = logging::LogLevel::INFO;
    }
    
    // 配置控制台输出
    config.consoleEnabled = !silent;
    config.colorEnabled = true;
    
    // 配置文件输出
    if (!logFile.empty()) {
        config.fileEnabled = true;
        config.filePath = logFile;
        config.maxFileSize = 10 * 1024 * 1024;  // 10MB
        config.maxFiles = 5;
        config.autoFlush = true;
    }
    
    // 配置异步处理
    config.asyncEnabled = true;
    config.bufferSize = 8192;
    config.flushInterval = std::chrono::milliseconds(1000);
    
    // 应用配置
    logging::ConfigurationManager::getInstance().setConfig(config);
    
    // 启用自动进度显示
    logging::ProgressTracker::getInstance().setAutoDisplay(true);
    logging::ProgressTracker::getInstance().setDisplayInterval(std::chrono::milliseconds(500));
    
    // 启用性能监控
    logging::PerformanceMonitor::getInstance().setMemoryThreshold(100 * 1024 * 1024);  // 100MB
    
    loggingInitialized_ = true;
    
    LOG_INFO("ConsoleCompatibility", "日志系统初始化完成");
    if (verbose) {
        LOG_DEBUG("ConsoleCompatibility", "启用详细模式");
    }
    if (silent) {
        LOG_INFO("ConsoleCompatibility", "启用静默模式");
    }
    if (!logFile.empty()) {
        LOG_INFOF("ConsoleCompatibility", "日志文件: %s", logFile.c_str());
    }
}

void ConsoleCompatibility::showError(const std::string& message) {
    LOG_ERROR("Console", message);
    
    // 在静默模式下，错误仍然需要显示到stderr
    if (silentMode_) {
        std::cerr << "ERROR: " << message << std::endl;
    }
}

void ConsoleCompatibility::showWarning(const std::string& message) {
    LOG_WARNING("Console", message);
    
    // 非静默模式下使用原有接口显示
    if (!silentMode_) {
        consoleInterface_.showWarning(message);
    }
}

void ConsoleCompatibility::showInfo(const std::string& message) {
    LOG_INFO("Console", message);
    
    // 非静默模式下使用原有接口显示
    if (!silentMode_) {
        consoleInterface_.showInfo(message);
    }
}

bool ConsoleCompatibility::confirmAction(const std::string& prompt) {
    LOG_INFOF("Console", "用户确认提示: %s", prompt.c_str());
    
    if (silentMode_) {
        // 静默模式下自动确认
        LOG_INFO("Console", "静默模式，自动确认操作");
        return true;
    }
    
    bool result = consoleInterface_.confirmAction(prompt);
    LOG_INFOF("Console", "用户选择: %s", result ? "确认" : "取消");
    return result;
}

void ConsoleCompatibility::showPackagingProgress(const std::string& currentFolder, float progress) {
    if (currentProgressId_) {
        // 使用新的进度追踪系统
        size_t completed = static_cast<size_t>(progress * 100);
        UPDATE_PROGRESS(*currentProgressId_, completed, currentFolder);
    }
    
    LOG_DEBUGF("PackagingProgress", "打包进度: %s (%.1f%%)", currentFolder.c_str(), progress * 100);
    
    // 非静默模式下显示传统进度条
    if (!silentMode_) {
        consoleInterface_.showPackagingProgress(currentFolder, progress);
    }
}

void ConsoleCompatibility::showInstallationProgress(const std::string& currentFolder, float progress) {
    if (currentProgressId_) {
        // 使用新的进度追踪系统
        size_t completed = static_cast<size_t>(progress * 100);
        UPDATE_PROGRESS(*currentProgressId_, completed, currentFolder);
    }
    
    LOG_DEBUGF("InstallationProgress", "安装进度: %s (%.1f%%)", currentFolder.c_str(), progress * 100);
    
    // 非静默模式下显示传统进度条
    if (!silentMode_) {
        consoleInterface_.showInstallationProgress(currentFolder, progress);
    }
}

void ConsoleCompatibility::showInstallationResult(bool success, const std::vector<std::string>& errors) {
    if (success) {
        LOG_INFO("Installation", "安装成功完成");
    } else {
        LOG_ERROR("Installation", "安装完成但有错误");
        for (const auto& error : errors) {
            LOG_ERRORF("Installation", "错误: %s", error.c_str());
        }
    }
    
    // 非静默模式下使用原有接口显示结果
    if (!silentMode_) {
        consoleInterface_.showInstallationResult(success, errors);
    }
}

logging::ProgressTracker::ProgressId ConsoleCompatibility::startOperation(const std::string& operationName, size_t totalItems) {
    auto progressId = START_PROGRESS(operationName, totalItems);
    currentProgressId_ = progressId;
    
    LOG_INFOF("Operation", "开始操作: %s (总项目数: %zu)", operationName.c_str(), totalItems);
    
    return progressId;
}

void ConsoleCompatibility::updateOperation(logging::ProgressTracker::ProgressId id, size_t completedItems, 
                                         const std::string& currentItem) {
    UPDATE_PROGRESS(id, completedItems, currentItem);
    
    if (verboseMode_ && !currentItem.empty()) {
        LOG_DEBUGF("Operation", "处理项目: %s (%zu 已完成)", currentItem.c_str(), completedItems);
    }
}

void ConsoleCompatibility::completeOperation(logging::ProgressTracker::ProgressId id) {
    COMPLETE_PROGRESS(id);
    
    if (currentProgressId_ && *currentProgressId_ == id) {
        currentProgressId_.reset();
    }
    
    LOG_INFO("Operation", "操作完成");
}

void ConsoleCompatibility::generatePerformanceReport() {
    LOG_INFO("Performance", "生成性能报告");
    logging::PerformanceMonitor::getInstance().generateReport();
    
    if (!silentMode_) {
        showPerformanceStats();
    }
}

void ConsoleCompatibility::showPerformanceStats() {
    auto metrics = logging::PerformanceMonitor::getInstance().getMetrics();
    auto operationStats = logging::PerformanceMonitor::getInstance().getOperationStats();
    
    std::cout << "\n=== 性能统计报告 ===" << std::endl;
    std::cout << "总日志数量: " << metrics.totalLogCount << std::endl;
    std::cout << "丢弃日志数量: " << metrics.droppedLogCount << std::endl;
    std::cout << "平均日志时间: " << std::fixed << std::setprecision(2) 
              << metrics.getAverageLogTime() << " 微秒" << std::endl;
    std::cout << "日志处理速率: " << std::fixed << std::setprecision(0) 
              << metrics.getLogRate() << " 条/秒" << std::endl;
    std::cout << "当前内存使用: " << metrics.currentMemoryUsage / 1024 / 1024 << " MB" << std::endl;
    std::cout << "峰值内存使用: " << metrics.peakMemoryUsage / 1024 / 1024 << " MB" << std::endl;
    
    if (!operationStats.empty()) {
        std::cout << "\n操作耗时统计:" << std::endl;
        for (const auto& [operation, duration] : operationStats) {
            std::cout << "  " << operation << ": " 
                      << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() 
                      << " 毫秒" << std::endl;
        }
    }
    std::cout << std::endl;
}

void ConsoleCompatibility::configureFromArgs(const ConsoleInterface::PackagerArgs& args) {
    initializeLogging(args.verbose, false, "packager.log");
    
    LOG_INFOF("PackagerConfig", "输入路径: %s", args.inputPath.c_str());
    LOG_INFOF("PackagerConfig", "输出路径: %s", args.outputPath.c_str());
    
    // 直接显示压缩算法枚举值
    const char* algorithmName = "UNKNOWN";
    switch (args.algorithm) {
        case MultiThreadedInstaller::CompressionAlgorithm::ZSTD_FAST: 
            algorithmName = "ZSTD_FAST"; 
            break;
        case MultiThreadedInstaller::CompressionAlgorithm::LZMA_HIGH: 
            algorithmName = "LZMA_HIGH"; 
            break;
    }
    LOG_INFOF("PackagerConfig", "压缩算法: %s", algorithmName);
    
    if (args.compressionLevel != -1) {
        LOG_INFOF("PackagerConfig", "压缩级别: %d", args.compressionLevel);
    }
    
    if (args.threadCount != -1) {
        LOG_INFOF("PackagerConfig", "线程数: %d", args.threadCount);
    }
}

void ConsoleCompatibility::configureFromArgs(const ConsoleInterface::InstallerArgs& args) {
    initializeLogging(args.verbose, args.silent, "installer.log");
    
    if (!args.defaultDestination.empty()) {
        LOG_INFOF("InstallerConfig", "默认目标路径: %s", args.defaultDestination.c_str());
    }
    
    if (args.force) {
        LOG_INFO("InstallerConfig", "启用强制覆盖模式");
    }
    
    if (args.threadCount != -1) {
        LOG_INFOF("InstallerConfig", "线程数: %d", args.threadCount);
    }
    
    if (!args.folderMappings.empty()) {
        LOG_INFO("InstallerConfig", "文件夹映射:");
        for (const auto& [folder, path] : args.folderMappings) {
            LOG_INFOF("InstallerConfig", "  %s -> %s", folder.c_str(), path.c_str());
        }
    }
}

// LoggingHelper 实现
std::unique_ptr<ConsoleCompatibility> LoggingHelper::instance_;

void LoggingHelper::initialize(bool verbose, bool silent, const std::string& logFile) {
    if (!instance_) {
        instance_ = std::make_unique<ConsoleCompatibility>();
    }
    instance_->initializeLogging(verbose, silent, logFile);
}

void LoggingHelper::showProgress(const std::string& operation, const std::string& item, float progress) {
    if (instance_) {
        // 这里可以根据操作类型选择合适的进度显示方法
        if (operation.find("打包") != std::string::npos || operation.find("Packaging") != std::string::npos) {
            instance_->showPackagingProgress(item, progress);
        } else if (operation.find("安装") != std::string::npos || operation.find("Installation") != std::string::npos) {
            instance_->showInstallationProgress(item, progress);
        } else {
            LOG_DEBUGF("Progress", "%s: %s (%.1f%%)", operation.c_str(), item.c_str(), progress * 100);
        }
    }
}

void LoggingHelper::showError(const std::string& message) {
    if (instance_) {
        instance_->showError(message);
    } else {
        std::cerr << "ERROR: " << message << std::endl;
    }
}

void LoggingHelper::showWarning(const std::string& message) {
    if (instance_) {
        instance_->showWarning(message);
    } else {
        std::cout << "WARNING: " << message << std::endl;
    }
}

void LoggingHelper::showInfo(const std::string& message) {
    if (instance_) {
        instance_->showInfo(message);
    } else {
        std::cout << "INFO: " << message << std::endl;
    }
}

void LoggingHelper::generateReport() {
    if (instance_) {
        instance_->generatePerformanceReport();
    }
}

ConsoleCompatibility& LoggingHelper::getInstance() {
    if (!instance_) {
        instance_ = std::make_unique<ConsoleCompatibility>();
    }
    return *instance_;
}

} // namespace MultiThreadedInstaller