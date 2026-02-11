#pragma once

#include "common/installer_parallel_install.h"
#include "common/types.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace MultiThreadedInstaller {

class MetadataParser;
class InstallerPathResolver;

enum class InstallServiceEventType {
    Status,
    Progress,
    Info,
    Warning,
    Error,
};

enum class InstallServiceStatus {
    Preparing,
    Precheck,
    Installing,
    Finalizing,
    Completed,
    Failed,
    Cancelled,
};

struct InstallServiceEvent {
    InstallServiceEventType type = InstallServiceEventType::Status;
    InstallServiceStatus status = InstallServiceStatus::Preparing;
    std::string message;
    std::string folder;
    std::string currentFile;
    float progress = 0.0f;
};

using InstallServiceEventCallback = std::function<void(const InstallServiceEvent&)>;

struct InstallServiceOptions {
    std::string installPath;
    std::vector<std::pair<std::string, std::string>> folderMappings;
    int threadCount = 0;
    std::string languageCode;
    bool applyRegistryBeforeFinalize = false;
    std::string preRegistryInstallPath;
    bool applyRegistryAfterInstall = true;
    bool writeUninstallRegistry = false;
    bool cleanupOldInstallRequested = false;
    std::function<bool()> cancellationCallback;
};

struct InstallServiceCallbacks {
    InstallServiceEventCallback onEvent;
};

struct InstallServiceResult {
    bool success = false;
    bool cancelled = false;
    std::string installRootPath;
    std::vector<std::string> installedRoots;
    std::vector<std::string> installedFiles;
    std::string uninstallPath;
    std::vector<std::string> errors;
    ParallelInstallSummary timing;
};

InstallServiceResult ExecuteInstallService(const ExtendedInstallationMetadata& metadata,
                                           MetadataParser& parser,
                                           InstallerPathResolver& pathResolver,
                                           const InstallServiceOptions& options,
                                           const InstallServiceCallbacks& callbacks);

} // namespace MultiThreadedInstaller