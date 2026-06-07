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

bool ExecuteInstallFinalization(const ExtendedInstallationMetadata& metadata,
                                const InstallExecutionPlan& plan,
                                const InstallServiceOptions& options,
                                const std::vector<RegistryEntry>& effectiveRegistry,
                                const std::vector<std::string>& effectiveKillProcesses,
                                bool effectiveAutoStartup,
                                bool effectiveDesktopIcons,
                                const std::vector<ComponentExecutionRecord>& componentActions,
                                InstallerPathResolver& pathResolver,
                                InstallProgressReporter& reporter,
                                InstallServiceResult& result);

} // namespace MultiThreadedInstaller
