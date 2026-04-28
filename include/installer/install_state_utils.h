#pragma once

#include "common/config_types.h"
#include "installer/path_resolver.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <string>

namespace MultiThreadedInstaller {

bool applyCoreInstallInfo(const InstallInfoConfig& config,
                          const std::string& installDir,
                          const std::string& version,
                          const std::string& appName,
                          const std::string& stateValue,
                          InstallerPathResolver& resolver);
HANDLE acquireInstallMutex(bool useMutex, const std::string& mutexName);
void releaseInstallMutex(HANDLE handle);
bool removeInstallInfoArtifacts(const InstallInfoConfig& config);

} // namespace MultiThreadedInstaller
