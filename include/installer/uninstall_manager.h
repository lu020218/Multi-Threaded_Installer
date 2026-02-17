#pragma once

#include "common/types.h"
#include "installer/console_interface.h"
#include "installer/path_resolver.h"

#include <functional>
#include <json.hpp>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

struct UninstallProgressInfo {
    float progress = 0.0f;
    std::string currentItem;
};

struct ComponentExecutionRecord {
    std::string componentId;
    std::string sourceType;
    std::string uninstallCommand;
    std::string workingDirectory;
    bool wait = true;
    uint32_t timeoutSec = 900;
};

using UninstallProgressCallback = std::function<void(const UninstallProgressInfo&)>;

bool writeManifest(const std::string& manifestPath,
                   const std::string& appName,
                   const std::string& configVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& filePaths,
                   const std::vector<RegistryEntry>& registry,
                   const std::vector<std::string>& installKillProcesses,
                   bool autoStartup,
                   bool desktopIcons,
                   const InstallStateConfig& installState,
                   const std::string& uninstallPath,
                   const std::string& languageCode,
                   const std::vector<ComponentExecutionRecord>& componentActions = {});
bool readManifest(const std::string& manifestPath, nlohmann::json& outManifest);
bool resolveExistingInstallInfo(const std::string& appName,
                                InstallerPathResolver& resolver,
                                std::string& manifestPath,
                                std::string& installDir);
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           ConsoleInterface& console);
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           ConsoleInterface& console,
                           const UninstallProgressCallback& progressCallback,
                           const std::function<bool()>& cancellationCallback = {});
bool scheduleSelfDelete();
bool scheduleSelfDeleteImmediate(const std::vector<std::string>& cleanupRoots,
                                 const std::string& manifestPath);
bool cleanupEmptyDirectoriesCmd(const std::string& root);

} // namespace MultiThreadedInstaller
