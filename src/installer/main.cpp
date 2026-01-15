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

using namespace MultiThreadedInstaller;

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
    std::atomic<bool> overallSuccess(true);
    std::atomic<size_t> completedFolders(0);
    
    // 准备所有文件夹的解压任务
    struct FolderTask {
        std::string folderName;
        std::string targetPath;
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
            
            // 将文件夹名称附加到基础路径
            if (!basePath.empty()) {
                // 确保路径以分隔符结尾
                if (basePath.back() != '\\' && basePath.back() != '/') {
                    basePath += '\\';
                }
                targetPath = basePath + mapping.folderName;
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
        
        // 从嵌入数据中读取压缩数据
        std::vector<uint8_t> compressedData = parser.readCompressedData(mapping.offset, mapping.compressedSize);
        if (compressedData.empty()) {
            std::string error = "Failed to read compressed data for folder: " + mapping.folderName;
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
        folderTask.decompTask.compressedData = std::move(compressedData);
        folderTask.decompTask.targetPath = targetPath;
        folderTask.decompTask.expectedChecksum = mapping.checksum;
        folderTask.decompTask.originalSize = mapping.originalSize;
        folderTask.decompTask.algorithm = mapping.algorithm;
        
        folderTasks.push_back(std::move(folderTask));
    }
    
    // 第二阶段：并行执行所有解压任务
    if (!folderTasks.empty()) {
        console.showInfo("Decompressing " + std::to_string(folderTasks.size()) + " folders in parallel...");
        
        for (auto& folderTask : folderTasks) {
            threadPool->enqueue([&folderTask, &decompressor, &console, &errors, &errorsMutex, 
                                &overallSuccess, &completedFolders, totalFolders = folderTasks.size()]() {
                // 执行解压
                if (!decompressor.decompressFolder(folderTask.decompTask)) {
                    std::string error = "Failed to decompress folder: " + folderTask.folderName;
                    console.showError(error);
                    std::lock_guard<std::mutex> lock(errorsMutex);
                    errors.push_back(error);
                    overallSuccess = false;
                } else {
                    // 更新进度
                    size_t completed = ++completedFolders;
                    float progress = static_cast<float>(completed) / totalFolders;
                    console.showInfo("Progress: " + std::to_string(completed) + "/" + 
                                   std::to_string(totalFolders) + " folders completed (" + 
                                   std::to_string(static_cast<int>(progress * 100)) + "%)");
                }
            });
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
