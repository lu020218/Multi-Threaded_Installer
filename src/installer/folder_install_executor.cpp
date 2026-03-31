#include "installer/folder_install_executor.h"

#include "installer/file_system_operator.h"

#include <chrono>

namespace MultiThreadedInstaller {

FolderInstallExecutor::FolderInstallExecutor(FolderPayloadReader& payloadReader,
                                             DecompressionEngine& decompressionEngine)
    : payloadReader_(payloadReader),
      decompressionEngine_(decompressionEngine) {}

FolderInstallResult FolderInstallExecutor::execute(const FolderInstallRequest& request) {
    FolderInstallResult result;
    result.folderName = request.folderName;
    result.targetPath = request.resolvedTargetPath;

    const auto totalStart = std::chrono::steady_clock::now();
    auto logError = [&](const std::string& message) {
        result.errors.push_back(message);
        if (request.errorCallback) {
            request.errorCallback(message);
        }
    };

    if (request.cancellationCallback && request.cancellationCallback()) {
        result.cancelled = true;
        result.errors.push_back("Installation cancelled.");
        return result;
    }

    if (request.resolvedTargetPath.empty()) {
        logError("Resolved target path is empty for folder: " + request.folderName);
        return result;
    }

    FileSystemOperator fsOperator;
    if (!fsOperator.createDirectoryRecursive(request.resolvedTargetPath)) {
        logError("Failed to create target directory: " + request.resolvedTargetPath);
        return result;
    }

    std::string payloadError;
    const auto readStart = std::chrono::steady_clock::now();
    std::vector<uint8_t> compressedPayload =
        payloadReader_.readPayload(request.mapping.offset, request.mapping.compressedSize, &payloadError);
    const auto readEnd = std::chrono::steady_clock::now();
    result.readSec = std::chrono::duration<double>(readEnd - readStart).count();

    if (compressedPayload.empty()) {
        if (payloadError.empty()) {
            payloadError = "Failed to read folder payload.";
        }
        logError(payloadError + " folder=" + request.folderName);
        return result;
    }

    if (request.cancellationCallback && request.cancellationCallback()) {
        result.cancelled = true;
        result.errors.push_back("Installation cancelled.");
        return result;
    }

    if (request.infoCallback) {
        request.infoCallback("Installing folder payload '" + request.folderName +
                             "' to: " + request.resolvedTargetPath);
    }

    DecompressionTask task;
    task.compressedData = std::move(compressedPayload);
    task.folderName = request.folderName;
    task.targetPath = request.resolvedTargetPath;
    task.schedulerConcurrencyHint = request.schedulerConcurrencyHint;
    task.expectedChecksum = request.mapping.checksum;
    task.originalSize = static_cast<size_t>(request.mapping.originalSize);
    task.algorithm = request.mapping.algorithm;

    DecompressionEngine::DecompressionTiming timing{};
    bool ok = false;
    try {
        ok = decompressionEngine_.decompressFolder(task, &timing);
    } catch (const std::exception& e) {
        logError(std::string("Folder payload installation aborted for '") +
                 request.folderName + "': " + e.what());
        ok = false;
    } catch (...) {
        logError("Folder payload installation aborted for '" + request.folderName +
                 "': unknown exception");
        ok = false;
    }
    result.decompressSec = static_cast<double>(timing.decompressNs) / 1e9;
    result.writeSec = static_cast<double>(timing.writeNs) / 1e9;

    if (!ok) {
        logError("Failed to install folder payload: " + request.folderName);
    } else {
        result.success = true;
    }

    if (request.cancellationCallback && request.cancellationCallback()) {
        result.cancelled = true;
        if (result.errors.empty()) {
            result.errors.push_back("Installation cancelled.");
        }
        result.success = false;
    }

    const auto totalEnd = std::chrono::steady_clock::now();
    result.totalSec = std::chrono::duration<double>(totalEnd - totalStart).count();
    return result;
}

} // namespace MultiThreadedInstaller
