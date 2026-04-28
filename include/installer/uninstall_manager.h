#pragma once

#include "common/config_types.h"
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
                   const std::string& appId,
                   const std::string& displayName,
                   const std::string& configVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& cleanupRoots,
                   const UninstallCleanupConfig& uninstallCleanup,
                   const std::vector<std::string>& filePaths,
                   const std::vector<RegistryEntry>& registry,
                   const std::vector<std::string>& installKillProcesses,
                   bool autoStartup,
                   bool desktopIcons,
                   const std::string& desktopShortcutDisplayName,
                   const InstallInfoConfig& installInfo,
                   const std::string& uninstallPath,
                   const std::string& languageCode,
                   const std::vector<ComponentExecutionRecord>& componentActions = {});
bool readManifest(const std::string& manifestPath, nlohmann::json& outManifest);
bool resolveExistingInstallInfo(const std::vector<std::string>& identityCandidates,
                                InstallerPathResolver& resolver,
                                std::string& manifestPath,
                                std::string& installDir,
                                std::string* matchedIdentity = nullptr);
bool resolveInstallInfoFromRegistry(const std::string& registryPath,
                                    const std::string& registryKey,
                                    std::string& manifestPath,
                                    std::string& installDir);
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console);
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console,
                           const UninstallProgressCallback& progressCallback,
                           const std::function<bool()>& cancellationCallback = {});
bool scheduleSelfDelete();
bool scheduleSelfDeleteImmediate(const std::vector<std::string>& cleanupRoots,
                                 const std::string& manifestPath);
bool cleanupEmptyDirectoriesCmd(const std::string& root);

} // namespace MultiThreadedInstaller
