#pragma once

#include "common/archive_types.h"
#include "common/installer_parallel_install.h"
#include "installer/state/install_manifest_store.h"
#include "installer/pipeline/install_plan_builder.h"
#include "installer/pipeline/install_progress_reporter.h"
#include "installer/pipeline/install_service.h"

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class MetadataParser;
class InstallerPathResolver;

/// 解压执行阶段的产出（供 finalize 与上层 result 消费）。
/// 注：成功与否由 ExecuteInstallExecution 的 bool 返回值表达，不再单列 success 字段；
/// finalize 所需的 killProcesses/autoStartup/desktopIcon 等直接读 InstallExecutionPlan，
/// 注册表写入项由 finalize 内写死（不再透传），故此结构体只保留解压真实产出 + 计时。
struct InstallExecutionOutput {
    bool cancelled = false;                        ///< 是否被取消。
    bool rebootRequired = false;                   ///< 是否有锁定文件待重启替换。
    std::string installRootPath;                   ///< 实际安装根。
    std::vector<std::string> installedRoots;       ///< 各 folder 目标根。
    std::vector<std::string> installedFiles;       ///< 已写入文件绝对路径。
    std::vector<std::string> pendingReplaceFiles;  ///< 待重启替换的锁定文件。
    std::vector<std::string> errors;               ///< 失败信息。
    ParallelInstallSummary timing;                 ///< 计时汇总。
};

/// 解压执行阶段：把（全部）payload 解压落地到安装目录，填充 InstallExecutionOutput。
/// 单产品单载荷下不再有组件执行；原"额外动作"由 hooks 承担（在 install_service 层调度）。
bool ExecuteInstallExecution(const PackageManifest& metadata,
                             MetadataParser& parser,
                             const InstallExecutionPlan& plan,
                             const InstallServiceOptions& options,
                             InstallerPathResolver& pathResolver,
                             InstallProgressReporter& reporter,
                             InstallExecutionOutput& output);

} // namespace MultiThreadedInstaller
