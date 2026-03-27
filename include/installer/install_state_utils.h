#pragma once

#include "common/config_types.h"
#include "installer/path_resolver.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

bool applyInstallStateRegistry(const InstallStateConfig& config, const std::string& stateValue);
bool applyInstallStateFile(const InstallStateConfig& config, const std::string& stateValue,
                           InstallerPathResolver& resolver);
HANDLE acquireInstallMutex(const InstallStateConfig& config);
void releaseInstallMutex(HANDLE handle);
void applyInstallState(const InstallStateConfig& config, const std::string& stateValue,
                       InstallerPathResolver& resolver);
bool removeInstallStateArtifacts(const InstallStateConfig& config, InstallerPathResolver& resolver);

} // namespace MultiThreadedInstaller
