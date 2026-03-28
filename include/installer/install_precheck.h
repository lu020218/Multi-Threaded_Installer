#pragma once

#include "common/archive_types.h"
#include "installer/install_plan_builder.h"
#include "installer/install_progress_reporter.h"
#include "installer/install_service.h"

#include <Windows.h>

#include <string>

namespace MultiThreadedInstaller {

class InstallerPathResolver;

bool ExecuteInstallPrecheck(const ExtendedInstallationMetadata& metadata,
                            const InstallExecutionPlan& plan,
                            const InstallServiceOptions& options,
                            InstallProgressReporter& reporter,
                            HANDLE& installMutex,
                            InstallerPathResolver& pathResolver,
                            std::string& error,
                            bool& cancelled);

} // namespace MultiThreadedInstaller
