#include "installer/pipeline/install_execution.h"

#include "common/installer_logger.h"
#include "installer/payload/folder_payload_reader.h"
#include "installer/payload/metadata_parser.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

float Clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

} // namespace

// 单产品单载荷：执行期只负责把全部 payload 解压到安装目录。原 local/download 组件的
// "额外安装动作"能力已由 hooks（install_service 在解压前后调度）替代，不再在此处理。
bool ExecuteInstallExecution(const ExtendedInstallationMetadata& metadata,
                             MetadataParser& parser,
                             const InstallExecutionPlan& plan,
                             const InstallServiceOptions& options,
                             InstallerPathResolver& pathResolver,
                             InstallProgressReporter& reporter,
                             InstallExecutionOutput& output) {
    output = InstallExecutionOutput{};
    output.success = false;

    reporter.EmitStatus(InstallServiceStatus::Installing,
                        InstallServicePhase::Installing,
                        0.0f,
                        "Installing files...");

    std::unordered_map<std::string, uint64_t> folderSizes;
    std::unordered_map<std::string, float> folderProgress;
    folderSizes.reserve(plan.selectedEmbeddedFolders.size());
    folderProgress.reserve(plan.selectedEmbeddedFolders.size());
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        folderSizes[mapping.folderName] = mapping.originalSize;
        folderProgress[mapping.folderName] = 0.0f;
    }
    std::mutex installProgressMutex;

    ProgressCallback progressCallback = [&](const std::string& folder,
                                            const std::string& currentFile,
                                            float progress) {
        float phaseProgress = Clamp01(progress);
        if (plan.totalInstallBytes > 0) {
            std::lock_guard<std::mutex> lock(installProgressMutex);
            folderProgress[folder] = Clamp01(progress);
            double completed = 0.0;
            for (const auto& entry : folderProgress) {
                auto sizeIt = folderSizes.find(entry.first);
                if (sizeIt != folderSizes.end()) {
                    completed += static_cast<double>(sizeIt->second) * static_cast<double>(entry.second);
                }
            }
            phaseProgress =
                static_cast<float>(completed / static_cast<double>(plan.totalInstallBytes));
        }
        reporter.EmitProgress(folder, currentFile, phaseProgress);
    };

    LogCallback infoCallback = [&](const std::string& message) {
        reporter.EmitMessage(InstallServiceEventType::Info, message);
    };
    LogCallback errorCallback = [&](const std::string& message) {
        reporter.EmitMessage(InstallServiceEventType::Error, message);
    };

    logInstallerInfo(std::string("[InstallFlow][Extract] start folderCount=") +
                     std::to_string(plan.selectedEmbeddedFolders.size()) +
                     " installPath=" + options.installPath);

    // Use the previous install's per-file fingerprints (captured during planning,
    // before cleanup deleted the old manifest) so unchanged files can be skipped
    // without reading them from disk (Scheme A, zero read).
    std::shared_ptr<const InstalledFileFingerprintMap> oldInstalledFingerprints =
        plan.previousInstalledFingerprints;
    if (oldInstalledFingerprints) {
        logInstallerInfo("[InstallFlow][Extract] zero-read fingerprints available count=" +
                         std::to_string(oldInstalledFingerprints->size()));
    }

    ParallelInstallResult parallelResult;
    FolderPayloadReader payloadReader(parser.getDataPackagePath());
    parallelResult = RunParallelInstall(metadata,
                                        payloadReader,
                                        pathResolver,
                                        options.installPath,
                                        options.folderMappings,
                                        plan.selectedEmbeddedFolders,
                                        /*filterFolders=*/false,
                                        0,
                                        progressCallback,
                                        infoCallback,
                                        errorCallback,
                                        options.cancellationCallback,
                                        oldInstalledFingerprints);
    logInstallerInfo(std::string("[InstallFlow][Extract] end success=") +
                     (parallelResult.success ? "true" : "false") +
                     " cancelled=" + (parallelResult.cancelled ? "true" : "false") +
                     " errors=" + std::to_string(parallelResult.errors.size()) +
                     " installRootPath=" + parallelResult.installRootPath);

    output.timing = parallelResult.timing;
    output.installRootPath = parallelResult.installRootPath;
    output.installedRoots = std::move(parallelResult.installedRoots);
    output.installedFiles = std::move(parallelResult.installedFiles);
    output.cancelled = parallelResult.cancelled;
    output.rebootRequired = parallelResult.rebootRequired;
    output.pendingReplaceFiles = std::move(parallelResult.pendingReplaceFiles);

    if (!parallelResult.success) {
        logInstallerError("[InstallFlow][Extract] failed, aborting installation");
        output.errors = std::move(parallelResult.errors);
        if (output.cancelled && output.errors.empty()) {
            output.errors.push_back("Installation cancelled.");
        }
        if (output.errors.empty()) {
            output.errors.push_back("Installation failed.");
        }
        for (const auto& error : output.errors) {
            reporter.EmitMessage(InstallServiceEventType::Error, error);
        }
        return false;
    }

    if (output.rebootRequired) {
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Some locked files were scheduled for replacement after reboot.");
    }

    if (output.installRootPath.empty()) {
        output.installRootPath = plan.pathDecision.resolvedInstallRoot.empty()
                                     ? options.installPath
                                     : plan.pathDecision.resolvedInstallRoot;
    }

    reporter.EmitProgress("", "File installation completed", 1.0f);

    output.effectiveRegistry = plan.effectiveRegistry;
    output.effectiveAutoStartup = plan.effectiveAutoStartup;
    output.effectiveDesktopIcons = plan.effectiveDesktopIcons;
    output.effectiveKillProcesses = plan.effectiveKillProcesses;

    output.success = true;
    return true;
}

} // namespace MultiThreadedInstaller
