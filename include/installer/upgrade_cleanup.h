#pragma once

#include "common/archive_types.h"
#include "installer/console_interface.h"
#include "installer/path_resolver.h"

#include <functional>
#include <string>

namespace MultiThreadedInstaller {

struct UpgradeCleanupProgressInfo {
    float progress = 0.0f;
    std::string currentItem;
};

using UpgradeCleanupProgressCallback =
    std::function<void(const UpgradeCleanupProgressInfo&)>;

bool cleanupPreviousInstallForUpgrade(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const std::string& newInstallDir,
    CliSupport& console,
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {});

bool cleanupUpgradeSystemArtifacts(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const ExtendedInstallationMetadata& metadata,
    InstallerPathResolver& resolver,
    CliSupport& console,
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {});

} // namespace MultiThreadedInstaller
