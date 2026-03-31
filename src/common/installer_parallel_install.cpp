#include "common/installer_parallel_install.h"

#include "common/installer_logger.h"
#include "installer/folder_install_executor.h"
#include "installer/folder_payload_reader.h"
#include "installer/installer_helpers.h"
#include "installer/path_resolver.h"
#include "installer/thread_pool_manager.h"

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
    ExtendedFolderMapping mapping;
};

} // namespace

ParallelInstallResult RunParallelInstall(const ExtendedInstallationMetadata& metadata,
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
                                         const CancellationCallback& cancellationCallback) {
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
    dispatches.reserve(metadata.extendedPayloadMappings.size());

    for (const auto& mapping : metadata.extendedPayloadMappings) {
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
            const std::string directoryName =
                resolveEffectiveDirectoryName(metadata.appDirectoryName, metadata.appName);
            std::string basePath;
            if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                basePath = pathResolver.resolveFinalPath(
                    userSelectedPath,
                    mapping.targetDirType,
                    directoryName,
                    mapping.appendDirectoryName);
            } else {
                basePath = pathResolver.resolveFinalPath(
                    mapping.customTargetPath.empty() ? mapping.targetPath : mapping.customTargetPath,
                    mapping.targetDirType,
                    directoryName,
                    mapping.appendDirectoryName);
            }

            if (!basePath.empty()) {
                if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                    targetPath = basePath;
                } else {
                    if (basePath.back() != '\\' && basePath.back() != '/') {
                        basePath.push_back('\\');
                    }
                    targetPath = basePath + mapping.folderName;
                }
            }
        }

        if (targetPath.empty()) {
            const std::string message = "No target path specified for folder: " + mapping.folderName;
            logError(message);
            result.errors.push_back(message);
            continue;
        }

        if (result.installRootPath.empty() &&
            mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
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

    const size_t workerCount =
        ResolveThreadPoolWorkerCount(threadCount > 0 ? static_cast<size_t>(threadCount) : 0);
    logInstallerInfo("[InstallFlow][Payload] folderCount=" + std::to_string(dispatches.size()) +
                     " requestedThreadCount=" + std::to_string(threadCount) +
                     " workerCount=" + std::to_string(workerCount));

    std::mutex resultMutex;
    std::vector<std::string> errors;
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
        });
    }

    threadPool.waitForAll();

    result.errors = std::move(errors);
    result.cancelled = cancelled.load();
    result.success = overallSuccess.load() && result.errors.empty() && !result.cancelled;
    result.timing.payloadReadSec = static_cast<double>(totalReadNs.load()) / 1e9;
    result.timing.decompressSec = static_cast<double>(totalDecompressNs.load()) / 1e9;
    result.timing.writeSec = static_cast<double>(totalWriteNs.load()) / 1e9;
    result.timing.totalSec = static_cast<double>(totalNs.load()) / 1e9;
    result.timing.folderTimings = std::move(folderTimings);

    return result;
}

} // namespace MultiThreadedInstaller
