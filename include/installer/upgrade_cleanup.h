#pragma once

#include "common/archive_types.h"
#include "installer/console_interface.h"
#include "installer/path_resolver.h"

#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

struct UpgradeCleanupProgressInfo {
    float progress = 0.0f;
    std::string currentItem;
};

using UpgradeCleanupProgressCallback =
    std::function<void(const UpgradeCleanupProgressInfo&)>;

struct UpgradeCleanupPolicy {
    uint32_t itemStaleTimeoutMs = 30000;
    uint32_t totalTimeoutMs = 120000;
    uint32_t heartbeatIntervalMs = 1000;
    uint32_t heartbeatEveryItems = 100;
    uint32_t slowItemLogMs = 3000;
    uint32_t workerConcurrency = 0;
    bool allowPartialSuccess = true;
};

struct UpgradeCleanupResult {
    bool success = true;
    bool partial = false;
    bool timedOut = false;
    uint64_t deletedCount = 0;
    uint64_t failedCount = 0;
    uint64_t skippedCount = 0;
    std::string timedOutPath;
    std::string message;
};

bool cleanupPreviousInstallForUpgrade(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const std::string& newInstallDir,
    CliSupport& console,
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {});

UpgradeCleanupResult runPreviousInstallCleanupWithWatchdog(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const std::string& newInstallDir,
    const std::vector<std::string>& replacementTargets = {},
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {},
    const UpgradeCleanupPolicy& policy = {});

UpgradeCleanupResult runUpgradeExtraPathCleanupWithWatchdog(
    const std::vector<UninstallCleanupRule>& rules,
    const std::string& previousInstallDir,
    InstallerPathResolver& resolver,
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {},
    const UpgradeCleanupPolicy& policy = {});

bool cleanupUpgradeSystemArtifacts(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const ExtendedInstallationMetadata& metadata,
    InstallerPathResolver& resolver,
    CliSupport& console,
    const UpgradeCleanupProgressCallback& progressCallback = {},
    const std::function<bool()>& cancellationCallback = {},
    bool cleanupExtraPaths = true);

} // namespace MultiThreadedInstaller
