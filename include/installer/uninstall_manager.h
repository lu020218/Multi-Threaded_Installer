#pragma once

#include "common/types.h"
#include "installer/console_interface.h"
#include "installer/path_resolver.h"
#include <json.hpp>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

bool writeManifest(const std::string& manifestPath,
                   const std::string& appName,
                   const std::string& configVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& filePaths,
                   const std::vector<RegistryEntry>& registry,
                   bool autoStartup,
                   bool desktopIcons,
                   const InstallStateConfig& installState,
                   const std::string& uninstallPath);
bool readManifest(const std::string& manifestPath, nlohmann::json& outManifest);
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           ConsoleInterface& console);
bool scheduleSelfDelete();
bool scheduleSelfDeleteImmediate(const std::vector<std::string>& cleanupRoots,
                                 const std::string& manifestPath);
bool cleanupEmptyDirectoriesCmd(const std::string& root);

} // namespace MultiThreadedInstaller
