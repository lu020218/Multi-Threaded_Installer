#pragma once

#include "installer/console_interface.h"

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
    ConsoleInterface& console,
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {});

} // namespace MultiThreadedInstaller
