#pragma once

#include "common/installer_parallel_install.h"
#include "common/archive_types.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace MultiThreadedInstaller {

class MetadataParser;
class InstallerPathResolver;

/// 安装事件类型（通过回调上报给 GUI / CLI）。
enum class InstallServiceEventType {
    Status,    ///< 阶段状态变更。
    Progress,  ///< 进度更新。
    Info,      ///< 信息日志。
    Warning,   ///< 警告日志。
    Error,     ///< 错误日志。
};

/// 安装整体状态。
enum class InstallServiceStatus {
    Preparing,        ///< 准备中。
    Precheck,         ///< 预检（磁盘/系统版本/进程/互斥量）。
    Installing,       ///< 解压安装中。
    Finalizing,       ///< 收尾（注册表/快捷方式/卸载入口/manifest）。
    Completed,        ///< 完成。
    RebootRequired,   ///< 完成但需重启替换锁定文件。
    Failed,           ///< 失败。
    Cancelled,        ///< 已取消。
};

/// 安装阶段（用于进度归属）。
enum class InstallServicePhase {
    None,
    Precheck,           ///< 预检。
    CleanupOldInstall,  ///< 清理旧安装（升级/覆盖）。
    PreInstallHook,     ///< 安装前脚本。
    Installing,         ///< 解压。
    Components,         ///< 组件安装。
    Finalizing,         ///< 收尾。
    PostInstallHook,    ///< 安装后脚本。
};

/// 单条安装事件。
struct InstallServiceEvent {
    InstallServiceEventType type = InstallServiceEventType::Status;
    InstallServiceStatus status = InstallServiceStatus::Preparing;
    InstallServicePhase phase = InstallServicePhase::None;
    std::string message;       ///< 文案（Info/Warning/Error 用）。
    std::string folder;        ///< 当前 folder（Progress 用）。
    std::string currentFile;   ///< 当前文件（Progress 用）。
    float progress = 0.0f;         ///< 文件级进度 [0..1]。
    float phaseProgress = 0.0f;    ///< 当前阶段进度 [0..1]。
    float overallProgress = 0.0f;  ///< 总体进度 [0..1]。
};

/// 安装事件回调。
using InstallServiceEventCallback = std::function<void(const InstallServiceEvent&)>;

/// 安装选项（由 GUI/CLI 构造后传入 ExecuteInstallService）。
struct InstallServiceOptions {
    std::string installPath;             ///< 安装根路径。
    bool installPathExplicit = false;    ///< installPath 是否为用户显式指定。
    std::vector<std::pair<std::string, std::string>> folderMappings;  ///< 逐 folder 目标覆盖（folderId→target），通常为空。
    bool upgradeMode = false;            ///< 升级模式（沿用旧安装目录 + 触发迁移）。
    int threadCount = 0;                 ///< 解压线程数；0 = 按 CPU 自动。
    std::string languageCode;            ///< 界面/快捷方式语言。
    bool applyRegistryAfterInstall = true;     ///< 是否在安装后写注册表。
    bool writeUninstallRegistry = false;       ///< 是否写系统卸载入口。
    bool overrideAutoStartup = false;    ///< 是否覆盖开机自启默认值。
    bool autoStartupEnabled = false;     ///< 覆盖值：开机自启。
    bool overrideDesktopIcons = false;   ///< 是否覆盖桌面快捷方式默认值。
    bool desktopIconsEnabled = false;    ///< 覆盖值：桌面快捷方式。
    /// 选中的组件 id（GUI 勾选 / 静默 CLI 解析得到）。引擎据此 + 注册表 required 决定装哪些组件；
    /// 注册表里有、但不在此集合且非 required 的组件跳过。空集合 = 仅装 required 组件。
    std::vector<std::string> selectedComponentIds;
    std::function<bool()> cancellationCallback;  ///< 取消查询回调（返回 true 表示请求取消）。
};

/// 安装回调集合。
struct InstallServiceCallbacks {
    InstallServiceEventCallback onEvent;  ///< 事件回调（进度/状态/日志）。
};

/// 安装结果。
struct InstallServiceResult {
    bool success = false;                          ///< 是否成功。
    bool cancelled = false;                        ///< 是否被取消。
    bool rebootRequired = false;                   ///< 是否需重启替换锁定文件。
    std::string installRootPath;                   ///< 实际安装根。
    std::vector<std::string> installedRoots;       ///< 各 folder 目标根。
    std::vector<std::string> installedFiles;       ///< 已写入文件绝对路径清单。
    std::string uninstallPath;                     ///< 释放出的 uninstall.exe 路径。
    std::vector<std::string> pendingReplaceFiles;  ///< 待重启替换的锁定文件。
    std::vector<std::string> installedComponentIds; ///< 实际运行了安装程序的组件 id（写入 manifest，供卸载反向执行）。
    std::vector<std::string> errors;               ///< 失败信息。
    ParallelInstallSummary timing;                 ///< 计时汇总。
};

/// 安装主流程编排入口：预检 → 清理旧装 → 写"installing"状态 → preInstall hook →
/// 解压 → finalize（注册表/快捷方式/卸载入口/manifest）→ postInstall hook。
/// 任一致命失败走 markFailed（postInstall abort 还会回滚已装产物）。全程通过 callbacks 上报。
InstallServiceResult ExecuteInstallService(const PackageManifest& metadata,
                                           MetadataParser& parser,
                                           InstallerPathResolver& pathResolver,
                                           const InstallServiceOptions& options,
                                           const InstallServiceCallbacks& callbacks);

} // namespace MultiThreadedInstaller
