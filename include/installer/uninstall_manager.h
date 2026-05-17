#pragma once

#include "installer/console_interface.h"
#include "installer/path_resolver.h"
#include "common/archive_types.h"

#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

struct UninstallProgressInfo {
    float progress = 0.0f;
    std::string currentItem;
};

using UninstallProgressCallback = std::function<void(const UninstallProgressInfo&)>;

struct UninstallContext {
    std::string manifestPath;
    std::string installDir;
    std::string appId;
    std::string appName;
    UninstallerCleanupConfigV3 embeddedUninstallerCleanup;
    bool hasEmbeddedUninstallerCleanup = false;
    std::vector<std::string> embeddedKillBeforeUninstall;
    bool manifestValidV3 = false;
    bool manifestReadable = false;
    bool fallbackAllowed = false;
    std::string fallbackPolicy;
    std::string detectSource;
    std::string errorMessage;
};

bool ResolveUninstallContext(const ExtendedInstallationMetadata* metadata,
                             InstallerPathResolver& resolver,
                             const std::string& explicitManifestPath,
                             UninstallContext& context);
bool ExecuteUninstallFromContext(const UninstallContext& context,
                                 const ExtendedInstallationMetadata* metadata,
                                 InstallerPathResolver& resolver,
                                 CliSupport& console,
                                 const UninstallProgressCallback& progressCallback = {},
                                 const std::function<bool()>& cancellationCallback = {});
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console);
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console,
                           const UninstallProgressCallback& progressCallback,
                           const std::function<bool()>& cancellationCallback = {});
bool uninstallFromManifest(const std::string& manifestPath,
                           const UninstallerCleanupConfigV3* embeddedCleanup,
                           const std::vector<std::string>* embeddedKillBeforeUninstall,
                           InstallerPathResolver& resolver,
                           CliSupport& console,
                           const UninstallProgressCallback& progressCallback,
                           const std::function<bool()>& cancellationCallback = {});

} // namespace MultiThreadedInstaller
