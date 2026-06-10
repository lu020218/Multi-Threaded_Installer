#include "installer/pipeline/install_cleanup_executor.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/app/console_interface.h"
#include "installer/state/install_manifest_store.h"
#include "installer/platform/installer_helpers.h"
#include "installer/uninstall/upgrade_cleanup.h"

#include <algorithm>
#include <filesystem>
#include <json.hpp>
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

// Resolves the absolute on-disk path of every file in the selected payload
// folders. This is the new package's file set; cleanup keeps these in place so
// the extractor can skip unchanged files instead of them being deleted and
// rewritten. Folders without a per-file index contribute nothing (fail-safe:
// their files fall back to the normal delete-then-rewrite path).
std::vector<std::string> ResolveSelectedPayloadFileTargets(const ExtendedInstallationMetadata& metadata,
                                                           const InstallExecutionPlan& plan,
                                                           InstallerPathResolver& pathResolver) {
    std::unordered_set<std::string> selected(plan.selectedEmbeddedFolders.begin(),
                                             plan.selectedEmbeddedFolders.end());
    std::vector<std::string> files;
    std::unordered_set<std::string> seen;
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        if (selected.find(mapping.folderId) == selected.end()) {
            continue;
        }
        if (mapping.fileIndex.empty()) {
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
        for (const auto& file : mapping.fileIndex) {
            if (file.relativePath.empty()) {
                continue;
            }
            const std::filesystem::path candidate =
                PathFromUtf8(target) / PathFromUtf8(file.relativePath);
            const std::string candidateUtf8 = Utf8FromPath(candidate);
            const std::string key = normalizePathForCompare(candidateUtf8);
            if (!key.empty() && seen.insert(key).second) {
                files.push_back(candidateUtf8);
            }
        }
    }
    return files;
}

} // namespace

// [旧版本-清理] 总入口：发现存在旧版本（plan.hasPreviousInstall）时，先删旧文件，
// 再清理旧版本的系统痕迹（注册表/开机自启/快捷方式/系统卸载入口，经迁移表）。
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

    bool previousManifestReadable = false;
    if (!plan.previousManifest.empty()) {
        nlohmann::json previousManifestJson;
        previousManifestReadable = readManifest(plan.previousManifest, previousManifestJson);
    }

    const std::vector<std::string> keepFiles =
        ResolveSelectedPayloadFileTargets(metadata, plan, pathResolver);
    if (!keepFiles.empty()) {
        logInstallerInfo("[InstallFlow][Cleanup] incremental difference-set cleanup keepFiles=" +
                         std::to_string(keepFiles.size()));
    }

    UpgradeCleanupResult previousCleanup;
    // [旧版本-清理:文件] 按旧 manifest 的 files[] 删除旧版本文件（带 keepFiles 差异集跳过、
    // 看门狗超时保护）；缺失清单时的回退策略写死为安全目录清理（需求 §5）。
    if (!previousManifestReadable) {
        previousCleanup = runPreviousInstallCleanupWithWatchdog(
            plan.previousManifest,
            plan.previousInstallDir,
            plan.pathDecision.resolvedInstallRoot,
            ResolveSelectedPayloadTargets(metadata, plan, pathResolver),
            cleanupProgress,
            options.cancellationCallback,
            UpgradeCleanupPolicy{},
            keepFiles);
    } else {
        previousCleanup = runPreviousInstallCleanupWithWatchdog(
            plan.previousManifest,
            plan.previousInstallDir,
            plan.pathDecision.resolvedInstallRoot,
            ResolveSelectedPayloadTargets(metadata, plan, pathResolver),
            cleanupProgress,
            options.cancellationCallback,
            UpgradeCleanupPolicy{},
            keepFiles);
    }
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

    // [旧版本-清理:系统痕迹] 旧版本的注册表/开机自启/快捷方式/系统卸载入口/残留路径，
    // 统一交给迁移表（cleanupUpgradeSystemArtifacts → migration::RunPending）按版本收尾。
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

    // YAML 驱动的额外路径清理（installerCleanup.paths）已删除；跨版本残留路径
    // 收尾改由迁移表（cleanupUpgradeSystemArtifacts → migration::RunPending）承接。

    reporter.EmitProgress("cleanup", "Previous installation cleanup finished", 1.0f);
    return true;
}

} // namespace MultiThreadedInstaller
