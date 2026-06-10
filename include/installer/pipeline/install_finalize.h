#pragma once

#include "common/archive_types.h"
#include "installer/pipeline/install_plan_builder.h"
#include "installer/state/install_manifest_store.h"
#include "installer/pipeline/install_progress_reporter.h"
#include "installer/pipeline/install_service.h"

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class InstallerPathResolver;

/// 安装收尾阶段：写注册表 → 建快捷方式/开机自启 → 释放 uninstall.exe → 写系统卸载入口 →
/// 写 install.manifest.json（含运行时卸载账本 + 文件指纹）→ 写 install-state（"installed"）。
/// 系统卸载入口/清理规则均按引擎写死处理，不再来自 YAML。
/// @param effectiveRegistry      额外注册表写入（重构后通常为空，产品注册表由引擎在别处写）。
/// @param effectiveKillProcesses 卸载时需结束的进程名（由产品名派生）。
/// @param effectiveAutoStartup   是否设开机自启（已合并 options 覆盖/EngineDefaults）。
/// @param effectiveDesktopIcons  是否建桌面/开始菜单快捷方式。
/// @param result                 入参带解压结果，出参补全 uninstallPath/installedFiles 等。
/// @return 成功返回 true。
bool ExecuteInstallFinalization(const ExtendedInstallationMetadata& metadata,
                                const InstallExecutionPlan& plan,
                                const InstallServiceOptions& options,
                                const std::vector<RegistryEntry>& effectiveRegistry,
                                const std::vector<std::string>& effectiveKillProcesses,
                                bool effectiveAutoStartup,
                                bool effectiveDesktopIcons,
                                InstallerPathResolver& pathResolver,
                                InstallProgressReporter& reporter,
                                InstallServiceResult& result);

} // namespace MultiThreadedInstaller
