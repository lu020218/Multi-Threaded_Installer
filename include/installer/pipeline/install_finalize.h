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
/// 注：killProcesses/autoStartup/desktopIcon 直接读 plan.effective*；
/// 额外注册表写入项由本函数内写死（BuildPostInstallRegistryEntries），不再外部透传。
/// @param result 入参带解压结果，出参补全 uninstallPath/installedFiles 等。
/// @return 成功返回 true。
bool ExecuteInstallFinalization(const PackageManifest& metadata,
                                const InstallExecutionPlan& plan,
                                const InstallServiceOptions& options,
                                InstallerPathResolver& pathResolver,
                                InstallProgressReporter& reporter,
                                InstallServiceResult& result);

} // namespace MultiThreadedInstaller
