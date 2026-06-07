#pragma once

#include "common/archive_types.h"
#include "installer/app/console_interface.h"
#include "installer/platform/path_resolver.h"
#include "installer/state/uninstall_record.h"

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
    const UpgradeCleanupPolicy& policy = {},
    // Absolute paths of files present in the new package. When non-empty the
    // cleanup performs a difference-set deletion: files in this set are kept
    // in place (so the extractor can skip or overwrite them), and whole-subtree
    // isolation is disabled so unchanged files are not moved away.
    const std::vector<std::string>& keepFiles = {});

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
