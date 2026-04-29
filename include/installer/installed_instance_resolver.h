#pragma once

#include "common/archive_types.h"

#include <string>

namespace MultiThreadedInstaller {

struct InstalledInstanceInfo {
    bool found = false;
    std::string installDir;
    std::string manifestPath;
    std::string installedVersion;
};

bool resolveInstalledInstanceFromInstallRoots(const ExtendedInstallationMetadata& metadata,
                                              InstalledInstanceInfo& instanceInfo);

bool resolveInstallInfoFromRegistry(const std::string& registryPath,
                                    const std::string& registryKey,
                                    std::string& manifestPath,
                                    std::string& installDir);

} // namespace MultiThreadedInstaller
