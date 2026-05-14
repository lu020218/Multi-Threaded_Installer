#include "installer/install_cleanup_executor.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/console_interface.h"
#include "installer/installer_helpers.h"
#include "installer/upgrade_cleanup.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace MultiThreadedInstaller {

namespace {

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
}

std::vector<std::string> ResolveSelectedPayloadTargets(const ExtendedInstallationMetadata& metadata,
                                                       const InstallExecutionPlan& plan,
                                                       InstallerPathResolver& pathResolver) {
    std::unordered_set<std::string> selected(plan.selectedEmbeddedFolders.begin(),
                                             plan.selectedEmbeddedFolders.end());
    std::vector<std::string> targets;
    std::unordered_set<std::string> seen;
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        if (selected.find(mapping.folderId) == selected.end()) {
            continue;
        }
        std::string target = mapping.target.empty() ? mapping.targetPath : mapping.target;
        const std::string token = "%InstallDir%";
        size_t pos = 0;
        while ((pos = target.find(token, pos)) != std::string::npos) {
            target.replace(pos, token.size(), plan.pathDecision.resolvedInstallRoot);
            pos += plan.pathDecision.resolvedInstallRoot.size();
        }
        target = pathResolver.expandEnvironmentVariables(target);
        if (target.empty()) {
            continue;
        }
        const std::string normalized = normalizePathForCompare(target);
        if (normalized.empty()) {
            continue;
        }
        if (normalized == normalizePathForCompare(plan.pathDecision.resolvedInstallRoot) &&
            !mapping.fileIndex.empty()) {
            for (const auto& file : mapping.fileIndex) {
                if (file.relativePath.empty()) {
                    continue;
                }
                const std::filesystem::path candidate =
                    PathFromUtf8(target) / PathFromUtf8(file.relativePath);
                const std::string candidateKey = normalizePathForCompare(Utf8FromPath(candidate));
                if (!candidateKey.empty() && seen.insert(candidateKey).second) {
                    targets.push_back(Utf8FromPath(candidate));
                }
            }
            continue;
        }
        if (seen.insert(normalized).second) {
            targets.push_back(std::move(target));
        }
    }
    return targets;
}

} // namespace

bool ExecuteInstallCleanup(const ExtendedInstallationMetadata& metadata,
                           const InstallExecutionPlan& plan,
                           const InstallServiceOptions& options,
                           InstallerPathResolver& pathResolver,
                           InstallProgressReporter& reporter,
                           std::string& error,
                           bool& cancelled) {
    error.clear();
    cancelled = false;

    if (!plan.hasPreviousInstall) {
        return true;
    }

    reporter.EmitMessage(InstallServiceEventType::Info,
                         "Detected previous install at: " + plan.previousInstallDir);

    logInstallerInfo(std::string("[InstallFlow][Cleanup] start previousManifest=") +
                     plan.previousManifest + " previousInstallDir=" + plan.previousInstallDir +
                     " resolvedInstallRoot=" + plan.pathDecision.resolvedInstallRoot +
                     " targetInstallRoot=" + plan.pathDecision.cleanupTargetInstallRoot);
    reporter.EmitStatus(InstallServiceStatus::Precheck,
                        InstallServicePhase::CleanupOldInstall,
                        0.0f,
                        "Cleaning previous installation...");

    CliSupport console;
    auto cleanupProgress = [&](const UpgradeCleanupProgressInfo& info) {
        const std::string detail = info.currentItem.empty()
                                       ? std::string("Cleaning previous installation")
                                       : info.currentItem;
        reporter.EmitProgress("cleanup", detail, info.progress);
    };

    UpgradeCleanupResult previousCleanup = runPreviousInstallCleanupWithWatchdog(
        plan.previousManifest,
        plan.previousInstallDir,
        plan.pathDecision.resolvedInstallRoot,
        ResolveSelectedPayloadTargets(metadata, plan, pathResolver),
        cleanupProgress,
        options.cancellationCallback);
    if (!previousCleanup.success && IsCancellationRequested(options)) {
        cancelled = true;
        error = "Installation cancelled.";
        return false;
    }
    if (!previousCleanup.success || previousCleanup.partial) {
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Previous install cleanup completed with warnings.");
        logInstallerWarning("[InstallFlow][Cleanup] finished partial success=" +
                            std::string(previousCleanup.success ? "true" : "false") +
                            " timedOut=" + std::string(previousCleanup.timedOut ? "true" : "false") +
                            " deleted=" + std::to_string(previousCleanup.deletedCount) +
                            " failed=" + std::to_string(previousCleanup.failedCount) +
                            " skipped=" + std::to_string(previousCleanup.skippedCount) +
                            " timedOutPath=" + previousCleanup.timedOutPath);
    } else {
        logInstallerInfo("[InstallFlow][Cleanup] finished successfully deleted=" +
                         std::to_string(previousCleanup.deletedCount) +
                         " skipped=" + std::to_string(previousCleanup.skippedCount));
    }

    if (!cleanupUpgradeSystemArtifacts(plan.previousManifest,
                                      plan.previousInstallDir,
                                      metadata,
                                      pathResolver,
                                      console,
                                      cleanupProgress,
                                      options.cancellationCallback,
                                      false)) {
        if (IsCancellationRequested(options)) {
            cancelled = true;
            error = "Installation cancelled.";
            return false;
        }
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Previous install system cleanup reported failure.");
        logInstallerWarning("[InstallFlow][Cleanup] system cleanup finished with failure");
    } else {
        logInstallerInfo("[InstallFlow][Cleanup] system cleanup finished successfully");
    }

    if (!metadata.lifecycleUpgradeCleanup.extraPaths.empty()) {
        UpgradeCleanupResult extraPathCleanup = runUpgradeExtraPathCleanupWithWatchdog(
            metadata.lifecycleUpgradeCleanup.extraPaths,
            plan.previousInstallDir,
            pathResolver,
            cleanupProgress,
            options.cancellationCallback);
        if (!extraPathCleanup.success && IsCancellationRequested(options)) {
            cancelled = true;
            error = "Installation cancelled.";
            return false;
        }
        if (!extraPathCleanup.success || extraPathCleanup.partial) {
            reporter.EmitMessage(InstallServiceEventType::Warning,
                                 "Previous install extra path cleanup completed with warnings.");
            logInstallerWarning("[InstallFlow][Cleanup] extra paths partial success=" +
                                std::string(extraPathCleanup.success ? "true" : "false") +
                                " timedOut=" + std::string(extraPathCleanup.timedOut ? "true" : "false") +
                                " deleted=" + std::to_string(extraPathCleanup.deletedCount) +
                                " failed=" + std::to_string(extraPathCleanup.failedCount) +
                                " skipped=" + std::to_string(extraPathCleanup.skippedCount) +
                                " timedOutPath=" + extraPathCleanup.timedOutPath);
        } else {
            logInstallerInfo("[InstallFlow][Cleanup] extra paths cleanup finished successfully deleted=" +
                             std::to_string(extraPathCleanup.deletedCount));
        }
    }

    reporter.EmitProgress("cleanup", "Previous installation cleanup finished", 1.0f);
    return true;
}

} // namespace MultiThreadedInstaller
