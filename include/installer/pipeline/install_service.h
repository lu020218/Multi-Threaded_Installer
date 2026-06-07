#pragma once

#include "common/installer_parallel_install.h"
#include "common/archive_types.h"

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
    RebootRequired,
    Failed,
    Cancelled,
};

enum class InstallServicePhase {
    None,
    Precheck,
    CleanupOldInstall,
    Installing,
    Finalizing,
};

struct InstallServiceEvent {
    InstallServiceEventType type = InstallServiceEventType::Status;
    InstallServiceStatus status = InstallServiceStatus::Preparing;
    InstallServicePhase phase = InstallServicePhase::None;
    std::string message;
    std::string folder;
    std::string currentFile;
    float progress = 0.0f;
    float phaseProgress = 0.0f;
    float overallProgress = 0.0f;
};

using InstallServiceEventCallback = std::function<void(const InstallServiceEvent&)>;

struct InstallServiceOptions {
    std::string installPath;
    bool installPathExplicit = false;
    std::vector<std::pair<std::string, std::string>> folderMappings;
    std::vector<std::string> selectedComponentIds;
    bool installAllComponents = false;
    bool upgradeMode = false;
    int threadCount = 0;
    std::string languageCode;
    bool applyRegistryBeforeFinalize = false;
    std::string preRegistryInstallPath;
    bool applyRegistryAfterInstall = true;
    bool writeUninstallRegistry = false;
    bool overrideAutoStartup = false;
    bool autoStartupEnabled = false;
    bool overrideDesktopIcons = false;
    bool desktopIconsEnabled = false;
    std::function<bool()> cancellationCallback;
};

struct InstallServiceCallbacks {
    InstallServiceEventCallback onEvent;
};

struct InstallServiceResult {
    bool success = false;
    bool cancelled = false;
    bool rebootRequired = false;
    std::string installRootPath;
    std::vector<std::string> installedRoots;
    std::vector<std::string> installedFiles;
    std::string uninstallPath;
    std::vector<std::string> pendingReplaceFiles;
    std::vector<std::string> errors;
    ParallelInstallSummary timing;
};

InstallServiceResult ExecuteInstallService(const ExtendedInstallationMetadata& metadata,
                                           MetadataParser& parser,
                                           InstallerPathResolver& pathResolver,
                                           const InstallServiceOptions& options,
                                           const InstallServiceCallbacks& callbacks);

} // namespace MultiThreadedInstaller
