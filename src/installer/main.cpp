#include "installer/metadata_parser.h"
#include "installer/thread_pool_manager.h"
#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include "installer/console_interface.h"
#include "installer/path_resolver.h"
#include <iostream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <memory>

using namespace MultiThreadedInstaller;

namespace {

struct FileWriter {
    std::string path;
    uint64_t start;
    uint64_t end;
    std::fstream stream;
    std::mutex mutex;
};

struct BlockInfo {
    uint32_t blockId;
    uint64_t compressedOffset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint64_t startOffset;
};

struct BlockSegment {
    size_t fileIndex;
    uint64_t blockOffset;
    uint64_t fileOffset;
    uint64_t size;
};

bool openFileWithSize(const std::string& path, uint64_t size, std::fstream& stream) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!file) {
        std::ofstream create(path, std::ios::binary | std::ios::trunc);
        if (!create) {
            return false;
        }
        create.close();
        file.open(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) {
            return false;
        }
    }
    
    if (size > 0) {
        file.seekp(static_cast<std::streamoff>(size - 1));
        char zero = 0;
        file.write(&zero, 1);
        file.flush();
    }
    
    stream = std::move(file);
    return static_cast<bool>(stream);
}

} // namespace

int main(int argc, char* argv[]) {
    ConsoleInterface console;
    auto startTime = std::chrono::steady_clock::now();
    
    // 解析命令行参数
    auto args = console.parseInstallerArgs(argc, argv);
    
    if (args.showHelp) {
        console.showInstallerHelp();
        return 0;
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
    
    // 如果没有提供文件夹映射，使用交互模式
    std::string userSelectedPath;
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
    
    // 准备所有文件夹的解压任务
    struct FolderTask {
        std::string folderName;
        std::string targetPath;
        ExtendedFolderMapping mapping;
        bool useIndex = false;
        DecompressionTask decompTask;
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
        
        if (!folderTask.useIndex) {
            std::vector<uint8_t> compressedData = parser.readCompressedData(mapping.offset, mapping.compressedSize);
            if (compressedData.empty()) {
                std::string error = "Failed to read compressed data for folder: " + mapping.folderName;
                console.showError(error);
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
                continue;
            }
            
            folderTask.decompTask.compressedData = std::move(compressedData);
            folderTask.decompTask.targetPath = targetPath;
            folderTask.decompTask.expectedChecksum = mapping.checksum;
            folderTask.decompTask.originalSize = mapping.originalSize;
            folderTask.decompTask.algorithm = mapping.algorithm;
        }
        
        folderTasks.push_back(std::move(folderTask));
    }
    
    auto installWithIndex = [&](const FolderTask& folderTask) -> bool {
        const auto& mapping = folderTask.mapping;
        if (mapping.fileIndex.empty() || mapping.blockIndex.empty()) {
            return false;
        }
        
        std::vector<std::unique_ptr<FileWriter>> writers;
        writers.reserve(mapping.fileIndex.size());
        
        uint64_t totalBytes = 0;
        for (const auto& fileEntry : mapping.fileIndex) {
            std::filesystem::path fullPath = std::filesystem::path(folderTask.targetPath) / fileEntry.relativePath;
            FileSystemOperator fsOp;
            std::filesystem::path parent = fullPath.parent_path();
            if (!parent.empty()) {
                if (!fsOp.createDirectoryRecursive(parent.string())) {
                    return false;
                }
            }
            
            std::fstream stream;
            if (!openFileWithSize(fullPath.string(), fileEntry.size, stream)) {
                return false;
            }
            
            auto writer = std::make_unique<FileWriter>();
            writer->path = fullPath.string();
            writer->start = fileEntry.offset;
            writer->end = fileEntry.offset + fileEntry.size;
            writer->stream = std::move(stream);
            writers.push_back(std::move(writer));
            totalBytes += fileEntry.size;
        }
        
        std::vector<FileWriter*> writerPtrs;
        writerPtrs.reserve(writers.size());
        for (const auto& writer : writers) {
            writerPtrs.push_back(writer.get());
        }
        
        std::vector<size_t> fileOrder(writerPtrs.size());
        for (size_t i = 0; i < fileOrder.size(); ++i) {
            fileOrder[i] = i;
        }
        std::sort(fileOrder.begin(), fileOrder.end(),
                  [&](size_t a, size_t b) { return writerPtrs[a]->start < writerPtrs[b]->start; });
        
        std::vector<BlockInfo> blocks;
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
        std::sort(blocks.begin(), blocks.end(),
                  [](const BlockInfo& a, const BlockInfo& b) { return a.blockId < b.blockId; });
        
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
        
        std::atomic<uint64_t> writtenBytes(0);
        
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
                    std::vector<uint8_t> compressedData = parser.readCompressedData(
                        mapping.offset + block.compressedOffset,
                        block.compressedSize
                    );
                    if (compressedData.empty()) {
                        blockFailed.store(true);
                        return false;
                    }
                    
                    std::vector<uint8_t> decompressed;
                    if (!decompressor.decompressLzmaBlockData(compressedData, block.originalSize, decompressed)) {
                        blockFailed.store(true);
                        return false;
                    }
                    
                    uint64_t blockWritten = 0;
                    for (const auto& seg : segments[i]) {
                        FileWriter* writer = writerPtrs[seg.fileIndex];
                        std::lock_guard<std::mutex> lock(writer->mutex);
                        writer->stream.seekp(static_cast<std::streamoff>(seg.fileOffset));
                        writer->stream.write(reinterpret_cast<const char*>(decompressed.data() + seg.blockOffset),
                                             static_cast<std::streamsize>(seg.size));
                        if (!writer->stream) {
                            blockFailed.store(true);
                            return false;
                        }
                        blockWritten += seg.size;
                    }
                    
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
                std::vector<uint8_t> compressedData = parser.readCompressedData(
                    mapping.offset + block.compressedOffset,
                    block.compressedSize
                );
                if (compressedData.empty()) {
                    return false;
                }
                
                std::vector<uint8_t> decompressed;
                if (!decompressor.decompressLzmaBlockData(compressedData, block.originalSize, decompressed)) {
                    return false;
                }
                
                uint64_t blockWritten = 0;
                for (const auto& seg : segments[i]) {
                    FileWriter* writer = writerPtrs[seg.fileIndex];
                    std::lock_guard<std::mutex> lock(writer->mutex);
                    writer->stream.seekp(static_cast<std::streamoff>(seg.fileOffset));
                    writer->stream.write(reinterpret_cast<const char*>(decompressed.data() + seg.blockOffset),
                                         static_cast<std::streamsize>(seg.size));
                    if (!writer->stream) {
                        return false;
                    }
                    blockWritten += seg.size;
                }
                
                if (totalBytes > 0 && blockWritten > 0) {
                    uint64_t current = writtenBytes.fetch_add(blockWritten) + blockWritten;
                    float progress = std::min(0.99f, static_cast<float>(current) / totalBytes);
                    std::lock_guard<std::mutex> lock(progressMutex);
                    console.showInstallationProgress(folderTask.folderName, progress);
                }
            }
        }
        
        for (auto& writer : writers) {
            writer->stream.flush();
        }
        
        console.showInstallationProgress(folderTask.folderName, 1.0f);
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
                                &overallSuccess, &completedFolders, totalFolders = folderTasks.size()]() {
                bool ok = decompressor.decompressFolder(folderTask->decompTask);
                
                if (!ok) {
                    std::string error = "Failed to decompress folder: " + folderTask->folderName;
                    console.showError(error);
                    std::lock_guard<std::mutex> lock(errorsMutex);
                    errors.push_back(error);
                    overallSuccess = false;
                } else {
                    size_t completed = ++completedFolders;
                    float progress = static_cast<float>(completed) / totalFolders;
                    console.showInfo("Progress: " + std::to_string(completed) + "/" + 
                                   std::to_string(totalFolders) + " folders completed (" + 
                                   std::to_string(static_cast<int>(progress * 100)) + "%)");
                }
            });
        }
        
        for (auto* folderTask : indexedTasks) {
            bool ok = installWithIndex(*folderTask);
            if (!ok) {
                std::string error = "Failed to decompress folder: " + folderTask->folderName;
                console.showError(error);
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back(error);
                overallSuccess = false;
            } else {
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
    
    if (overallSuccess) {
        console.showInfo("Installation completed successfully!");
        auto endTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        std::cout << "Total time: " << std::fixed << std::setprecision(2) << elapsed.count()
                  << " seconds" << std::endl;
        return 0;
    } else {
        console.showError("Installation completed with errors");
        auto endTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        std::cout << "Total time: " << std::fixed << std::setprecision(2) << elapsed.count()
                  << " seconds" << std::endl;
        return 1;
    }
}
