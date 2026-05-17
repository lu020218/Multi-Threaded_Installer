#pragma once

#include "common/config_types.h"
#include "installer/path_resolver.h"

#include <string>

namespace MultiThreadedInstaller {

struct InstallStateContext {
    std::string installDir;
    std::string version;
    std::string appName;
    std::string appId;
    std::string installSource;
    std::string state;
    std::string userName;
};

bool ApplyInstallState(const InstallStateConfig& config,
                       const InstallStateContext& context,
                       InstallerPathResolver& resolver);

bool CleanupInstallState(const InstallStateConfig& config,
                         const std::string& mode,
                         const InstallStateContext& context,
                         InstallerPathResolver& resolver);

std::string ExpandInstallStateTokenValue(const std::string& value,
                                         const InstallStateContext& context,
                                         InstallerPathResolver& resolver);

std::string GetCurrentUserNameForInstallState();

} // namespace MultiThreadedInstaller
