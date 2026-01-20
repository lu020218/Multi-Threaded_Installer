#include "installer/metadata_parser.h"
#include "installer/thread_pool_manager.h"
#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include "installer/console_interface.h"
#include "installer/path_resolver.h"
#include "installer/installer_helpers.h"
#include "installer/install_state_utils.h"
#include "installer/registry_utils.h"
#include "installer/uninstall_manager.h"
#include <iostream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <memory>
#include <io.h>
#include <fcntl.h>

using namespace MultiThreadedInstaller;

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

struct FolderTiming {
    double totalSec = 0.0;
    double readSec = 0.0;
    double decompressSec = 0.0;
    double writeSec = 0.0;
    double processSec = 0.0;
    bool indexed = false;
    std::string folderName;
};

std::vector<std::string> collectFilesRecursive(const std::string& rootPath) {
    std::vector<std::string> files;
    if (rootPath.empty()) {
        return files;
    }
    std::filesystem::path root(rootPath);
    if (!std::filesystem::exists(root)) {
        return files;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }
    return files;
}

} // namespace

int main(int argc, char* argv[]) {
    ConsoleInterface console;
    auto startTime = std::chrono::steady_clock::now();
    
    // 解析命令行参数
    auto args = console.parseInstallerArgs(argc, argv);
    if (!args.uninstall) {
        std::filesystem::path exePath = getCurrentExecutablePath();
        std::string exeName = exePath.filename().string();
        std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
        if (exeName == "uninstall.exe") {
            args.uninstall = true;
        }
    }
    
    if (args.showHelp) {
        console.showInstallerHelp();
        return 0;
    }
    
    if (args.uninstall) {
        console.showInfo("Starting uninstall process...");
        InstallerPathResolver pathResolver;
        std::string exePath = getCurrentExecutablePath();
        std::string localManifest = getLocalManifestPath(exePath);
        if (!localManifest.empty() && std::filesystem::exists(localManifest)) {
            bool ok = uninstallFromManifest(localManifest, pathResolver, console);
            return ok ? 0 : 1;
        }
        std::string fallbackAppName;
        if (!exePath.empty()) {
            std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
            if (!exeDir.empty()) {
                fallbackAppName = exeDir.filename().string();
            }
        }
        if (fallbackAppName.empty()) {
            std::filesystem::path exeName = std::filesystem::path(exePath).filename();
            fallbackAppName = exeName.stem().string();
        }
        if (!fallbackAppName.empty()) {
            std::string manifestPath = getDefaultManifestPath(fallbackAppName, pathResolver);
            if (!manifestPath.empty() && std::filesystem::exists(manifestPath)) {
                bool ok = uninstallFromManifest(manifestPath, pathResolver, console);
                return ok ? 0 : 1;
            }
        }
        MetadataParser parser;
        auto metadata = parser.parseExtendedEmbeddedMetadata();
        if (parser.validateMetadata(metadata)) {
            std::string manifestPath = getDefaultManifestPath(metadata.applicationName, pathResolver);
            if (!manifestPath.empty() && std::filesystem::exists(manifestPath)) {
                bool ok = uninstallFromManifest(manifestPath, pathResolver, console);
                return ok ? 0 : 1;
            }
        }
        console.showError("Manifest not found for uninstall");
        return 1;
    }

    console.showInfo("Starting installation process...");
    
    // 解析嵌入的扩展元数据
    MetadataParser parser;
    if (!args.dataPackagePath.empty()) {
        parser.setDataPackagePath(args.dataPackagePath);
    }
    auto metadata = parser.parseExtendedEmbeddedMetadata();
    
    if (!parser.validateMetadata(metadata)) {
        console.showError("Invalid or corrupted installer metadata");
        return 1;
    }
    
    console.showInfo("Found " + std::to_string(metadata.folderCount) + " folders to install");
    console.showInfo("Application: " + metadata.applicationName);
    
    // 创建路径解析器
    InstallerPathResolver pathResolver;
    HANDLE installMutex = nullptr;
    if (metadata.installState.useMutex) {
        installMutex = acquireInstallMutex(metadata.installState);
    }
    applyInstallState(metadata.installState, "installing", pathResolver);
    
    // 如果没有提供文件夹映射，使用交互模式
    std::string userSelectedPath;
    std::string installRootPath;
    if (args.folderMappings.empty() && args.defaultDestination.empty()) {
        console.showInstallerMenu();
        
        // 显示默认安装目录建议
        std::string defaultPath = pathResolver.expandEnvironmentVariables(metadata.defaultInstallDir);
        console.showInfo("Suggested installation directory: " + defaultPath);
        
        // 获取用户输入的安装目录
        std::cout << "Enter installation directory (or press Enter to use default): ";
        std::getline(std::cin, userSelectedPath);
        
        if (userSelectedPath.empty()) {
            userSelectedPath = defaultPath;
        }
        
        console.showInfo("Installing to: " + userSelectedPath);
    } else if (!args.defaultDestination.empty()) {
        userSelectedPath = args.defaultDestination;
    }
    
    // 创建线程池
    auto threadPool = std::make_shared<ThreadPoolManager>(
        args.threadCount > 0 ? args.threadCount : std::thread::hardware_concurrency()
    );
    
    // 创建解压引擎
    DecompressionEngine decompressor;
    decompressor.setThreadPool(threadPool);
    decompressor.registerProgressCallback([&console](const std::string& folder, float progress) {
        console.showInstallationProgress(folder, progress);
    });
    
    // 创建文件系统操作器
    FileSystemOperator fsOperator;
    
    std::vector<std::string> errors;
    std::mutex errorsMutex;
    std::mutex progressMutex;
    std::atomic<bool> overallSuccess(true);
    std::atomic<size_t> completedFolders(0);
    std::atomic<long long> totalReadNs(0);
    std::atomic<long long> totalDecompressNs(0);
    std::atomic<long long> totalWriteNs(0);
    std::atomic<long long> totalLegacyNs(0);
    std::mutex timingMutex;
    std::vector<FolderTiming> folderTimings;
    
    // 准备所有文件夹的解压任务
    struct FolderTask {
        std::string folderName;
        std::string targetPath;
        ExtendedFolderMapping mapping;
        bool useIndex = false;
        DecompressionTask decompTask;
        double legacyReadSec = 0.0;
        DecompressionEngine::LegacyStageTiming legacyStage;
    };
    
    std::vector<FolderTask> folderTasks;
    folderTasks.reserve(metadata.extendedMappings.size());
    
    // 第一阶段：准备所有任务（路径解析、目录创建、数据读取）
    for (size_t i = 0; i < metadata.extendedMappings.size(); ++i) {
        const auto& mapping = metadata.extendedMappings[i];
        
        // 确定目标路径
        std::string targetPath;
        bool foundMapping = false;
        
        // 首先检查用户是否为此文件夹指定了特定路径
        for (const auto& userMapping : args.folderMappings) {
            if (userMapping.first == mapping.folderName) {
                targetPath = userMapping.second;
                foundMapping = true;
                break;
            }
        }
        
        // 如果没有找到用户映射，使用路径解析器根据目标目录类型解析路径
        if (!foundMapping) {
            std::string basePath;
            if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                // 使用用户选择的安装目录
                basePath = pathResolver.resolveFinalPath(
                    userSelectedPath,
                    mapping.targetDirType,
                    metadata.applicationName
                );
            } else {
                // 使用环境变量路径
                basePath = pathResolver.resolveFinalPath(
                    mapping.customTargetPath.empty() ? mapping.targetPath : mapping.customTargetPath,
                    mapping.targetDirType,
                    metadata.applicationName
                );
            }
            
            // 将文件夹名称附加到基础路径（安装目录不需要额外层级）
            if (!basePath.empty()) {
                if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                    targetPath = basePath;
                } else {
                    // 确保路径以分隔符结尾
                    if (basePath.back() != '\\' && basePath.back() != '/') {
                        basePath += '\\';
                    }
                    targetPath = basePath + mapping.folderName;
                }
            }
        }

        if (installRootPath.empty() && mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
            installRootPath = targetPath;
        }
        
        if (targetPath.empty()) {
            std::string error = "No target path specified for folder: " + mapping.folderName;
            console.showError(error);
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back(error);
            overallSuccess = false;
            continue;
        }
        
        console.showInfo("Installing folder '" + mapping.folderName + "' to: " + targetPath);
        
        // 创建目标目录
        if (!fsOperator.createDirectoryRecursive(targetPath)) {
            std::string error = "Failed to create target directory: " + targetPath;
            console.showError(error);
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back(error);
            overallSuccess = false;
            continue;
        }
        
        // 创建解压任务
        FolderTask folderTask;
        folderTask.folderName = mapping.folderName;
        folderTask.targetPath = targetPath;
        folderTask.mapping = mapping;
        folderTask.useIndex = !mapping.fileIndex.empty() && !mapping.blockIndex.empty();
        if (folderTask.useIndex) {
            console.showInfo("Install path for '" + mapping.folderName + "': indexed");
        } else {
            console.showInfo("Install path for '" + mapping.folderName + "': legacy");
        }
        
        if (!folderTask.useIndex) {
            auto readStart = std::chrono::steady_clock::now();
            std::vector<uint8_t> compressedData = parser.readCompressedData(mapping.offset, mapping.compressedSize);
            auto readEnd = std::chrono::steady_clock::now();
            if (compressedData.empty()) {
                std::string error = "Failed to read compressed data for folder: " + mapping.folderName;
                console.showError(error);
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
    }
    
    auto installWithIndex = [&](const FolderTask& folderTask, FolderTiming& timing) -> bool {
        const auto& mapping = folderTask.mapping;
        if (mapping.fileIndex.empty() || mapping.blockIndex.empty()) {
            console.showError("Indexed metadata missing for '" + folderTask.folderName + "'");
            return false;
        }
        
        timing.indexed = true;
        timing.folderName = folderTask.folderName;
        auto totalStart = std::chrono::steady_clock::now();
        
        std::vector<std::unique_ptr<FileWriter>> writers;
        writers.reserve(mapping.fileIndex.size());
        
        // Create directories once to reduce repeated filesystem work.
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
                console.showError("Failed to create directory: " + dir);
                return false;
            }
        }

        uint64_t totalBytes = 0;
        for (const auto& fileEntry : mapping.fileIndex) {
            std::filesystem::path fullPath = std::filesystem::path(folderTask.targetPath) / fileEntry.relativePath;
            std::string fullPathStr = fullPath.string();
            if (!ensureFileWithSize(fullPath, fileEntry.size, metadata.sparseFileThresholdBytes)) {
                console.showError("Failed to create file: " + fullPathStr);
                return false;
            }
            
            auto writer = std::make_unique<FileWriter>();
            writer->path = std::move(fullPathStr);
            writer->start = fileEntry.offset;
            writer->end = fileEntry.offset + fileEntry.size;
            writers.push_back(std::move(writer));
            totalBytes += fileEntry.size;
        }
        
        std::vector<FileWriter*> writerPtrs;
        writerPtrs.reserve(writers.size());
        for (const auto& writer : writers) {
            writerPtrs.push_back(writer.get());
        }
        if (writerPtrs.empty()) {
            console.showError("No files to write for '" + folderTask.folderName + "'");
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
            console.showInfo("Indexed header read failed for '" + folderTask.folderName + "', using metadata index");
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
            console.showError("No blocks available for '" + folderTask.folderName + "'");
            return false;
        }
        std::sort(blocks.begin(), blocks.end(),
                  [](const BlockInfo& a, const BlockInfo& b) { return a.blockId < b.blockId; });
        
        for (const auto& block : blocks) {
            if (block.compressedOffset + block.compressedSize > mapping.compressedSize) {
                console.showError("Invalid block metadata for '" + folderTask.folderName +
                                  "': block " + std::to_string(block.blockId) +
                                  " out of range");
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
                        console.showError("Indexed read failed for '" + folderTask.folderName +
                                          "': block " + std::to_string(block.blockId));
                        blockFailed.store(true);
                        return false;
                    }
                    
                    auto decompressStart = std::chrono::steady_clock::now();
                    std::vector<uint8_t> decompressed;
                    if (!decompressor.decompressLzmaBlockData(compressedData, block.originalSize, decompressed)) {
                        console.showError("Indexed decompress failed for '" + folderTask.folderName +
                                          "': block " + std::to_string(block.blockId));
                        blockFailed.store(true);
                        return false;
                    }
                    auto decompressEnd = std::chrono::steady_clock::now();
                    decompressNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(decompressEnd - decompressStart).count());
                    
                    uint64_t blockWritten = 0;
                    auto writeStart = std::chrono::steady_clock::now();
                    const auto& segs = segments[i];
                    size_t currentFileIndex = static_cast<size_t>(-1);
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
                            FileWriter* writer = writerPtrs[currentFileIndex];
                            fileLock = std::unique_lock<std::mutex>(writer->mutex);
                            if (!openFileForWrite(writer->path, stream)) {
                                console.showError("Indexed write failed for '" + folderTask.folderName +
                                                  "': block " + std::to_string(block.blockId));
                                blockFailed.store(true);
                                return false;
                            }
                        }
                        stream.seekp(static_cast<std::streamoff>(seg.fileOffset));
                        stream.write(reinterpret_cast<const char*>(decompressed.data() + seg.blockOffset),
                                     static_cast<std::streamsize>(seg.size));
                        if (!stream) {
                            console.showError("Indexed write failed for '" + folderTask.folderName +
                                              "': block " + std::to_string(block.blockId));
                            blockFailed.store(true);
                            return false;
                        }
                        blockWritten += seg.size;
                    }
                    if (stream.is_open()) {
                        stream.close();
                    }
                    if (fileLock.owns_lock()) {
                        if (fileLock.owns_lock()) {
                            fileLock.unlock();
                        }
                    }
                    auto writeEnd = std::chrono::steady_clock::now();
                    writeNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(writeEnd - writeStart).count());
                    
                    if (totalBytes > 0 && blockWritten > 0) {
                        uint64_t current = writtenBytes.fetch_add(blockWritten) + blockWritten;
                        float progress = std::min(0.99f, static_cast<float>(current) / totalBytes);
                        std::lock_guard<std::mutex> lock(progressMutex);
                        console.showInstallationProgress(folderTask.folderName, progress);
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
                    console.showError("Indexed read failed for '" + folderTask.folderName +
                                      "': block " + std::to_string(block.blockId));
                    return false;
                }
                
                auto decompressStart = std::chrono::steady_clock::now();
                std::vector<uint8_t> decompressed;
                if (!decompressor.decompressLzmaBlockData(compressedData, block.originalSize, decompressed)) {
                    console.showError("Indexed decompress failed for '" + folderTask.folderName +
                                      "': block " + std::to_string(block.blockId));
                    return false;
                }
                auto decompressEnd = std::chrono::steady_clock::now();
                decompressNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(decompressEnd - decompressStart).count());
                
                uint64_t blockWritten = 0;
                auto writeStart = std::chrono::steady_clock::now();
                const auto& segs = segments[i];
                size_t currentFileIndex = static_cast<size_t>(-1);
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
                        FileWriter* writer = writerPtrs[currentFileIndex];
                        fileLock = std::unique_lock<std::mutex>(writer->mutex);
                        if (!openFileForWrite(writer->path, stream)) {
                            console.showError("Indexed write failed for '" + folderTask.folderName +
                                              "': block " + std::to_string(block.blockId));
                            return false;
                        }
                    }
                    stream.seekp(static_cast<std::streamoff>(seg.fileOffset));
                    stream.write(reinterpret_cast<const char*>(decompressed.data() + seg.blockOffset),
                                 static_cast<std::streamsize>(seg.size));
                    if (!stream) {
                        console.showError("Indexed write failed for '" + folderTask.folderName +
                                          "': block " + std::to_string(block.blockId));
                        return false;
                    }
                    blockWritten += seg.size;
                }
                if (stream.is_open()) {
                    stream.close();
                }
                if (fileLock.owns_lock()) {
                    fileLock.unlock();
                }
                auto writeEnd = std::chrono::steady_clock::now();
                writeNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(writeEnd - writeStart).count());
                
                if (totalBytes > 0 && blockWritten > 0) {
                    uint64_t current = writtenBytes.fetch_add(blockWritten) + blockWritten;
                    float progress = std::min(0.99f, static_cast<float>(current) / totalBytes);
                    std::lock_guard<std::mutex> lock(progressMutex);
                    console.showInstallationProgress(folderTask.folderName, progress);
                }
            }
        }
        
        console.showInstallationProgress(folderTask.folderName, 1.0f);
        
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
    
    // 第二阶段：并行执行所有解压任务
    if (!folderTasks.empty()) {
        console.showInfo("Decompressing " + std::to_string(folderTasks.size()) + " folders in parallel...");
        
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
            threadPool->enqueue([folderTask, &decompressor, &console, &errors, &errorsMutex, 
                                &overallSuccess, &completedFolders, &totalLegacyNs, &folderTimings, &timingMutex,
                                totalFolders = folderTasks.size()]() {
                auto legacyStart = std::chrono::steady_clock::now();
                bool ok = decompressor.decompressFolder(folderTask->decompTask, &folderTask->legacyStage);
                auto legacyEnd = std::chrono::steady_clock::now();
                long long legacyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(legacyEnd - legacyStart).count();
                totalLegacyNs.fetch_add(legacyNs);
                
                if (!ok) {
                    std::string error = "Failed to decompress folder: " + folderTask->folderName;
                    console.showError(error);
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
                    float progress = static_cast<float>(completed) / totalFolders;
                    console.showInfo("Progress: " + std::to_string(completed) + "/" + 
                                   std::to_string(totalFolders) + " folders completed (" + 
                                   std::to_string(static_cast<int>(progress * 100)) + "%)");
                }
            });
        }
        
        for (auto* folderTask : indexedTasks) {
            FolderTiming timing;
            bool ok = installWithIndex(*folderTask, timing);
            if (!ok) {
                std::string error = "Failed to decompress folder: " + folderTask->folderName;
                console.showError(error);
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
            } else {
                    {
                        std::lock_guard<std::mutex> lock(timingMutex);
                        folderTimings.push_back(timing);
                    }
                size_t completed = ++completedFolders;
                float progress = static_cast<float>(completed) / folderTasks.size();
                console.showInfo("Progress: " + std::to_string(completed) + "/" + 
                               std::to_string(folderTasks.size()) + " folders completed (" + 
                               std::to_string(static_cast<int>(progress * 100)) + "%)");
            }
        }
    }
    
    // 等待所有任务完成
    threadPool->waitForAll();
    
    // 显示安装结果
    console.showInstallationResult(overallSuccess, errors);
    
    double indexedRead = static_cast<double>(totalReadNs.load()) / 1e9;
    double indexedDecompress = static_cast<double>(totalDecompressNs.load()) / 1e9;
    double indexedWrite = static_cast<double>(totalWriteNs.load()) / 1e9;
    double legacySum = static_cast<double>(totalLegacyNs.load()) / 1e9;
    
    std::cout << "Timing summary: indexed read "
              << std::fixed << std::setprecision(2)
              << indexedRead << "s, indexed decompress "
              << indexedDecompress << "s, indexed write "
              << indexedWrite << "s, legacy total "
              << legacySum << "s" << std::endl;

    for (const auto& timing : folderTimings) {
        if (timing.indexed) {
            std::cout << "Timing (indexed) " << timing.folderName
                      << ": total " << std::fixed << std::setprecision(2) << timing.totalSec
                      << "s, read " << timing.readSec
                      << "s, decompress " << timing.decompressSec
                      << "s, write " << timing.writeSec << "s" << std::endl;
        } else {
            std::cout << "Timing (legacy) " << timing.folderName
                      << ": total " << std::fixed << std::setprecision(2) << timing.totalSec
                      << "s, read " << timing.readSec
                      << "s, decompress " << timing.decompressSec
                      << "s, write " << timing.writeSec
                      << "s, process " << timing.processSec << "s" << std::endl;
        }
    }
    
    if (overallSuccess) {
        if ((metadata.autoStartup || metadata.desktopIcons) && installRootPath.empty()) {
            console.showWarning("Install root not detected; AutoStartup/DesktopIcons skipped");
        }
        
        if (!installRootPath.empty()) {
            std::filesystem::path exePath = findPrimaryExecutable(installRootPath, metadata.applicationName);
            if ((metadata.autoStartup || metadata.desktopIcons) && exePath.empty()) {
                console.showWarning("No executable found for AutoStartup/DesktopIcons");
            } else {
                if (metadata.autoStartup) {
                    if (setAutoStartup(metadata.applicationName, exePath)) {
                        console.showInfo("AutoStartup enabled");
                    } else {
                        console.showWarning("Failed to enable AutoStartup");
                    }
                }
                if (metadata.desktopIcons) {
                    if (createDesktopShortcut(metadata.applicationName, exePath)) {
                        console.showInfo("Desktop icon created");
                    } else {
                        console.showWarning("Failed to create desktop icon");
                    }
                }
            }
        }

        std::vector<std::string> installedFiles;
        for (const auto& folderTask : folderTasks) {
            auto files = collectFilesRecursive(folderTask.targetPath);
            installedFiles.insert(installedFiles.end(), files.begin(), files.end());
        }
        std::sort(installedFiles.begin(), installedFiles.end());
        installedFiles.erase(std::unique(installedFiles.begin(), installedFiles.end()), installedFiles.end());
        
        std::string uninstallPath;
        if (!installRootPath.empty()) {
            std::filesystem::path target = std::filesystem::path(installRootPath) / "uninstall.exe";
            std::string currentExe = getCurrentExecutablePath();
            std::error_code ec;
            if (!currentExe.empty() && std::filesystem::exists(currentExe)) {
                if (createUninstallStub(currentExe, target.string())) {
                    uninstallPath = target.string();
                } else {
                    std::filesystem::copy_file(currentExe, target,
                                               std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec) {
                        console.showWarning("Failed to create uninstall.exe");
                    } else {
                        uninstallPath = target.string();
                    }
                }
            }
        }
        if (!uninstallPath.empty()) {
            installedFiles.erase(std::remove(installedFiles.begin(), installedFiles.end(), uninstallPath),
                                 installedFiles.end());
        }
        
        std::string manifestPath = getDefaultManifestPath(metadata.applicationName, pathResolver);
        if (!writeManifest(manifestPath, metadata.applicationName, metadata.configVersion,
                           installRootPath, installedFiles, metadata.registry,
                           metadata.autoStartup, metadata.desktopIcons,
                           metadata.installState, uninstallPath)) {
            console.showWarning("Failed to write install manifest");
        }
        
        if (!installRootPath.empty()) {
            std::filesystem::path localPath = std::filesystem::path(installRootPath) / "install.manifest.json";
            if (!writeManifest(localPath.string(), metadata.applicationName, metadata.configVersion,
                               installRootPath, installedFiles, metadata.registry,
                               metadata.autoStartup, metadata.desktopIcons,
                               metadata.installState, uninstallPath)) {
                console.showWarning("Failed to write local install manifest");
            }
        }
        
        if (!metadata.registry.empty()) {
            applyRegistryEntries(metadata.registry, installRootPath,
                                 metadata.configVersion, metadata.applicationName);
        }
        
        applyInstallState(metadata.installState, "installed", pathResolver);
        if (installMutex) {
            releaseInstallMutex(installMutex);
            installMutex = nullptr;
        }
        console.showInfo("Installation completed successfully!");
        auto endTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        std::cout << "Total time: " << std::fixed << std::setprecision(2) << elapsed.count()
                  << " seconds" << std::endl;
        return 0;
    } else {
        applyInstallState(metadata.installState, "failed", pathResolver);
        if (installMutex) {
            releaseInstallMutex(installMutex);
            installMutex = nullptr;
        }
        console.showError("Installation completed with errors");
        auto endTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        std::cout << "Total time: " << std::fixed << std::setprecision(2) << elapsed.count()
                  << " seconds" << std::endl;
        return 1;
    }
}
