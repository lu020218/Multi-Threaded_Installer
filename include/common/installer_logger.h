#pragma once

#include <string>

namespace MultiThreadedInstaller {

/// 日志级别。
enum class InstallerLogLevel {
    Info,     ///< 一般流程信息。
    Warning,  ///< 可继续的异常（best-effort 失败等）。
    Error,    ///< 导致失败/中止的错误。
    Debug,    ///< 排查用细节，正常运行可忽略。
};

/// 初始化日志子系统（确定日志文件路径、打开文件、装好 sink）。进程启动早期调用一次。
void initializeInstallerLogging();

/// 将缓冲中的日志刷盘。退出前或关键节点调用，确保崩溃前的日志不丢。
void flushInstallerLogging();

/// 返回当前日志文件的完整路径（如 %LocalAppData%\MTInstaller\MTInstaller_<产品>_<时间>.log）。
std::string getInstallerLogPath();

/// 按指定级别写一条日志。约定消息带 `[模块]` 前缀以便过滤（如 "[Migration] ..."）。
void writeInstallerLog(InstallerLogLevel level, const std::string& message);

void logInstallerInfo(const std::string& message);     ///< 等价 writeInstallerLog(Info, ...)。
void logInstallerWarning(const std::string& message);  ///< 等价 writeInstallerLog(Warning, ...)。
void logInstallerError(const std::string& message);    ///< 等价 writeInstallerLog(Error, ...)。
void logInstallerDebug(const std::string& message);    ///< 等价 writeInstallerLog(Debug, ...)。

} // namespace MultiThreadedInstaller
