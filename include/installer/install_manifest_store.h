#pragma once

#include "common/config_types.h"

#include <json.hpp>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

struct ComponentExecutionRecord {
    std::string componentId;
    std::string sourceType;
    std::string uninstallCommand;
    std::string workingDirectory;
    bool wait = true;
    uint32_t timeoutSec = 900;
};

struct PreviousInstallOptions {
    bool autoStartup = false;
    bool desktopIcon = false;
    bool installAllComponents = false;
    std::string languageCode;
    std::vector<std::string> selectedComponentIds;
};

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
                   const std::vector<ComponentExecutionRecord>& componentActions = {},
                   const std::vector<std::string>& selectedComponentIds = {},
                   bool installAllComponents = false);
bool readManifest(const std::string& manifestPath, nlohmann::json& outManifest);
bool loadPreviousInstallOptions(const std::string& manifestPath,
                                PreviousInstallOptions& options,
                                std::string& error);

} // namespace MultiThreadedInstaller
