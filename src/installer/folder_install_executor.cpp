#include "installer/folder_install_executor.h"

#include "common/content_hash.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/file_system_operator.h"
#include "installer/installer_helpers.h"
#include "installer/stream_sink.h"
#include "installer/tar_stream_extractor.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

// Collects decompressed bytes into a caller-owned buffer.
class BufferSink : public StreamSink {
public:
    explicit BufferSink(std::vector<uint8_t>& out) : out_(out) {}
    bool write(const uint8_t* data, size_t size) override {
        out_.insert(out_.end(), data, data + size);
        return true;
    }
    void flush() override {}

private:
    std::vector<uint8_t>& out_;
};

// Installs a per-file framed folder payload (P2): iterate the file index,
// skip unchanged files without reading or decompressing their frame, and for
// changed/new files decompress only that file's frame and write it through the
// shared staging + atomic-replace path (reusing TarStreamExtractor).
bool InstallFramedFolder(const FolderInstallRequest& request,
                         FolderPayloadReader& payloadReader,
                         DecompressionEngine& decompressionEngine,
                         FolderInstallResult& result,
                         const std::function<void(const std::string&)>& logError) {
    const ExtendedFolderMapping& mapping = request.mapping;
    TarStreamExtractor writer(request.resolvedTargetPath);
    // No skip fingerprints set on the writer: we decide skips ourselves below
    // (before decompressing), and only feed the writer the files we want written.
    std::vector<std::string> skippedFiles;
    const InstalledFileFingerprintMap* oldFingerprints = request.oldInstalledFingerprints.get();

    for (const auto& entry : mapping.fileIndex) {
        if (request.cancellationCallback && request.cancellationCallback()) {
            result.cancelled = true;
            return false;
        }
        if (entry.relativePath.empty()) {
            continue;
        }
        if (entry.size > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
            logError("Framed file too large: " + entry.relativePath);
            return false;
        }

        const std::filesystem::path fullPath =
            PathFromUtf8(request.resolvedTargetPath) / PathFromUtf8(entry.relativePath);

        // Skip unchanged files without touching the compressed frame at all.
        if (ExistingInstalledFileMatches(fullPath, entry.size, entry.contentHash, oldFingerprints)) {
            skippedFiles.push_back(Utf8FromPath(fullPath));
            continue;
        }

        std::string readError;
        std::vector<uint8_t> frame = payloadReader.readPayload(
            mapping.offset + entry.frameOffset, entry.frameCompressedSize, &readError);
        if (frame.empty() && entry.frameCompressedSize > 0) {
            logError("Failed to read frame for '" + entry.relativePath + "': " + readError);
            return false;
        }

        std::vector<uint8_t> content;
        content.reserve(static_cast<size_t>(entry.size));
        BufferSink sink(content);
        DecompressionTask task;
        task.compressedData = std::move(frame);
        task.folderName = request.folderName;
        task.targetPath = request.resolvedTargetPath;
        task.originalSize = static_cast<size_t>(entry.size);
        task.algorithm = mapping.algorithm;
        task.expectedChecksum = 0;
        if (!decompressionEngine.decompressToStream(task, sink, nullptr, nullptr, nullptr)) {
            logError("Failed to decompress frame for '" + entry.relativePath + "'");
            return false;
        }
        if (content.size() != entry.size) {
            logError("Decompressed frame size mismatch for '" + entry.relativePath + "'");
            return false;
        }
        if (entry.contentHash != 0 &&
            ComputeContentHash64(content.data(), content.size()) != entry.contentHash) {
            logError("Decompressed frame content hash mismatch for '" + entry.relativePath + "'");
            return false;
        }

        // Synthesize the single-file tar record the extractor expects:
        // [uint32 pathLength][uint32 fileSize][path][content].
        std::vector<uint8_t> record;
        const uint32_t pathLength = static_cast<uint32_t>(entry.relativePath.size());
        const uint32_t fileSize = static_cast<uint32_t>(content.size());
        record.reserve(sizeof(uint32_t) * 2 + entry.relativePath.size() + content.size());
        const auto* pathLenBytes = reinterpret_cast<const uint8_t*>(&pathLength);
        record.insert(record.end(), pathLenBytes, pathLenBytes + sizeof(uint32_t));
        const auto* fileSizeBytes = reinterpret_cast<const uint8_t*>(&fileSize);
        record.insert(record.end(), fileSizeBytes, fileSizeBytes + sizeof(uint32_t));
        record.insert(record.end(), entry.relativePath.begin(), entry.relativePath.end());
        record.insert(record.end(), content.begin(), content.end());
        if (!writer.write(record.data(), record.size())) {
            logError("Failed to write framed file '" + entry.relativePath + "'");
            return false;
        }
    }
    writer.flush();

    result.installedFiles = writer.installedFiles();
    result.installedFiles.insert(result.installedFiles.end(),
                                 skippedFiles.begin(), skippedFiles.end());
    result.pendingReplaceFiles = writer.pendingReplaceFiles();
    result.rebootRequired = !writer.pendingReplaceFiles().empty();
    logInstallerInfo("[DECOMP] framed install folder=" + request.folderName +
                     " skipped=" + std::to_string(skippedFiles.size()) +
                     " written=" + std::to_string(writer.installedFiles().size()));
    return true;
}

} // namespace

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

    // Per-file framed payloads (P2) decompress only changed files; unchanged
    // files are skipped without reading or decompressing their frame.
    if (request.mapping.framed) {
        if (request.infoCallback) {
            request.infoCallback("Installing framed folder payload '" + request.folderName +
                                 "' to: " + request.resolvedTargetPath);
        }
        bool framedOk = false;
        try {
            framedOk = InstallFramedFolder(request, payloadReader_, decompressionEngine_,
                                           result, logError);
        } catch (const std::exception& e) {
            logError(std::string("Framed folder installation aborted for '") +
                     request.folderName + "': " + e.what());
            framedOk = false;
        } catch (...) {
            logError("Framed folder installation aborted for '" + request.folderName +
                     "': unknown exception");
            framedOk = false;
        }
        result.success = framedOk && !result.cancelled;
        if (request.cancellationCallback && request.cancellationCallback()) {
            result.cancelled = true;
            result.success = false;
            if (result.errors.empty()) {
                result.errors.push_back("Installation cancelled.");
            }
        }
        const auto totalEnd = std::chrono::steady_clock::now();
        result.totalSec = std::chrono::duration<double>(totalEnd - totalStart).count();
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
    // Per-file fingerprints let the extractor skip rewriting unchanged files.
    task.fileIndex = request.mapping.fileIndex;
    task.oldInstalledFingerprints = request.oldInstalledFingerprints;

    DecompressionEngine::DecompressionTiming timing{};
    bool ok = false;
    DecompressionEngine::DecompressionOutcome outcome;
    try {
        ok = decompressionEngine_.decompressFolder(task, &timing, &outcome);
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
    result.rebootRequired = outcome.rebootRequired;
    result.pendingReplaceFiles = std::move(outcome.pendingReplaceFiles);
    result.installedFiles = std::move(outcome.installedFiles);

    if (!ok) {
        logError("Failed to install folder payload: " + request.folderName);
    } else {
        result.success = true;
        if (result.rebootRequired && request.infoCallback) {
            request.infoCallback("Folder payload '" + request.folderName +
                                 "' scheduled locked files for replacement after reboot.");
        }
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
