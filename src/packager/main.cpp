#include "packager/folder_scanner.h"
#include "packager/compression_module.h"
#include "packager/metadata_generator.h"
#include "packager/installer_generator.h"
#include "installer/console_interface.h"
#include <iostream>

using namespace MultiThreadedInstaller;

int main(int argc, char* argv[]) {
    ConsoleInterface console;
    
    // 解析命令行参数
    auto args = console.parsePackagerArgs(argc, argv);
    
    if (args.showHelp) {
        console.showPackagerHelp();
        return 0;
    }
    
    // 如果没有提供命令行参数，使用交互模式
    if (args.inputPath.empty() || args.outputPath.empty()) {
        console.showPackagerMenu();
        if (!console.getPackagerInput(args.inputPath, args.outputPath, args.algorithm)) {
            console.showError("Failed to get valid input parameters");
            return 1;
        }
    }
    
    console.showInfo("Starting packaging process...");
    console.showInfo("Input directory: " + args.inputPath);
    console.showInfo("Output file: " + args.outputPath);
    console.showInfo(std::string("Compression algorithm: ") + 
                    (args.algorithm == CompressionAlgorithm::ZSTD_FAST ? "ZSTD" : "LZMA"));
    
    // 扫描输入目录
    FolderScanner scanner;
    auto folders = scanner.scanInputDirectory(args.inputPath);
    
    if (!scanner.validateFolderStructure(folders)) {
        console.showError("Invalid folder structure");
        return 1;
    }
    
    console.showInfo("Found " + std::to_string(folders.size()) + " folders to package");
    
    // 压缩文件夹
    CompressionModule compressor;
    compressor.setCompressionAlgorithm(args.algorithm);
    
    if (args.compressionLevel != -1) {
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
    
    // 生成元数据
    MetadataGenerator metadataGen;
    auto metadata = metadataGen.generateMetadata(compressionResults, folders);
    auto serializedMetadata = metadataGen.serializeMetadata(metadata);
    
    // 生成安装程序
    InstallerGenerator installerGen;
    std::vector<std::vector<uint8_t>> compressedDataList;
    for (const auto& result : compressionResults) {
        compressedDataList.push_back(result.compressedData);
    }
    
    if (!installerGen.generateInstaller(args.outputPath, serializedMetadata, compressedDataList)) {
        console.showError("Failed to generate installer");
        return 1;
    }
    
    console.showInfo("Packaging completed successfully!");
    console.showInfo("Installer created: " + args.outputPath);
    
    return 0;
}