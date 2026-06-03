#pragma once

#include "common/archive_types.h"
#include "common/installer_parallel_install.h"
#include "installer/install_manifest_store.h"
#include "installer/install_plan_builder.h"
#include "installer/install_progress_reporter.h"
#include "installer/install_service.h"

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class MetadataParser;
class InstallerPathResolver;

struct ComponentInstallTiming {
    std::string id;
    std::string name;
    std::string type;
    bool success = false;
    uint64_t totalMs = 0;
    uint64_t downloadMs = 0;
    uint64_t installMs = 0;
    std::string error;
};

struct InstallExecutionOutput {
    bool success = false;
    bool cancelled = false;
    bool rebootRequired = false;
    std::string installRootPath;
    std::vector<std::string> installedRoots;
    std::vector<std::string> installedFiles;
    std::vector<std::string> pendingReplaceFiles;
    std::vector<std::string> errors;
    ParallelInstallSummary timing;
    std::vector<ComponentInstallTiming> componentTimings;
    std::vector<ComponentExecutionRecord> componentActions;
    std::vector<RegistryEntry> effectiveRegistry;
    std::vector<std::string> effectiveKillProcesses;
    bool effectiveAutoStartup = false;
    bool effectiveDesktopIcons = false;
    std::vector<std::string> failedOptionalComponentMessages;
};

bool ExecuteInstallExecution(const ExtendedInstallationMetadata& metadata,
                             MetadataParser& parser,
                             const InstallExecutionPlan& plan,
                             const InstallServiceOptions& options,
                             InstallerPathResolver& pathResolver,
                             InstallProgressReporter& reporter,
                             InstallExecutionOutput& output);

} // namespace MultiThreadedInstaller
