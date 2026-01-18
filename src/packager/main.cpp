#include "packager/folder_scanner.h"
#include "packager/compression_module.h"
#include "packager/metadata_generator.h"
#include "packager/installer_generator.h"
#include "packager/configuration_manager.h"
#include "installer/console_interface.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>

using namespace MultiThreadedInstaller;
namespace fs = std::filesystem;

void showUsage(const std::string& programName) {
    std::cout << "Usage: " << programName << " <input_directory> <output_file>\n";
    std::cout << "\n";
    std::cout << "Arguments:\n";
    std::cout << "  input_directory  Directory containing files to package\n";
    std::cout << "  output_file      Path for the generated installer executable\n";
    std::cout << "\n";
    std::cout << "Configuration:\n";
    std::cout << "  Place a packager.json or .packager.json file in the input directory\n";
    std::cout << "  to configure packaging options. If no configuration file is found,\n";
    std::cout << "  default settings will be used.\n";
}

int main(int argc, char* argv[]) {
    ConsoleInterface console;
    auto startTime = std::chrono::steady_clock::now();
    auto args = console.parsePackagerArgs(argc, argv);
    
    if (args.showHelp) {
        console.showPackagerHelp();
        return 0;
    }
    
    if (args.inputPath.empty() || args.outputPath.empty()) {
        console.showError("Error: Missing required arguments");
        showUsage(argv[0]);
        return 1;
    }
    
    std::string inputPath = args.inputPath;
    std::string outputPath = args.outputPath;
    
    // 验证输入目录存在
    if (!fs::exists(inputPath) || !fs::is_directory(inputPath)) {
        console.showError("Error: Input directory does not exist: " + inputPath);
        return 1;
    }
    
    // 验证输出文件路径有效
    fs::path outputFilePath(outputPath);
    if (outputFilePath.has_parent_path()) {
        fs::path parentPath = outputFilePath.parent_path();
        if (!fs::exists(parentPath)) {
            console.showError("Error: Output directory does not exist: " + parentPath.string());
            return 1;
        }
    }
    
    console.showInfo("Starting packaging process...");
    console.showInfo("Input directory: " + inputPath);
    console.showInfo("Output file: " + outputPath);
    
    // 初始化配置管理器
    ConfigurationManager configManager;
    if (!configManager.initialize(inputPath)) {
        console.showError("Failed to initialize configuration");
        std::string error = configManager.getLastError();
        if (!error.empty()) {
            console.showError("Configuration error: " + error);
        }
        return 1;
    }
    
    const auto& config = configManager.getConfiguration();
    
    // 记录配置信息
    if (configManager.hasConfigFile()) {
        console.showInfo("Using configuration file: " + configManager.getConfigFilePath());
    } else {
        console.showInfo("No configuration file found, using default settings");
    }
    
    console.showInfo("Application name: " + config.applicationName);
    console.showInfo("Default install directory: " + config.defaultInstallDir);
    console.showInfo(std::string("Compression algorithm: LZMA"));
    
    // 扫描输入目录
    FolderScanner scanner;
    auto folders = scanner.scanInputDirectory(inputPath);
    
    if (!scanner.validateFolderStructure(folders)) {
        console.showError("Invalid folder structure");
        return 1;
    }
    
    console.showInfo("Found " + std::to_string(folders.size()) + " folders to package");
    
    // 应用文件夹目标配置
    configManager.applyFolderTargets(folders);
    
    // 记录文件夹目标配置
    for (const auto& folder : folders) {
        if (!folder.targetPath.empty()) {
            console.showInfo("Folder '" + folder.sourcePath + "' will be installed to: " + folder.targetPath);
        }
    }
    
    // 压缩文件夹
    CompressionModule compressor;
    compressor.setCompressionAlgorithm(config.compressionAlgorithm);
    if (args.compressionLevel >= 0) {
        compressor.setCompressionLevel(args.compressionLevel);
    }
    
    std::vector<CompressionResult> compressionResults;
    
    for (size_t i = 0; i < folders.size(); ++i) {
        const auto& folder = folders[i];
        console.showPackagingProgress(folder.sourcePath, static_cast<float>(i) / folders.size());
        
        auto result = compressor.compressFolder(folder);
        if (result.compressedData.empty()) {
            console.showError("Failed to compress folder: " + folder.sourcePath);
            return 1;
        }
        
        compressionResults.push_back(result);
    }
    
    console.showPackagingProgress("Finalizing", 1.0f);
    
    // 生成扩展元数据（包含配置信息）
    MetadataGenerator metadataGen;
    auto extendedMetadata = metadataGen.generateExtendedMetadata(compressionResults, folders, config);
    auto serializedMetadata = metadataGen.serializeExtendedMetadata(extendedMetadata);
    
    // 生成安装程序
    InstallerGenerator installerGen;
    std::vector<std::vector<uint8_t>> compressedDataList;
    for (const auto& result : compressionResults) {
        compressedDataList.push_back(result.compressedData);
    }
    
    if (!installerGen.generateInstaller(outputPath, serializedMetadata, compressedDataList)) {
        console.showError("Failed to generate installer");
        return 1;
    }
    
    if (!args.dataPackagePath.empty()) {
        if (!installerGen.generateDataPackage(args.dataPackagePath, serializedMetadata, compressedDataList)) {
            console.showError("Failed to generate data package");
            return 1;
        }
        console.showInfo("Data package created: " + args.dataPackagePath);
    }
    
    console.showInfo("Packaging completed successfully!");
    console.showInfo("Installer created: " + outputPath);
    
    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << elapsed.count()
              << " seconds" << std::endl;
    
    return 0;
}
