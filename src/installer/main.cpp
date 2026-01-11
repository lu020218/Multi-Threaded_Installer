#include "installer/metadata_parser.h"
#include "installer/thread_pool_manager.h"
#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include "installer/console_interface.h"
#include <iostream>

using namespace MultiThreadedInstaller;

int main(int argc, char* argv[]) {
    ConsoleInterface console;
    
    // 解析命令行参数
    auto args = console.parseInstallerArgs(argc, argv);
    
    if (args.showHelp) {
        console.showInstallerHelp();
        return 0;
    }
    
    console.showInfo("Starting installation process...");
    
    // 解析嵌入的元数据
    MetadataParser parser;
    auto metadata = parser.parseEmbeddedMetadata();
    
    if (!parser.validateMetadata(metadata)) {
        console.showError("Invalid or corrupted installer metadata");
        return 1;
    }
    
    console.showInfo("Found " + std::to_string(metadata.folderCount) + " folders to install");
    
    // 如果没有提供文件夹映射，使用交互模式
    if (args.folderMappings.empty() && args.defaultDestination.empty()) {
        console.showInstallerMenu();
        if (!console.getInstallationPaths(args.folderMappings)) {
            console.showError("Failed to get installation paths");
            return 1;
        }
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
    bool overallSuccess = true;
    
    // 处理每个文件夹
    for (size_t i = 0; i < metadata.folderMappings.size(); ++i) {
        const auto& mapping = metadata.folderMappings[i];
        
        // 确定目标路径
        std::string targetPath;
        bool foundMapping = false;
        
        for (const auto& userMapping : args.folderMappings) {
            if (userMapping.first == mapping.folderName) {
                targetPath = userMapping.second;
                foundMapping = true;
                break;
            }
        }
        
        if (!foundMapping && !args.defaultDestination.empty()) {
            targetPath = args.defaultDestination + "/" + mapping.folderName;
        }
        
        if (targetPath.empty()) {
            std::string error = "No target path specified for folder: " + mapping.folderName;
            console.showError(error);
            errors.push_back(error);
            overallSuccess = false;
            continue;
        }
        
        // 创建目标目录
        if (!fsOperator.createDirectoryRecursive(targetPath)) {
            std::string error = "Failed to create target directory: " + targetPath;
            console.showError(error);
            errors.push_back(error);
            overallSuccess = false;
            continue;
        }
        
        // 从嵌入数据中读取压缩数据
        std::vector<uint8_t> compressedData = parser.readCompressedData(mapping.offset, mapping.compressedSize);
        if (compressedData.empty()) {
            std::string error = "Failed to read compressed data for folder: " + mapping.folderName;
            console.showError(error);
            errors.push_back(error);
            overallSuccess = false;
            continue;
        }
        
        // 创建解压任务
        DecompressionTask task;
        task.compressedData = compressedData;
        task.targetPath = targetPath;
        task.expectedChecksum = mapping.checksum;
        task.originalSize = mapping.originalSize;
        task.algorithm = mapping.algorithm;
        
        // 执行解压
        if (!decompressor.decompressFolder(task)) {
            std::string error = "Failed to decompress folder: " + mapping.folderName;
            console.showError(error);
            errors.push_back(error);
            overallSuccess = false;
        }
    }
    
    // 等待所有任务完成
    threadPool->waitForAll();
    
    // 显示安装结果
    console.showInstallationResult(overallSuccess, errors);
    
    if (overallSuccess) {
        console.showInfo("Installation completed successfully!");
        return 0;
    } else {
        console.showError("Installation completed with errors");
        return 1;
    }
}