#pragma once

#include "common/archive_types.h"
#include "installer/app/console_interface.h"
#include "installer/platform/path_resolver.h"
#include "installer/state/uninstall_record.h"

#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/// 升级清理进度信息。
struct UpgradeCleanupProgressInfo {
    float progress = 0.0f;     ///< 进度 [0..1]。
    std::string currentItem;   ///< 当前处理项。
};

using UpgradeCleanupProgressCallback =
    std::function<void(const UpgradeCleanupProgressInfo&)>;  ///< 升级清理进度回调。

/// 升级清理的看门狗/并发策略（防止删大量文件时卡死、并控制并发与日志阈值）。
struct UpgradeCleanupPolicy {
    uint32_t itemStaleTimeoutMs = 30000;   ///< 单项心跳停滞超时（毫秒），超时判为卡住。
    uint32_t totalTimeoutMs = 120000;      ///< 整次清理总超时（毫秒）。
    uint32_t heartbeatIntervalMs = 1000;   ///< 心跳写入间隔（毫秒）。
    uint32_t heartbeatEveryItems = 100;    ///< 每处理多少项写一次心跳。
    uint32_t slowItemLogMs = 3000;         ///< 单项耗时超过此值记一条慢删日志。
    uint32_t workerConcurrency = 0;        ///< 并发删除线程数；0 = 按策略自动。
    bool allowPartialSuccess = true;       ///< 是否容忍部分失败（仍视为成功，记警告）。
};

/// 升级清理结果。
struct UpgradeCleanupResult {
    bool success = true;          ///< 是否成功（受 allowPartialSuccess 影响）。
    bool partial = false;         ///< 是否部分完成（有失败/跳过）。
    bool timedOut = false;        ///< 是否触发看门狗超时。
    uint64_t deletedCount = 0;    ///< 已删除数。
    uint64_t failedCount = 0;     ///< 失败数。
    uint64_t skippedCount = 0;    ///< 跳过数。
    std::string timedOutPath;     ///< 超时时正在处理的路径。
    std::string message;          ///< 结果说明。
};

/// 带看门狗的旧安装清理：在独立线程执行并由看门狗监控超时/心跳，避免锁定文件拖死流程。
/// @param keepFiles 新包中也存在的文件绝对路径集合；非空时做差集清理——集合内文件保留原位
///        （供解压器跳过或覆盖），且关闭整子树隔离，避免未变文件被移走。
UpgradeCleanupResult runPreviousInstallCleanupWithWatchdog(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const std::string& newInstallDir,
    const std::vector<std::string>& replacementTargets = {},
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {},
    const UpgradeCleanupPolicy& policy = {},
    const std::vector<std::string>& keepFiles = {});

/// 带看门狗地清理一组额外路径规则（升级残留路径）。
UpgradeCleanupResult runUpgradeExtraPathCleanupWithWatchdog(
    const std::vector<UninstallCleanupRule>& rules,
    const std::string& previousInstallDir,
    InstallerPathResolver& resolver,
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {},
    const UpgradeCleanupPolicy& policy = {});

/// 升级系统级收尾：重构后改为调用引擎迁移表（migration::RunPending）承接旧名单清理
/// （旧注册表/快捷方式/卸载入口/路径残留），而非读 YAML legacy 名单。
/// fromVersionHint：调用方在计划期抓取的旧版本号快照（注册表优先）。非空时直接作为迁移
/// fromVersion（此时旧 manifest 往往已被文件清理删除，注册表也可能已被覆盖，快照是唯一可靠来源）；
/// 为空时按 注册表 Version → 旧 manifest appVersion 的优先级现场读取。
bool cleanupUpgradeSystemArtifacts(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const PackageManifest& metadata,
    InstallerPathResolver& resolver,
    CliSupport& console,
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {},
    bool cleanupExtraPaths = true,
    const std::string& fromVersionHint = {});

} // namespace MultiThreadedInstaller
