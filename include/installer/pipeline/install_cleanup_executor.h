#pragma once

#include "common/archive_types.h"
#include "installer/pipeline/install_plan_builder.h"
#include "installer/pipeline/install_progress_reporter.h"
#include "installer/pipeline/install_service.h"

#include <string>

namespace MultiThreadedInstaller {

class InstallerPathResolver;

/// 升级/覆盖安装前的旧安装清理阶段：按上次 manifest 做差集清理旧文件（缺失时回退安全目录清理），
/// 并触发跨版本迁移收尾（cleanupUpgradeSystemArtifacts → migration::RunPending）。
/// 仅在检出旧安装时有实质动作；全新安装基本为空操作。
/// @param error/cancelled 出参：失败原因 / 是否被取消。
bool ExecuteInstallCleanup(const PackageManifest& metadata,
                           const InstallExecutionPlan& plan,
                           const InstallServiceOptions& options,
                           InstallerPathResolver& pathResolver,
                           InstallProgressReporter& reporter,
                           std::string& error,
                           bool& cancelled);

} // namespace MultiThreadedInstaller
