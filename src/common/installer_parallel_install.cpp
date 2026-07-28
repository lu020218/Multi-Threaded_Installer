#include "common/installer_parallel_install.h"

#include "common/installer_logger.h"
#include "installer/payload/folder_install_executor.h"
#include "installer/payload/folder_payload_reader.h"
#include "installer/pipeline/installer_concurrency_policy.h"
#include "installer/platform/installer_helpers.h"
#include "installer/platform/path_resolver.h"
#include "installer/pipeline/thread_pool_manager.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <unordered_set>

namespace MultiThreadedInstaller {

namespace {

struct FolderDispatch {
    std::string folderName;
    std::string targetPath;
    PackagePayloadFolder mapping;
};

std::string ReplaceAll(std::string value, const std::string& token, const std::string& replacement) {
    if (token.empty()) {
        return value;
    }
    size_t pos = 0;
    while ((pos = value.find(token, pos)) != std::string::npos) {
        value.replace(pos, token.size(), replacement);
        pos += replacement.size();
    }
    return value;
}

} // namespace

ParallelInstallResult RunParallelInstall(const PackageManifest& metadata,
                                         FolderPayloadReader& payloadReader,
                                         InstallerPathResolver& pathResolver,
                                         const std::string& userSelectedPath,
                                         const std::vector<std::pair<std::string, std::string>>& folderMappings,
                                         const std::vector<std::string>& includedFolders,
                                         bool filterFolders,
                                         int threadCount,
                                         const ProgressCallback& progressCallback,
                                         const LogCallback& infoCallback,
                                         const LogCallback& errorCallback,
                                         const CancellationCallback& cancellationCallback,
                                         std::shared_ptr<const InstalledFileFingerprintMap>
                                             oldInstalledFingerprints) {
    ParallelInstallResult result;

    auto logInfo = [&](const std::string& message) {
        if (infoCallback) {
            infoCallback(message);
        }
    };
    auto logError = [&](const std::string& message) {
        if (errorCallback) {
            errorCallback(message);
        }
    };

    std::unordered_set<std::string> includedFolderSet;
    if (filterFolders) {
        includedFolderSet.reserve(includedFolders.size());
        for (const auto& folder : includedFolders) {
            if (!folder.empty()) {
                includedFolderSet.insert(folder);
            }
        }
    }

    std::vector<FolderDispatch> dispatches;
    dispatches.reserve(metadata.payload.folders.size());

    for (const auto& mapping : metadata.payload.folders) {
        if (cancellationCallback && cancellationCallback()) {
            result.cancelled = true;
            result.errors.push_back("Installation cancelled.");
            return result;
        }

        if (filterFolders &&
            (includedFolderSet.empty() ||
             includedFolderSet.find(mapping.folderId) == includedFolderSet.end())) {
            continue;
        }

        std::string targetPath;
        for (const auto& explicitMapping : folderMappings) {
            if (explicitMapping.first == mapping.folderId ||
                explicitMapping.first == mapping.folderName) {
                targetPath = explicitMapping.second;
                break;
            }
        }

        if (targetPath.empty()) {
            std::string expandedTarget = ReplaceAll(mapping.target, "%InstallDir%", userSelectedPath);
            targetPath = pathResolver.expandEnvironmentVariables(expandedTarget);
        }

        if (targetPath.empty()) {
            const std::string message = "No target path specified for folder: " + mapping.folderName;
            logError(message);
            result.errors.push_back(message);
            continue;
        }

        if (result.installRootPath.empty() &&
            mapping.target.find("%InstallDir%") != std::string::npos) {
            result.installRootPath = targetPath;
        }

        FolderDispatch dispatch;
        dispatch.folderName = mapping.folderName;
        dispatch.targetPath = targetPath;
        dispatch.mapping = mapping;
        dispatches.push_back(std::move(dispatch));
        result.installedRoots.push_back(targetPath);
    }

    if (!result.errors.empty()) {
        result.success = false;
        return result;
    }

    if (dispatches.empty()) {
        result.success = true;
        return result;
    }

    (void)threadCount;
    const size_t workerCount = ResolvePayloadWorkerCount(dispatches.size());
    const uint32_t decoderBudget =
        ResolveDecoderThreadCount(static_cast<uint32_t>(std::max<size_t>(1, workerCount)));
    logInstallerInfo("[InstallFlow][Payload] folderCount=" + std::to_string(dispatches.size()) +
                     " hwThreads=" + std::to_string(GetInstallerHardwareConcurrency()) +
                     " payloadWorkers=" + std::to_string(workerCount) +
                     " decoderBudget=" + std::to_string(decoderBudget));

    std::mutex resultMutex;
    std::vector<std::string> errors;
    std::vector<std::string> pendingReplaceFiles;
    std::vector<std::string> installedFiles;
    std::vector<FolderTiming> folderTimings;
    std::atomic<bool> overallSuccess(true);
    std::atomic<bool> cancelled(false);
    std::atomic<long long> totalReadNs(0);
    std::atomic<long long> totalDecompressNs(0);
    std::atomic<long long> totalWriteNs(0);
    std::atomic<long long> totalNs(0);

    ThreadPoolManager threadPool(workerCount);
    DecompressionEngine decompressor;
    decompressor.registerProgressCallback(
        [&](const std::string& folder, const std::string& currentFile, float progress) {
            if (cancellationCallback && cancellationCallback()) {
                cancelled.store(true);
                throw std::runtime_error("Installation cancelled.");
            }
            if (progressCallback) {
                progressCallback(folder, currentFile, progress);
            }
        });
    FolderInstallExecutor executor(payloadReader, decompressor);

    for (const auto& dispatch : dispatches) {
        threadPool.enqueue([&, dispatch]() {
            if (cancelled.load()) {
                return;
            }

            FolderInstallRequest request;
            request.folderName = dispatch.folderName;
            request.mapping = dispatch.mapping;
            request.resolvedTargetPath = dispatch.targetPath;
            request.schedulerConcurrencyHint = static_cast<unsigned int>(workerCount);
            request.oldInstalledFingerprints = oldInstalledFingerprints;
            request.cancellationCallback = cancellationCallback;
            request.infoCallback = infoCallback;
            request.errorCallback = errorCallback;

            FolderInstallResult folderResult = executor.execute(request);
            if (folderResult.cancelled) {
                cancelled.store(true);
            }
            if (!folderResult.success) {
                overallSuccess.store(false);
            }

            FolderTiming timing;
            timing.folderName = folderResult.folderName;
            timing.readSec = folderResult.readSec;
            timing.decompressSec = folderResult.decompressSec;
            timing.writeSec = folderResult.writeSec;
            timing.totalSec = folderResult.totalSec;

            totalReadNs.fetch_add(static_cast<long long>(folderResult.readSec * 1e9));
            totalDecompressNs.fetch_add(static_cast<long long>(folderResult.decompressSec * 1e9));
            totalWriteNs.fetch_add(static_cast<long long>(folderResult.writeSec * 1e9));
            totalNs.fetch_add(static_cast<long long>(folderResult.totalSec * 1e9));

            std::lock_guard<std::mutex> lock(resultMutex);
            folderTimings.push_back(std::move(timing));
            errors.insert(errors.end(), folderResult.errors.begin(), folderResult.errors.end());
            if (folderResult.rebootRequired) {
                result.rebootRequired = true;
            }
            pendingReplaceFiles.insert(pendingReplaceFiles.end(),
                                       folderResult.pendingReplaceFiles.begin(),
                                       folderResult.pendingReplaceFiles.end());
            installedFiles.insert(installedFiles.end(),
                                  folderResult.installedFiles.begin(),
                                  folderResult.installedFiles.end());
        });
    }

    threadPool.waitForAll();

    result.errors = std::move(errors);
    result.cancelled = cancelled.load();
    result.success = overallSuccess.load() && result.errors.empty() && !result.cancelled;
    result.pendingReplaceFiles = std::move(pendingReplaceFiles);
    std::sort(installedFiles.begin(), installedFiles.end());
    installedFiles.erase(std::unique(installedFiles.begin(), installedFiles.end()), installedFiles.end());
    result.installedFiles = std::move(installedFiles);
    result.timing.payloadReadSec = static_cast<double>(totalReadNs.load()) / 1e9;
    result.timing.decompressSec = static_cast<double>(totalDecompressNs.load()) / 1e9;
    result.timing.writeSec = static_cast<double>(totalWriteNs.load()) / 1e9;
    result.timing.totalSec = static_cast<double>(totalNs.load()) / 1e9;
    result.timing.folderTimings = std::move(folderTimings);

    return result;
}

} // namespace MultiThreadedInstaller
