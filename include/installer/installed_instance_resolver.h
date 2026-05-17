#pragma once

#include "common/archive_types.h"

#include <string>

namespace MultiThreadedInstaller {

class InstallerPathResolver;

struct InstalledInstanceInfo {
    bool found = false;
    std::string installDir;
    std::string manifestPath;
    std::string installedVersion;
    std::string detectSource;
};

bool resolveInstalledInstanceFromInstallRoots(const ExtendedInstallationMetadata& metadata,
                                              InstalledInstanceInfo& instanceInfo);

bool resolveInstalledInstanceFromInstallState(const ExtendedInstallationMetadata& metadata,
                                              InstallerPathResolver& resolver,
                                              InstalledInstanceInfo& instanceInfo,
                                              std::string* error = nullptr);

bool resolveInstallDirFromInstallStateStore(const ExtendedInstallationMetadata& metadata,
                                            InstallerPathResolver& resolver,
                                            std::string& installDir,
                                            std::string& manifestPath,
                                            std::string& detectSource,
                                            std::string& error);

bool resolveInstallInfoFromRegistry(const std::string& registryPath,
                                    const std::string& registryKey,
                                    std::string& manifestPath,
                                    std::string& installDir);

} // namespace MultiThreadedInstaller
