#include "common/installer_parallel_install.h"

#include "installer/metadata_parser.h"
#include "installer/thread_pool_manager.h"
#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include "installer/installer_helpers.h"
#include "installer/path_resolver.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <thread>

namespace MultiThreadedInstaller {

namespace {

struct FileWriter {
    std::string path;
    uint64_t start;
    uint64_t end;
    std::mutex mutex;
};

struct BlockInfo {
    uint32_t blockId;
    uint64_t compressedOffset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint64_t startOffset;
};

struct BlockMetaHeader {
    uint32_t offset;
    uint32_t compressedSize;
    uint32_t originalSize;
    uint32_t checksum;
};

struct BlockSegment {
    size_t fileIndex;
    uint64_t blockOffset;
    uint64_t fileOffset;
    uint64_t size;
};

struct FolderTask {
    std::string folderName;
    std::string targetPath;
    ExtendedFolderMapping mapping;
    bool useIndex = false;
    DecompressionTask decompTask;
    double legacyReadSec = 0.0;
    DecompressionEngine::LegacyStageTiming legacyStage;
};

std::string BuildDisplayPath(const std::string& folderName, const std::string& relativePath) {
    if (relativePath.empty()) {
        return folderName;
    }
    std::string display = folderName;
    if (!display.empty() && display.back() != '\\' && display.back() != '/') {
        display += '\\';
    }
    if (relativePath.front() == '\\' || relativePath.front() == '/') {
        display += relativePath.substr(1);
    } else {
        display += relativePath;
    }
    return display;
}

} // namespace

ParallelInstallResult RunParallelInstall(const ExtendedInstallationMetadata& metadata,
                                         MetadataParser& parser,
                                         InstallerPathResolver& pathResolver,
                                         const std::string& userSelectedPath,
                                         const std::vector<std::pair<std::string, std::string>>& folderMappings,
                                         int threadCount,
                                         const ProgressCallback& progressCallback,
                                         const LogCallback& infoCallback,
                                         const LogCallback& errorCallback) {
    ParallelInstallResult result;
    auto logInfo = [&](const std::string& msg) {
        if (infoCallback) {
            infoCallback(msg);
        }
    };
    auto logError = [&](const std::string& msg) {
        if (errorCallback) {
            errorCallback(msg);
        }
    };
    bool hasInfoCallback = static_cast<bool>(infoCallback);

    auto threadPool = std::make_shared<ThreadPoolManager>(
        threadCount > 0 ? threadCount : std::thread::hardware_concurrency()
    );

    DecompressionEngine decompressor;
    decompressor.setThreadPool(threadPool);
    if (progressCallback) {
        decompressor.registerProgressCallback(progressCallback);
    }

    FileSystemOperator fsOperator;
    std::mutex errorsMutex;
    std::vector<std::string> errors;
    std::mutex progressMutex;
    std::atomic<bool> overallSuccess(true);
    std::atomic<size_t> completedFolders(0);
    std::atomic<long long> totalReadNs(0);
    std::atomic<long long> totalDecompressNs(0);
    std::atomic<long long> totalWriteNs(0);
    std::atomic<long long> totalLegacyNs(0);
    std::mutex timingMutex;
    std::vector<FolderTiming> folderTimings;

    std::vector<FolderTask> folderTasks;
    folderTasks.reserve(metadata.extendedMappings.size());

    for (const auto& mapping : metadata.extendedMappings) {
        std::string targetPath;
        bool foundMapping = false;

        for (const auto& userMapping : folderMappings) {
            if (userMapping.first == mapping.folderName) {
                targetPath = userMapping.second;
                foundMapping = true;
                break;
            }
        }

        if (!foundMapping) {
            std::string basePath;
            if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                basePath = pathResolver.resolveFinalPath(
                    userSelectedPath,
                    mapping.targetDirType,
                    metadata.applicationName
                );
            } else {
                basePath = pathResolver.resolveFinalPath(
                    mapping.customTargetPath.empty() ? mapping.targetPath : mapping.customTargetPath,
                    mapping.targetDirType,
                    metadata.applicationName
                );
            }

            if (!basePath.empty()) {
                if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                    targetPath = basePath;
                } else {
                    if (basePath.back() != '\\' && basePath.back() != '/') {
                        basePath += '\\';
                    }
                    targetPath = basePath + mapping.folderName;
                }
            }
        }

        if (result.installRootPath.empty() &&
            mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
            result.installRootPath = targetPath;
        }

        if (targetPath.empty()) {
            std::string error = "No target path specified for folder: " + mapping.folderName;
            logError(error);
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back(error);
            overallSuccess = false;
            continue;
        }

        logInfo("Installing folder '" + mapping.folderName + "' to: " + targetPath);

        if (!fsOperator.createDirectoryRecursive(targetPath)) {
            std::string error = "Failed to create target directory: " + targetPath;
            logError(error);
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back(error);
            overallSuccess = false;
            continue;
        }

        FolderTask folderTask;
        folderTask.folderName = mapping.folderName;
        folderTask.targetPath = targetPath;
        folderTask.mapping = mapping;
        folderTask.useIndex = !mapping.fileIndex.empty() && !mapping.blockIndex.empty();
        logInfo("Install path for '" + mapping.folderName + "': " +
                std::string(folderTask.useIndex ? "indexed" : "legacy"));

        if (!folderTask.useIndex) {
            auto readStart = std::chrono::steady_clock::now();
            std::vector<uint8_t> compressedData = parser.readCompressedData(mapping.offset, mapping.compressedSize);
            auto readEnd = std::chrono::steady_clock::now();
            if (compressedData.empty()) {
                std::string error = "Failed to read compressed data for folder: " + mapping.folderName;
                logError(error);
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
                continue;
            }
            folderTask.legacyReadSec = std::chrono::duration<double>(readEnd - readStart).count();

            folderTask.decompTask.compressedData = std::move(compressedData);
            folderTask.decompTask.targetPath = targetPath;
            folderTask.decompTask.expectedChecksum = mapping.checksum;
            folderTask.decompTask.originalSize = mapping.originalSize;
            folderTask.decompTask.algorithm = mapping.algorithm;
        }

        folderTasks.push_back(std::move(folderTask));
        result.installedRoots.push_back(targetPath);
    }

    auto installWithIndex = [&](const FolderTask& folderTask, FolderTiming& timing) -> bool {
        const auto& mapping = folderTask.mapping;
        if (mapping.fileIndex.empty() || mapping.blockIndex.empty()) {
            logError("Indexed metadata missing for '" + folderTask.folderName + "'");
            return false;
        }

        timing.indexed = true;
        timing.folderName = folderTask.folderName;
        auto totalStart = std::chrono::steady_clock::now();

        std::vector<std::unique_ptr<FileWriter>> writers;
        writers.reserve(mapping.fileIndex.size());
        std::vector<std::string> displayNames;
        displayNames.reserve(mapping.fileIndex.size());

        std::unordered_set<std::string> parentDirs;
        parentDirs.reserve(mapping.fileIndex.size());
        for (const auto& fileEntry : mapping.fileIndex) {
            std::filesystem::path fullPath = std::filesystem::path(folderTask.targetPath) / fileEntry.relativePath;
            std::filesystem::path parent = fullPath.parent_path();
            if (!parent.empty()) {
                parentDirs.insert(parent.string());
            }
        }
        FileSystemOperator fsOp;
        for (const auto& dir : parentDirs) {
            if (!fsOp.createDirectoryRecursive(dir)) {
                logError("Failed to create directory: " + dir);
                return false;
            }
        }

        uint64_t totalBytes = 0;
        for (const auto& fileEntry : mapping.fileIndex) {
            std::filesystem::path fullPath = std::filesystem::path(folderTask.targetPath) / fileEntry.relativePath;
            std::string fullPathStr = fullPath.string();
            if (!ensureFileWithSize(fullPath, fileEntry.size, metadata.sparseFileThresholdBytes)) {
                logError("Failed to create file: " + fullPathStr);
                return false;
            }

            auto writer = std::make_unique<FileWriter>();
            writer->path = std::move(fullPathStr);
            writer->start = fileEntry.offset;
            writer->end = fileEntry.offset + fileEntry.size;
            writers.push_back(std::move(writer));
            displayNames.push_back(BuildDisplayPath(folderTask.folderName, fileEntry.relativePath));
            totalBytes += fileEntry.size;
        }

        std::vector<FileWriter*> writerPtrs;
        writerPtrs.reserve(writers.size());
        for (const auto& writer : writers) {
            writerPtrs.push_back(writer.get());
        }
        if (writerPtrs.empty()) {
            logError("No files to write for '" + folderTask.folderName + "'");
            return false;
        }

        std::vector<size_t> fileOrder(writerPtrs.size());
        for (size_t i = 0; i < fileOrder.size(); ++i) {
            fileOrder[i] = i;
        }
        std::sort(fileOrder.begin(), fileOrder.end(),
                  [&](size_t a, size_t b) { return writerPtrs[a]->start < writerPtrs[b]->start; });

        std::vector<BlockInfo> blocks;
        bool parsedHeader = false;
        {
            std::vector<uint8_t> headerCount = parser.readCompressedData(mapping.offset, sizeof(uint32_t));
            if (headerCount.size() == sizeof(uint32_t)) {
                uint32_t blockCount = *reinterpret_cast<const uint32_t*>(headerCount.data());
                size_t headerSize = sizeof(uint32_t) + static_cast<size_t>(blockCount) * sizeof(BlockMetaHeader);
                if (blockCount > 0 && headerSize <= static_cast<size_t>(mapping.compressedSize)) {
                    std::vector<uint8_t> headerData = parser.readCompressedData(mapping.offset, headerSize);
                    if (headerData.size() == headerSize) {
                        blocks.reserve(blockCount);
                        size_t metaOffset = sizeof(uint32_t);
                        for (uint32_t i = 0; i < blockCount; ++i) {
                            BlockMetaHeader meta;
                            std::memcpy(&meta, headerData.data() + metaOffset + i * sizeof(BlockMetaHeader),
                                        sizeof(BlockMetaHeader));
                            BlockInfo block;
                            block.blockId = i;
                            block.compressedOffset = meta.offset;
                            block.compressedSize = meta.compressedSize;
                            block.originalSize = meta.originalSize;
                            block.startOffset = 0;
                            blocks.push_back(block);
                        }
                        parsedHeader = true;
                    }
                }
            }
        }

        if (!parsedHeader) {
            logInfo("Indexed header read failed for '" + folderTask.folderName + "', using metadata index");
            blocks.reserve(mapping.blockIndex.size());
            for (const auto& blockEntry : mapping.blockIndex) {
                BlockInfo block;
                block.blockId = blockEntry.blockId;
                block.compressedOffset = blockEntry.offset;
                block.compressedSize = blockEntry.compressedSize;
                block.originalSize = blockEntry.originalSize;
                block.startOffset = 0;
                blocks.push_back(block);
            }
        }
        if (blocks.empty()) {
            logError("No blocks available for '" + folderTask.folderName + "'");
            return false;
        }
        std::sort(blocks.begin(), blocks.end(),
                  [](const BlockInfo& a, const BlockInfo& b) { return a.blockId < b.blockId; });

        for (const auto& block : blocks) {
            if (block.compressedOffset + block.compressedSize > mapping.compressedSize) {
                logError("Invalid block metadata for '" + folderTask.folderName +
                         "': block " + std::to_string(block.blockId) + " out of range");
                return false;
            }
        }

        uint64_t cumulative = 0;
        for (auto& block : blocks) {
            block.startOffset = cumulative;
            cumulative += block.originalSize;
        }

        std::vector<std::vector<BlockSegment>> segments(blocks.size());
        size_t fileIdx = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            uint64_t blockStart = blocks[i].startOffset;
            uint64_t blockEnd = blockStart + blocks[i].originalSize;

            while (fileIdx < fileOrder.size() && writerPtrs[fileOrder[fileIdx]]->end <= blockStart) {
                ++fileIdx;
            }

            size_t k = fileIdx;
            while (k < fileOrder.size()) {
                FileWriter* writer = writerPtrs[fileOrder[k]];
                if (writer->start >= blockEnd) {
                    break;
                }

                uint64_t overlapStart = std::max(blockStart, writer->start);
                uint64_t overlapEnd = std::min(blockEnd, writer->end);
                if (overlapEnd > overlapStart) {
                    BlockSegment seg;
                    seg.fileIndex = fileOrder[k];
                    seg.blockOffset = overlapStart - blockStart;
                    seg.fileOffset = overlapStart - writer->start;
                    seg.size = overlapEnd - overlapStart;
                    segments[i].push_back(seg);
                }

                if (writer->end <= blockEnd) {
                    ++k;
                } else {
                    break;
                }
            }

            while (fileIdx < fileOrder.size() && writerPtrs[fileOrder[fileIdx]]->end <= blockEnd) {
                ++fileIdx;
            }
        }

        for (auto& segs : segments) {
            std::sort(segs.begin(), segs.end(),
                      [](const BlockSegment& a, const BlockSegment& b) {
                          if (a.fileIndex == b.fileIndex) {
                              return a.fileOffset < b.fileOffset;
                          }
                          return a.fileIndex < b.fileIndex;
                      });
        }

        std::atomic<uint64_t> writtenBytes(0);
        std::atomic<long long> readNs(0);
        std::atomic<long long> decompressNs(0);
        std::atomic<long long> writeNs(0);

        constexpr uint64_t kProgressChunkBytes = 8 * 1024 * 1024;
        if (threadPool && threadPool->getTotalThreadCount() > 1) {
            std::atomic<bool> blockFailed(false);
            std::vector<std::future<bool>> futures;
            futures.reserve(blocks.size());

            for (size_t i = 0; i < blocks.size(); ++i) {
                futures.push_back(threadPool->enqueue([&, i]() -> bool {
                    if (blockFailed.load()) {
                        return true;
                    }

                    const auto& block = blocks[i];
                    auto readStart = std::chrono::steady_clock::now();
                    std::vector<uint8_t> compressedData = parser.readCompressedData(
                        mapping.offset + block.compressedOffset,
                        block.compressedSize
                    );
                    auto readEnd = std::chrono::steady_clock::now();
                    readNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(readEnd - readStart).count());
                    if (compressedData.empty()) {
                        logError("Indexed read failed for '" + folderTask.folderName +
                                 "': block " + std::to_string(block.blockId));
                        blockFailed.store(true);
                        return false;
                    }

                    auto decompressStart = std::chrono::steady_clock::now();
                    std::vector<uint8_t> decompressed;
                    if (!decompressor.decompressLzmaBlockData(compressedData, block.originalSize, decompressed)) {
                        logError("Indexed decompress failed for '" + folderTask.folderName +
                                 "': block " + std::to_string(block.blockId));
                        blockFailed.store(true);
                        return false;
                    }
                    auto decompressEnd = std::chrono::steady_clock::now();
                    decompressNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(decompressEnd - decompressStart).count());

                    uint64_t blockWritten = 0;
                    uint64_t progressChunk = 0;
                    auto writeStart = std::chrono::steady_clock::now();
                    const auto& segs = segments[i];
                    size_t currentFileIndex = static_cast<size_t>(-1);
                    std::string currentDisplayName;
                    std::fstream stream;
                    std::unique_lock<std::mutex> fileLock;
                    for (const auto& seg : segs) {
                        if (seg.fileIndex != currentFileIndex) {
                            if (stream.is_open()) {
                                stream.close();
                            }
                            if (fileLock.owns_lock()) {
                                fileLock.unlock();
                            }
                            currentFileIndex = seg.fileIndex;
                            if (currentFileIndex < displayNames.size()) {
                                currentDisplayName = displayNames[currentFileIndex];
                            } else {
                                currentDisplayName.clear();
                            }
                            FileWriter* writer = writerPtrs[currentFileIndex];
                            fileLock = std::unique_lock<std::mutex>(writer->mutex);
                            if (!openFileForWrite(writer->path, stream)) {
                                logError("Indexed write failed for '" + folderTask.folderName +
                                         "': block " + std::to_string(block.blockId));
                                blockFailed.store(true);
                                return false;
                            }
                        }
                        stream.seekp(static_cast<std::streamoff>(seg.fileOffset));
                        stream.write(reinterpret_cast<const char*>(decompressed.data() + seg.blockOffset),
                                     static_cast<std::streamsize>(seg.size));
                        if (!stream) {
                            logError("Indexed write failed for '" + folderTask.folderName +
                                     "': block " + std::to_string(block.blockId));
                            blockFailed.store(true);
                            return false;
                        }
                        blockWritten += seg.size;
                        progressChunk += seg.size;
                        if (totalBytes > 0 && progressChunk >= kProgressChunkBytes && progressCallback) {
                            uint64_t current = writtenBytes.fetch_add(progressChunk) + progressChunk;
                            float progress = std::min(0.99f, static_cast<float>(current) / totalBytes);
                            {
                                std::lock_guard<std::mutex> lock(progressMutex);
                                progressCallback(folderTask.folderName, currentDisplayName, progress);
                            }
                            progressChunk = 0;
                        }
                    }
                    if (stream.is_open()) {
                        stream.close();
                    }
                    if (fileLock.owns_lock()) {
                        fileLock.unlock();
                    }
                    auto writeEnd = std::chrono::steady_clock::now();
                    writeNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(writeEnd - writeStart).count());

                    if (totalBytes > 0 && progressChunk > 0 && progressCallback) {
                        uint64_t toAdd = progressChunk;
                        uint64_t current = writtenBytes.fetch_add(toAdd) + toAdd;
                        float progress = std::min(0.99f, static_cast<float>(current) / totalBytes);
                        std::lock_guard<std::mutex> lock(progressMutex);
                        progressCallback(folderTask.folderName, currentDisplayName, progress);
                    }

                    return true;
                }));
            }

            for (auto& future : futures) {
                if (!future.get()) {
                    return false;
                }
            }
        } else {
            for (size_t i = 0; i < blocks.size(); ++i) {
                const auto& block = blocks[i];
                auto readStart = std::chrono::steady_clock::now();
                std::vector<uint8_t> compressedData = parser.readCompressedData(
                    mapping.offset + block.compressedOffset,
                    block.compressedSize
                );
                auto readEnd = std::chrono::steady_clock::now();
                readNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(readEnd - readStart).count());
                if (compressedData.empty()) {
                    logError("Indexed read failed for '" + folderTask.folderName +
                             "': block " + std::to_string(block.blockId));
                    return false;
                }

                auto decompressStart = std::chrono::steady_clock::now();
                std::vector<uint8_t> decompressed;
                if (!decompressor.decompressLzmaBlockData(compressedData, block.originalSize, decompressed)) {
                    logError("Indexed decompress failed for '" + folderTask.folderName +
                             "': block " + std::to_string(block.blockId));
                    return false;
                }
                auto decompressEnd = std::chrono::steady_clock::now();
                decompressNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(decompressEnd - decompressStart).count());

                uint64_t blockWritten = 0;
                uint64_t progressChunk = 0;
                auto writeStart = std::chrono::steady_clock::now();
                const auto& segs = segments[i];
                size_t currentFileIndex = static_cast<size_t>(-1);
                std::string currentDisplayName;
                std::fstream stream;
                std::unique_lock<std::mutex> fileLock;
                for (const auto& seg : segs) {
                    if (seg.fileIndex != currentFileIndex) {
                        if (stream.is_open()) {
                            stream.close();
                        }
                        if (fileLock.owns_lock()) {
                            fileLock.unlock();
                        }
                        currentFileIndex = seg.fileIndex;
                        if (currentFileIndex < displayNames.size()) {
                            currentDisplayName = displayNames[currentFileIndex];
                        } else {
                            currentDisplayName.clear();
                        }
                        FileWriter* writer = writerPtrs[currentFileIndex];
                        fileLock = std::unique_lock<std::mutex>(writer->mutex);
                        if (!openFileForWrite(writer->path, stream)) {
                            logError("Indexed write failed for '" + folderTask.folderName +
                                     "': block " + std::to_string(block.blockId));
                            return false;
                        }
                    }
                    stream.seekp(static_cast<std::streamoff>(seg.fileOffset));
                    stream.write(reinterpret_cast<const char*>(decompressed.data() + seg.blockOffset),
                                 static_cast<std::streamsize>(seg.size));
                    if (!stream) {
                        logError("Indexed write failed for '" + folderTask.folderName +
                                 "': block " + std::to_string(block.blockId));
                        return false;
                    }
                    blockWritten += seg.size;
                    progressChunk += seg.size;
                    if (totalBytes > 0 && progressChunk >= kProgressChunkBytes && progressCallback) {
                        uint64_t current = writtenBytes.fetch_add(progressChunk) + progressChunk;
                        float progress = std::min(0.99f, static_cast<float>(current) / totalBytes);
                        std::lock_guard<std::mutex> lock(progressMutex);
                        progressCallback(folderTask.folderName, currentDisplayName, progress);
                        progressChunk = 0;
                    }
                }
                if (stream.is_open()) {
                    stream.close();
                }
                if (fileLock.owns_lock()) {
                    fileLock.unlock();
                }
                auto writeEnd = std::chrono::steady_clock::now();
                writeNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(writeEnd - writeStart).count());

                if (totalBytes > 0 && (progressChunk > 0 || blockWritten > 0) && progressCallback) {
                    uint64_t toAdd = progressChunk > 0 ? progressChunk : blockWritten;
                    uint64_t current = writtenBytes.fetch_add(toAdd) + toAdd;
                    float progress = std::min(0.99f, static_cast<float>(current) / totalBytes);
                    std::lock_guard<std::mutex> lock(progressMutex);
                    progressCallback(folderTask.folderName, currentDisplayName, progress);
                }
            }
        }

        if (progressCallback) {
            progressCallback(folderTask.folderName, std::string(), 1.0f);
        }

        auto totalEnd = std::chrono::steady_clock::now();
        timing.totalSec = std::chrono::duration<double>(totalEnd - totalStart).count();
        timing.readSec = static_cast<double>(readNs.load()) / 1e9;
        timing.decompressSec = static_cast<double>(decompressNs.load()) / 1e9;
        timing.writeSec = static_cast<double>(writeNs.load()) / 1e9;
        totalReadNs.fetch_add(readNs.load());
        totalDecompressNs.fetch_add(decompressNs.load());
        totalWriteNs.fetch_add(writeNs.load());

        return true;
    };

    if (!folderTasks.empty()) {
        logInfo("Decompressing " + std::to_string(folderTasks.size()) + " folders in parallel...");

        std::vector<FolderTask*> indexedTasks;
        std::vector<FolderTask*> regularTasks;
        for (auto& folderTask : folderTasks) {
            if (folderTask.useIndex) {
                indexedTasks.push_back(&folderTask);
            } else {
                regularTasks.push_back(&folderTask);
            }
        }

        for (auto* folderTask : regularTasks) {
            threadPool->enqueue([folderTask, &decompressor, &logError, &errors, &errorsMutex,
                                &overallSuccess, &completedFolders, &totalLegacyNs, &folderTimings, &timingMutex,
                                logInfo, totalFolders = folderTasks.size(), hasInfoCallback]() {
                auto legacyStart = std::chrono::steady_clock::now();
                bool ok = decompressor.decompressFolder(folderTask->decompTask, &folderTask->legacyStage);
                auto legacyEnd = std::chrono::steady_clock::now();
                long long legacyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(legacyEnd - legacyStart).count();
                totalLegacyNs.fetch_add(legacyNs);

                if (!ok) {
                    std::string error = "Failed to decompress folder: " + folderTask->folderName;
                    logError(error);
                    std::lock_guard<std::mutex> lock(errorsMutex);
                    errors.push_back(error);
                    overallSuccess = false;
                } else {
                    FolderTiming timing;
                    timing.indexed = false;
                    timing.folderName = folderTask->folderName;
                    timing.totalSec = static_cast<double>(legacyNs) / 1e9;
                    timing.readSec = folderTask->legacyReadSec;
                    timing.decompressSec = static_cast<double>(folderTask->legacyStage.decompressNs) / 1e9;
                    timing.writeSec = static_cast<double>(folderTask->legacyStage.writeNs) / 1e9;
                    timing.processSec = std::max(0.0, timing.totalSec - timing.readSec);
                    {
                        std::lock_guard<std::mutex> lock(timingMutex);
                        folderTimings.push_back(timing);
                    }
                    size_t completed = ++completedFolders;
                    if (hasInfoCallback) {
                        float progress = static_cast<float>(completed) / totalFolders;
                        logInfo("Progress: " + std::to_string(completed) + "/" +
                               std::to_string(totalFolders) + " folders completed (" +
                               std::to_string(static_cast<int>(progress * 100)) + "%)");
                    }
                }
            });
        }

        for (auto* folderTask : indexedTasks) {
            FolderTiming timing;
            bool ok = installWithIndex(*folderTask, timing);
            if (!ok) {
                std::string error = "Failed to decompress folder: " + folderTask->folderName;
                logError(error);
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
            } else {
                {
                    std::lock_guard<std::mutex> lock(timingMutex);
                    folderTimings.push_back(timing);
                }
                size_t completed = ++completedFolders;
                if (hasInfoCallback) {
                    float progress = static_cast<float>(completed) / folderTasks.size();
                    logInfo("Progress: " + std::to_string(completed) + "/" +
                           std::to_string(folderTasks.size()) + " folders completed (" +
                           std::to_string(static_cast<int>(progress * 100)) + "%)");
                }
            }
        }
    }

    threadPool->waitForAll();

    result.errors = std::move(errors);
    result.success = overallSuccess.load() && result.errors.empty();
    result.timing.indexedReadSec = static_cast<double>(totalReadNs.load()) / 1e9;
    result.timing.indexedDecompressSec = static_cast<double>(totalDecompressNs.load()) / 1e9;
    result.timing.indexedWriteSec = static_cast<double>(totalWriteNs.load()) / 1e9;
    result.timing.legacyTotalSec = static_cast<double>(totalLegacyNs.load()) / 1e9;
    result.timing.folderTimings = std::move(folderTimings);

    return result;
}

} // namespace MultiThreadedInstaller
