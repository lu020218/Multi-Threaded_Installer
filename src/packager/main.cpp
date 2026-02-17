#include "packager/folder_scanner.h"
#include "packager/compression_module.h"
#include "packager/metadata_generator.h"
#include "packager/installer_generator.h"
#include "packager/configuration_manager.h"
#include "packager/icon_updater.h"
#include "packager/version_info_updater.h"
#include "installer/console_interface.h"
#include "common/utf8_utils.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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
    std::cout << "  Place packager.yaml/packager.yml/packager.json/.packager.json in the input directory\n";
    std::cout << "  to configure packaging options. If no configuration file is found,\n";
    std::cout << "  default settings will be used.\n";
}

static fs::path makeTempTemplatePath(const fs::path& outputPath) {
    fs::path baseDir = outputPath.has_parent_path() ? outputPath.parent_path() : fs::path(".");
    std::string baseName = Utf8FromPath(outputPath.filename());
#ifdef _WIN32
    auto pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    auto pid = static_cast<unsigned long>(getpid());
#endif
    return baseDir / (baseName + ".template." + std::to_string(pid) + ".exe");
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
    

    if (!fs::exists(PathFromUtf8(inputPath)) || !fs::is_directory(PathFromUtf8(inputPath))) {
        console.showError("Error: Input directory does not exist: " + inputPath);
        return 1;
    }
    

    fs::path outputFilePath = PathFromUtf8(outputPath);
    if (outputFilePath.has_parent_path()) {
        fs::path parentPath = outputFilePath.parent_path();
        if (!fs::exists(parentPath)) {
            console.showError("Error: Output directory does not exist: " + Utf8FromPath(parentPath));
            return 1;
        }
    }
    
    console.showInfo("Starting packaging process...");
    console.showInfo("Input directory: " + inputPath);
    console.showInfo("Output file: " + outputPath);
    

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
    

    if (configManager.hasConfigFile()) {
        console.showInfo("Using configuration file: " + configManager.getConfigFilePath());
    } else {
        console.showInfo("No configuration file found, using default settings");
    }
    
    console.showInfo("Application name: " + config.applicationName);
    console.showInfo("Default install directory: " + config.defaultInstallDir);
    console.showInfo(std::string("Compression algorithm: LZMA"));
    

    FolderScanner scanner;
    auto folders = scanner.scanInputDirectory(inputPath);
    
    if (!scanner.validateFolderStructure(folders)) {
        console.showError("Invalid folder structure");
        return 1;
    }
    
    console.showInfo("Found " + std::to_string(folders.size()) + " folders to package");
    

    configManager.applyFolderTargets(folders);
    

    for (const auto& folder : folders) {
        if (!folder.targetPath.empty()) {
            console.showInfo("Folder '" + folder.sourcePath + "' will be installed to: " + folder.targetPath);
        }
    }
    

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
    

    MetadataGenerator metadataGen;
    auto extendedMetadata = metadataGen.generateExtendedMetadata(compressionResults, folders, config);
    auto serializedMetadata = metadataGen.serializeExtendedMetadata(extendedMetadata);
    

    InstallerGenerator installerGen;
    std::vector<std::vector<uint8_t>> compressedDataList;
    for (const auto& result : compressionResults) {
        compressedDataList.push_back(result.compressedData);
    }

    fs::path tempTemplatePath;
    bool usesTempTemplate = false;
    if (!config.iconPath.empty() ||
        !config.productName.empty() ||
        !config.fileDescription.empty() ||
        !config.companyName.empty() ||
        !config.copyright.empty() ||
        !config.fileVersion.empty() ||
        !config.productVersion.empty()) {
        std::string baseTemplate = installerGen.findDefaultInstallerTemplatePath();
        if (baseTemplate.empty()) {
            console.showError("Failed to locate installer template for resource updates");
            return 1;
        }

        installerGen.setTemplateResourceDir(PathFromUtf8(baseTemplate).parent_path());

        tempTemplatePath = makeTempTemplatePath(outputFilePath);
        std::error_code copyError;
        fs::copy_file(PathFromUtf8(baseTemplate), tempTemplatePath, fs::copy_options::overwrite_existing, copyError);
        if (copyError) {
            console.showError("Failed to create temporary installer template: " + copyError.message());
            return 1;
        }

        if (!installerGen.embedInstallerTemplate(Utf8FromPath(tempTemplatePath))) {
            console.showError("Failed to use temporary installer template");
            return 1;
        }

        usesTempTemplate = true;

        if (!config.iconPath.empty()) {
            fs::path iconPath = PathFromUtf8(config.iconPath);
            if (!iconPath.is_absolute()) {
                iconPath = PathFromUtf8(inputPath) / iconPath;
            }
            std::string iconError;
            if (UpdateInstallerIcon(Utf8FromPath(tempTemplatePath), Utf8FromPath(iconPath), iconError)) {
                console.showInfo("Applied installer icon: " + Utf8FromPath(iconPath));
            } else {
                console.showWarning("Failed to apply installer icon: " + iconError);
            }
        }

        VersionInfoData versionInfo;
        versionInfo.productName = config.productName.empty() ? config.applicationName : config.productName;
        versionInfo.fileDescription = config.fileDescription.empty()
            ? (config.applicationName + " Installer")
            : config.fileDescription;
        versionInfo.companyName = config.companyName;
        versionInfo.copyright = config.copyright;
        versionInfo.fileVersion = config.fileVersion.empty() ? config.version : config.fileVersion;
        versionInfo.productVersion = config.productVersion.empty() ? config.version : config.productVersion;
        versionInfo.originalFilename = Utf8FromPath(PathFromUtf8(outputPath).filename());

        std::string versionError;
        if (UpdateInstallerVersionInfo(Utf8FromPath(tempTemplatePath), versionInfo, versionError)) {
            console.showInfo("Applied installer version info");
        } else {
            console.showWarning("Failed to apply installer version info: " + versionError);
        }
    }
    
    if (!installerGen.generateInstaller(outputPath, serializedMetadata, compressedDataList)) {
        console.showError("Failed to generate installer");
        if (!installerGen.getLastError().empty()) {
            console.showError("Details: " + installerGen.getLastError());
        }
        return 1;
    }

    if (usesTempTemplate) {
        std::error_code removeError;
        fs::remove(tempTemplatePath, removeError);
        if (removeError) {
            console.showWarning("Failed to remove temporary template: " + removeError.message());
        }
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
