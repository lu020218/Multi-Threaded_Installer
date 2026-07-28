#include "installer/payload/folder_install_executor.h"

#include "common/content_hash.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/platform/file_system_operator.h"
#include "installer/platform/installer_helpers.h"
#include "installer/payload/stream_sink.h"
#include "installer/payload/tar_stream_extractor.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <thread>
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

// 帧级并行安装的 worker 数上限（引擎写死）：写盘/AV 扫描是主要瓶颈，并发收益在 4~8 后趋平。
constexpr size_t kFrameInstallWorkersCap = 8;

// Installs a per-file framed folder payload (P2 + 聚合帧 + 并行)：
//   · fileIndex 按 frameOffset 聚合成"帧组"（大文件独帧；小文件多成员共享一帧，
//     entry.offset 为解压后缓冲内的帧内偏移）；
//   · 帧组内全部成员指纹命中 → 连帧都不读不解压，整组零成本跳过；
//   · 其余帧组由 worker 池并行处理：读帧 → 解压 → 逐成员切片校验哈希 → 经
//     worker 私有的 TarStreamExtractor 走 staging + 原子替换落盘（含锁定文件
//     pending-replace）；结果在末尾统一合并。
bool InstallFramedFolder(const FolderInstallRequest& request,
                         FolderPayloadReader& payloadReader,
                         DecompressionEngine& decompressionEngine,
                         FolderInstallResult& result,
                         const std::function<void(const std::string&)>& logError) {
    (void)decompressionEngine;  // worker 各持私有引擎实例（并发安全）
    const PackagePayloadFolder& mapping = request.mapping;
    const InstalledFileFingerprintMap* oldFingerprints = request.oldInstalledFingerprints.get();

    // 1) 按 frameOffset 聚合帧组（同帧成员在 fileIndex 中天然连续，稳妥起见仍按键归组）。
    struct FrameGroup {
        uint64_t frameOffset = 0;
        uint64_t frameCompressedSize = 0;
        uint64_t decompressedSize = 0;
        std::vector<const FileIndexEntry*> members;
    };
    std::vector<FrameGroup> groups;
    for (const auto& entry : mapping.fileIndex) {
        if (entry.relativePath.empty()) {
            continue;
        }
        if (entry.size > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
            logError("Framed file too large: " + entry.relativePath);
            return false;
        }
        if (groups.empty() || groups.back().frameOffset != entry.frameOffset ||
            groups.back().frameCompressedSize != entry.frameCompressedSize) {
            FrameGroup group;
            group.frameOffset = entry.frameOffset;
            group.frameCompressedSize = entry.frameCompressedSize;
            groups.push_back(std::move(group));
        }
        FrameGroup& group = groups.back();
        group.members.push_back(&entry);
        group.decompressedSize = (std::max)(group.decompressedSize, entry.offset + entry.size);
    }

    // 2) worker 池并行处理帧组。共享聚合状态一律加锁合并；私有 extractor/engine 免锁。
    struct SharedState {
        std::mutex mutex;
        std::vector<std::string> installedFiles;
        std::vector<std::string> skippedFiles;
        std::vector<std::string> pendingReplaceFiles;
        std::string firstError;
        double readSec = 0.0;
        double decompressSec = 0.0;
        double writeSec = 0.0;
    } shared;
    std::atomic<size_t> nextGroup{0};
    std::atomic<bool> failed{false};
    std::atomic<bool> cancelled{false};

    auto worker = [&]() {
        TarStreamExtractor writer(request.resolvedTargetPath);
        DecompressionEngine engine;
        std::vector<std::string> localSkipped;
        double readSec = 0.0;
        double decompressSec = 0.0;
        double writeSec = 0.0;
        std::string localError;

        auto fail = [&](const std::string& message) {
            localError = message;
            failed.store(true);
        };

        for (;;) {
            if (failed.load() || cancelled.load()) {
                break;
            }
            const size_t index = nextGroup.fetch_add(1);
            if (index >= groups.size()) {
                break;
            }
            if (request.cancellationCallback && request.cancellationCallback()) {
                cancelled.store(true);
                break;
            }
            const FrameGroup& group = groups[index];

            // 逐成员判定跳过；全部命中则整帧零读跳过。
            std::vector<const FileIndexEntry*> toWrite;
            toWrite.reserve(group.members.size());
            for (const FileIndexEntry* entry : group.members) {
                const std::filesystem::path fullPath =
                    PathFromUtf8(request.resolvedTargetPath) / PathFromUtf8(entry->relativePath);
                if (ExistingInstalledFileMatches(fullPath, entry->size, entry->contentHash,
                                                 oldFingerprints)) {
                    localSkipped.push_back(Utf8FromPath(fullPath));
                } else {
                    toWrite.push_back(entry);
                }
            }
            if (toWrite.empty()) {
                continue;
            }

            // 读帧 + 解压（decompressedSize==0 的全空文件帧无需解压）。
            std::vector<uint8_t> content;
            if (group.decompressedSize > 0) {
                const auto readStart = std::chrono::steady_clock::now();
                std::string readError;
                std::vector<uint8_t> frame = payloadReader.readPayload(
                    mapping.offset + group.frameOffset, group.frameCompressedSize, &readError);
                readSec += std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - readStart).count();
                if (frame.empty() && group.frameCompressedSize > 0) {
                    fail("Failed to read frame for '" + toWrite.front()->relativePath +
                         "': " + readError);
                    break;
                }

                const auto decompressStart = std::chrono::steady_clock::now();
                content.reserve(static_cast<size_t>(group.decompressedSize));
                BufferSink sink(content);
                DecompressionTask task;
                task.compressedData = std::move(frame);
                task.folderName = request.folderName;
                task.targetPath = request.resolvedTargetPath;
                task.originalSize = static_cast<size_t>(group.decompressedSize);
                task.algorithm = mapping.algorithm;
                task.expectedChecksum = 0;
                const bool decompressOk = engine.decompressToStream(task, sink, nullptr, nullptr, nullptr);
                decompressSec += std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - decompressStart).count();
                if (!decompressOk) {
                    fail("Failed to decompress frame for '" + toWrite.front()->relativePath + "'");
                    break;
                }
                if (content.size() != group.decompressedSize) {
                    fail("Decompressed frame size mismatch for '" +
                         toWrite.front()->relativePath + "'");
                    break;
                }
            }

            // 逐成员：帧内切片 → 哈希校验 → 合成单文件 tar 记录经 extractor 落盘。
            bool groupFailed = false;
            for (const FileIndexEntry* entry : toWrite) {
                if (entry->offset + entry->size > content.size() &&
                    !(entry->size == 0 && entry->offset <= content.size())) {
                    fail("Frame member out of range: " + entry->relativePath);
                    groupFailed = true;
                    break;
                }
                const uint8_t* slice = content.data() + entry->offset;
                if (entry->contentHash != 0 &&
                    ComputeContentHash64(slice, static_cast<size_t>(entry->size)) !=
                        entry->contentHash) {
                    fail("Decompressed frame content hash mismatch for '" +
                         entry->relativePath + "'");
                    groupFailed = true;
                    break;
                }

                const auto writeStart = std::chrono::steady_clock::now();
                // Synthesize the single-file tar record the extractor expects:
                // [uint32 pathLength][uint32 fileSize][path][content].
                std::vector<uint8_t> record;
                const uint32_t pathLength = static_cast<uint32_t>(entry->relativePath.size());
                const uint32_t fileSize = static_cast<uint32_t>(entry->size);
                record.reserve(sizeof(uint32_t) * 2 + entry->relativePath.size() +
                               static_cast<size_t>(entry->size));
                const auto* pathLenBytes = reinterpret_cast<const uint8_t*>(&pathLength);
                record.insert(record.end(), pathLenBytes, pathLenBytes + sizeof(uint32_t));
                const auto* fileSizeBytes = reinterpret_cast<const uint8_t*>(&fileSize);
                record.insert(record.end(), fileSizeBytes, fileSizeBytes + sizeof(uint32_t));
                record.insert(record.end(), entry->relativePath.begin(), entry->relativePath.end());
                record.insert(record.end(), slice, slice + entry->size);
                const bool writeOk = writer.write(record.data(), record.size());
                writeSec += std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - writeStart).count();
                if (!writeOk) {
                    fail("Failed to write framed file '" + entry->relativePath + "'");
                    groupFailed = true;
                    break;
                }
            }
            if (groupFailed) {
                break;
            }
        }
        writer.flush();

        std::lock_guard<std::mutex> lock(shared.mutex);
        const auto& written = writer.installedFiles();
        shared.installedFiles.insert(shared.installedFiles.end(), written.begin(), written.end());
        shared.skippedFiles.insert(shared.skippedFiles.end(), localSkipped.begin(),
                                   localSkipped.end());
        const auto& pending = writer.pendingReplaceFiles();
        shared.pendingReplaceFiles.insert(shared.pendingReplaceFiles.end(), pending.begin(),
                                          pending.end());
        shared.readSec += readSec;
        shared.decompressSec += decompressSec;
        shared.writeSec += writeSec;
        if (!localError.empty() && shared.firstError.empty()) {
            shared.firstError = localError;
        }
    };

    const size_t workerCount = std::min<size_t>(
        {groups.empty() ? size_t{1} : groups.size(),
         static_cast<size_t>((std::max)(1u, std::thread::hardware_concurrency())),
         kFrameInstallWorkersCap});
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        workers.emplace_back(worker);
    }
    for (auto& thread : workers) {
        thread.join();
    }

    result.readSec = shared.readSec;
    result.decompressSec = shared.decompressSec;
    result.writeSec = shared.writeSec;
    if (cancelled.load()) {
        result.cancelled = true;
        return false;
    }
    if (failed.load()) {
        logError(shared.firstError.empty() ? "Framed folder install failed." : shared.firstError);
        return false;
    }

    result.installedFiles = std::move(shared.installedFiles);
    const size_t writtenCount = result.installedFiles.size();
    result.installedFiles.insert(result.installedFiles.end(), shared.skippedFiles.begin(),
                                 shared.skippedFiles.end());
    result.pendingReplaceFiles = std::move(shared.pendingReplaceFiles);
    result.rebootRequired = !result.pendingReplaceFiles.empty();
    logInstallerInfo("[DECOMP] framed install folder=" + request.folderName +
                     " frames=" + std::to_string(groups.size()) +
                     " workers=" + std::to_string(workerCount) +
                     " skipped=" + std::to_string(shared.skippedFiles.size()) +
                     " written=" + std::to_string(writtenCount));
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
