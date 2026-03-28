#pragma once

#include "common/archive_types.h"
#include "installer/install_plan_builder.h"
#include "installer/install_progress_reporter.h"
#include "installer/install_service.h"

#include <string>

namespace MultiThreadedInstaller {

class InstallerPathResolver;

bool ExecuteInstallCleanup(const ExtendedInstallationMetadata& metadata,
                           const InstallExecutionPlan& plan,
                           const InstallServiceOptions& options,
                           InstallerPathResolver& pathResolver,
                           InstallProgressReporter& reporter,
                           std::string& error,
                           bool& cancelled);

} // namespace MultiThreadedInstaller
