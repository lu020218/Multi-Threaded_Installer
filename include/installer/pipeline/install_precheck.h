#pragma once

#include "common/archive_types.h"
#include "installer/pipeline/install_plan_builder.h"
#include "installer/pipeline/install_progress_reporter.h"
#include "installer/pipeline/install_service.h"

#include <Windows.h>

#include <string>

namespace MultiThreadedInstaller {

class InstallerPathResolver;

/// 安装预检阶段：依次检查磁盘空间、最低 Windows 版本、结束占用进程，最后获取安装互斥量。
/// 各阈值/开关取自 EngineDefaults（写死），不再来自 YAML。
/// @param installMutex 出参：获取到的安装互斥量句柄（安装结束由调用方释放）。
/// @param error        出参：失败原因。
/// @param cancelled    出参：是否因用户取消而中止。
/// @return 全部通过返回 true；否则 false（error/cancelled 指明原因）。
bool ExecuteInstallPrecheck(const ExtendedInstallationMetadata& metadata,
                            const InstallExecutionPlan& plan,
                            const InstallServiceOptions& options,
                            InstallProgressReporter& reporter,
                            HANDLE& installMutex,
                            InstallerPathResolver& pathResolver,
                            std::string& error,
                            bool& cancelled);

} // namespace MultiThreadedInstaller
