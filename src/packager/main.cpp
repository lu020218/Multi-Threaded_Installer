#include "packager/folder_scanner.h"
#include "packager/compression_module.h"
#include "packager/metadata_generator.h"
#include "packager/package_manifest_builder.h"
#include "packager/installer_generator.h"
#include "packager/configuration_manager.h"
#include "packager/icon_updater.h"
#include "packager/version_info_updater.h"
#include "common/package_manifest_codec.h"
#include "installer/console_interface.h"
#include "common/utf8_utils.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace MultiThreadedInstaller;
namespace fs = std::filesystem;

namespace {

const char* CompressionAlgorithmName(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::LZMA2_XZ:
            return "XZ/LZMA2";
        case CompressionAlgorithm::ZSTD:
            return "ZSTD";
        default:
            return "Unknown";
    }
}

size_t ResolveCompressionThreadBudget(int requestedThreadCount) {
    if (requestedThreadCount > 0) {
        return static_cast<size_t>(requestedThreadCount);
    }
    unsigned int hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1u : static_cast<size_t>(hw);
}

double ElapsedSeconds(std::chrono::steady_clock::time_point start,
                      std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double>(end - start).count();
}

struct PackagerStageTimings {
    double configLoadSec = 0.0;
    double scanSec = 0.0;
    double compressSec = 0.0;
    double metadataSec = 0.0;
    double templatePrepSec = 0.0;
    double installerWriteSec = 0.0;
    double dataPackageWriteSec = 0.0;
    double cleanupSec = 0.0;
};

void PrintTimingSummary(const PackagerStageTimings& timings,
                        const std::vector<FolderInfo>& folders,
                        const std::vector<double>& folderCompressionSec,
                        double totalSec) {
    std::cout << "\nTiming summary:\n";
    std::cout << "  Config load:    " << std::fixed << std::setprecision(2)
              << timings.configLoadSec << "s\n";
    std::cout << "  Scan input:     " << timings.scanSec << "s\n";
    std::cout << "  Compress:       " << timings.compressSec << "s\n";
    std::cout << "  Metadata:       " << timings.metadataSec << "s\n";
    std::cout << "  Template prep:  " << timings.templatePrepSec << "s\n";
    std::cout << "  Installer write:" << timings.installerWriteSec << "s\n";
    if (timings.dataPackageWriteSec > 0.0) {
        std::cout << "  Data package:   " << timings.dataPackageWriteSec << "s\n";
    }
    if (timings.cleanupSec > 0.0) {
        std::cout << "  Cleanup:        " << timings.cleanupSec << "s\n";
    }
    std::cout << "  Total:          " << totalSec << "s\n";

    if (folders.empty() || folderCompressionSec.empty()) {
        return;
    }

    std::vector<size_t> indices;
    indices.reserve(folderCompressionSec.size());
    for (size_t i = 0; i < folderCompressionSec.size(); ++i) {
        indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return folderCompressionSec[a] > folderCompressionSec[b];
    });

    size_t topCount = std::min<size_t>(5, indices.size());
    std::cout << "  Slowest folders:\n";
    for (size_t i = 0; i < topCount; ++i) {
        size_t index = indices[i];
        std::cout << "    " << folders[index].sourcePath << ": " << folderCompressionSec[index]
                  << "s\n";
    }
}

}

void showUsage(const std::string& programName) {
    std::cout << "Usage: " << programName << " <input_directory> <output_file>\n";
    std::cout << "\n";
    std::cout << "Arguments:\n";
    std::cout << "  input_directory  Directory containing files to package\n";
    std::cout << "  output_file      Path for the generated installer executable\n";
    std::cout << "\n";
    std::cout << "Configuration:\n";
    std::cout << "  Place packager.yaml or packager.yml in the input directory\n";
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
    CliSupport console;
    auto startTime = std::chrono::steady_clock::now();
    PackagerStageTimings timings;
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
    auto configStart = std::chrono::steady_clock::now();
    if (!configManager.initialize(inputPath)) {
        console.showError("Failed to initialize configuration");
        std::string error = configManager.getLastError();
        if (!error.empty()) {
            console.showError("Configuration error: " + error);
        }
        return 1;
    }
    timings.configLoadSec = ElapsedSeconds(configStart, std::chrono::steady_clock::now());
    
    const auto& config = configManager.getConfiguration();
    

    if (configManager.hasConfigFile()) {
        console.showInfo("Using configuration file: " + configManager.getConfigFilePath());
    } else {
        console.showInfo("No configuration file found, using default settings");
    }
    
    console.showInfo("Application name: " + config.app.name);
    console.showInfo("Default install directory: " + config.install.defaultDir);

    CompressionAlgorithm effectiveAlgorithm = config.package.compression.algorithm;
    if (args.algorithmExplicitlySet) {
        effectiveAlgorithm = args.algorithm;
    }
    console.showInfo(std::string("Compression algorithm: ") + CompressionAlgorithmName(effectiveAlgorithm));
    

    FolderScanner scanner;
    auto scanStart = std::chrono::steady_clock::now();
    auto folders = scanner.scanInputDirectory(inputPath);
    
    if (!scanner.validateFolderStructure(folders)) {
        console.showError("Invalid folder structure");
        return 1;
    }
    timings.scanSec = ElapsedSeconds(scanStart, std::chrono::steady_clock::now());
    
    console.showInfo("Found " + std::to_string(folders.size()) + " folders to package");
    

    configManager.applyFolderTargets(folders);
    

    for (const auto& folder : folders) {
        if (!folder.targetPath.empty()) {
            console.showInfo("Folder '" + folder.sourcePath + "' will be installed to: " + folder.targetPath);
        }
    }
    

    CompressionModule compressor;
    if (!compressor.setCompressionAlgorithm(effectiveAlgorithm)) {
        console.showError("Failed to set compression algorithm");
        return 1;
    }

    int effectiveCompressionLevel = -1;
    if (args.compressionLevel >= 0) {
        effectiveCompressionLevel = args.compressionLevel;
    } else if (config.package.compression.level >= 0) {
        effectiveCompressionLevel = config.package.compression.level;
    }
    if (effectiveCompressionLevel >= 0) {
        if (!compressor.setCompressionLevel(effectiveCompressionLevel)) {
            console.showError("Invalid compression level for selected algorithm: " +
                              std::to_string(effectiveCompressionLevel));
            return 1;
        }
    }

    int configuredThreadCount = args.threadCount > 0 ? args.threadCount : config.package.compression.threads;
    size_t compressionThreadBudget = ResolveCompressionThreadBudget(configuredThreadCount);
    size_t folderWorkerCount = std::min(compressionThreadBudget, folders.size());
    int perCompressorThreadCount = static_cast<int>(compressionThreadBudget);
    if (folderWorkerCount > 1) {
        perCompressorThreadCount = static_cast<int>(
            std::max<size_t>(1, compressionThreadBudget / folderWorkerCount));
    }
    if (!compressor.setThreadCount(perCompressorThreadCount)) {
        console.showError("Invalid compression thread count");
        return 1;
    }

    std::vector<CompressionResult> compressionResults(folders.size());
    std::vector<double> folderCompressionSec(folders.size(), 0.0);

    auto configureCompressor = [&](CompressionModule& instance) -> bool {
        if (!instance.setCompressionAlgorithm(effectiveAlgorithm)) {
            return false;
        }
        if (effectiveCompressionLevel >= 0 &&
            !instance.setCompressionLevel(effectiveCompressionLevel)) {
            return false;
        }
        return instance.setThreadCount(perCompressorThreadCount);
    };

    auto compressStart = std::chrono::steady_clock::now();
    if (folderWorkerCount <= 1) {
        for (size_t i = 0; i < folders.size(); ++i) {
            const auto& folder = folders[i];
            console.showPackagingProgress(folder.sourcePath, static_cast<float>(i) / folders.size());

            auto folderStart = std::chrono::steady_clock::now();
            auto result = compressor.compressFolder(folder);
            if (result.compressedData.empty()) {
                console.showError("Failed to compress folder: " + folder.sourcePath);
                return 1;
            }

            compressionResults[i] = std::move(result);
            folderCompressionSec[i] =
                ElapsedSeconds(folderStart, std::chrono::steady_clock::now());
        }
    } else {
        console.showInfo("Compressing folders in parallel with " +
                         std::to_string(folderWorkerCount) + " workers");

        std::atomic<size_t> nextIndex{0};
        std::atomic<size_t> completed{0};
        std::atomic<bool> failed{false};
        std::mutex failureMutex;
        std::mutex progressMutex;
        std::string failedFolder;

        std::vector<std::thread> workers;
        workers.reserve(folderWorkerCount);
        for (size_t worker = 0; worker < folderWorkerCount; ++worker) {
            workers.emplace_back([&, worker]() {
                CompressionModule workerCompressor;
                if (!configureCompressor(workerCompressor)) {
                    failed.store(true, std::memory_order_relaxed);
                    std::lock_guard<std::mutex> lock(failureMutex);
                    if (failedFolder.empty()) {
                        failedFolder = "compression worker initialization";
                    }
                    return;
                }

                while (!failed.load(std::memory_order_relaxed)) {
                    size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                    if (index >= folders.size()) {
                        break;
                    }

                    auto folderStart = std::chrono::steady_clock::now();
                    auto result = workerCompressor.compressFolder(folders[index]);
                    if (result.compressedData.empty()) {
                        failed.store(true, std::memory_order_relaxed);
                        std::lock_guard<std::mutex> lock(failureMutex);
                        if (failedFolder.empty()) {
                            failedFolder = folders[index].sourcePath;
                        }
                        break;
                    }

                    compressionResults[index] = std::move(result);
                    folderCompressionSec[index] =
                        ElapsedSeconds(folderStart, std::chrono::steady_clock::now());

                    size_t done = completed.fetch_add(1, std::memory_order_relaxed) + 1;
                    std::lock_guard<std::mutex> lock(progressMutex);
                    console.showPackagingProgress(
                        folders[index].sourcePath,
                        static_cast<float>(done) / static_cast<float>(folders.size()));
                }
            });
        }

        for (auto& worker : workers) {
            worker.join();
        }

        if (failed.load(std::memory_order_relaxed)) {
            console.showError("Failed to compress folder: " + failedFolder);
            return 1;
        }
    }
    timings.compressSec = ElapsedSeconds(compressStart, std::chrono::steady_clock::now());
    
    console.showPackagingProgress("Finalizing", 1.0f);
    

    auto metadataStart = std::chrono::steady_clock::now();
    PackageManifestBuilder manifestBuilder;
    PackageManifest manifest = manifestBuilder.build(compressionResults, folders, config);
    auto serializedMetadata = SerializePackageManifest(manifest);
    timings.metadataSec = ElapsedSeconds(metadataStart, std::chrono::steady_clock::now());
    

    InstallerGenerator installerGen;

    fs::path tempTemplatePath;
    bool usesTempTemplate = false;
    auto templateStart = std::chrono::steady_clock::now();
    if (!config.app.product.iconPath.empty() ||
        !config.app.product.productName.empty() ||
        !config.app.product.fileDescription.empty() ||
        !config.app.product.companyName.empty() ||
        !config.app.product.copyright.empty() ||
        !config.app.product.fileVersion.empty() ||
        !config.app.product.productVersion.empty()) {
        std::string baseTemplate = installerGen.findDefaultInstallerTemplatePath();
        if (baseTemplate.empty()) {
            console.showError("Failed to locate installer template for resource updates");
            return 1;
        }

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

        if (!config.app.product.iconPath.empty()) {
            fs::path iconPath = PathFromUtf8(config.app.product.iconPath);
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
        versionInfo.productName =
            config.app.product.productName.empty() ? config.app.name : config.app.product.productName;
        versionInfo.fileDescription = config.app.product.fileDescription.empty()
            ? (config.app.name + " Installer")
            : config.app.product.fileDescription;
        versionInfo.companyName = config.app.product.companyName;
        versionInfo.copyright = config.app.product.copyright;
        versionInfo.fileVersion =
            config.app.product.fileVersion.empty() ? config.app.version : config.app.product.fileVersion;
        versionInfo.productVersion =
            config.app.product.productVersion.empty() ? config.app.version : config.app.product.productVersion;
        versionInfo.originalFilename = Utf8FromPath(PathFromUtf8(outputPath).filename());

        std::string versionError;
        if (UpdateInstallerVersionInfo(Utf8FromPath(tempTemplatePath), versionInfo, versionError)) {
            console.showInfo("Applied installer version info");
        } else {
            console.showWarning("Failed to apply installer version info: " + versionError);
        }
    }
    timings.templatePrepSec = ElapsedSeconds(templateStart, std::chrono::steady_clock::now());
    
    auto installerWriteStart = std::chrono::steady_clock::now();
    if (!installerGen.generateInstaller(outputPath, serializedMetadata, compressionResults)) {
        console.showError("Failed to generate installer");
        if (!installerGen.getLastError().empty()) {
            console.showError("Details: " + installerGen.getLastError());
        }
        return 1;
    }
    timings.installerWriteSec = ElapsedSeconds(
        installerWriteStart, std::chrono::steady_clock::now());

    if (usesTempTemplate) {
        auto cleanupStart = std::chrono::steady_clock::now();
        std::error_code removeError;
        fs::remove(tempTemplatePath, removeError);
        if (removeError) {
            console.showWarning("Failed to remove temporary template: " + removeError.message());
        }
        timings.cleanupSec += ElapsedSeconds(cleanupStart, std::chrono::steady_clock::now());
    }
    
    if (!args.dataPackagePath.empty()) {
        auto dataPackageStart = std::chrono::steady_clock::now();
        if (!installerGen.generateDataPackage(args.dataPackagePath,
                                              serializedMetadata,
                                              compressionResults)) {
            console.showError("Failed to generate data package");
            return 1;
        }
        timings.dataPackageWriteSec = ElapsedSeconds(
            dataPackageStart, std::chrono::steady_clock::now());
        console.showInfo("Data package created: " + args.dataPackagePath);
    }
    
    console.showInfo("Packaging completed successfully!");
    console.showInfo("Installer created: " + outputPath);
    
    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    PrintTimingSummary(timings, folders, folderCompressionSec, elapsed.count());
    
    return 0;
}
