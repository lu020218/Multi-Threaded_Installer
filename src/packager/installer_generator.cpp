#include "packager/installer_generator.h"
#include "packager/resource_embedder.h"
#include "packager/template_loader.h"
#include "common/utf8_utils.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <vector>
#include <cstdint>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace MultiThreadedInstaller {

bool InstallerGenerator::generateInstaller(const std::string& outputPath,
                                          const std::vector<uint8_t>& metadata,
                                          const std::vector<CompressionResult>& compressionResults) {
    return createSelfExtractingExecutable(outputPath, metadata, compressionResults);
}

bool InstallerGenerator::generateDataPackage(const std::string& outputPath,
                                             const std::vector<uint8_t>& metadata,
                                             const std::vector<CompressionResult>& compressionResults) {
    try {
        std::ofstream outFile(PathFromUtf8(outputPath), std::ios::binary);
        if (!outFile) {
            std::cerr << "Failed to create data package: " << outputPath << std::endl;
            return false;
        }
        
        uint64_t metadataOffset = sizeof(DataPackageHeader);
        uint64_t dataOffset = metadataOffset + metadata.size();
        uint64_t totalDataSize = 0;
        for (const auto& result : compressionResults) {
            totalDataSize += result.compressedData.size();
        }
        
        DataPackageHeader header;
        header.metadataOffset = metadataOffset;
        header.metadataSize = metadata.size();
        header.dataOffset = dataOffset;
        header.dataSize = totalDataSize;
        
        outFile.write(reinterpret_cast<const char*>(&header), sizeof(DataPackageHeader));
        outFile.write(reinterpret_cast<const char*>(metadata.data()), metadata.size());
        for (const auto& result : compressionResults) {
            outFile.write(reinterpret_cast<const char*>(result.compressedData.data()),
                          static_cast<std::streamsize>(result.compressedData.size()));
        }
        
        outFile.close();
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error creating data package: " << e.what() << std::endl;
        return false;
    }
}

bool InstallerGenerator::embedInstallerTemplate(const std::string& templatePath) {

    if (!std::filesystem::exists(PathFromUtf8(templatePath))) {
        std::cerr << "Installer template not found: " << templatePath << std::endl;
        return false;
    }
    
    installerTemplatePath = templatePath;
    return true;
}

void InstallerGenerator::setResourceDirectory(const std::string& resourceDirectory) {
    resourceDirectoryPath_ = resourceDirectory;
}

std::string InstallerGenerator::findDefaultInstallerTemplatePath() const {
    std::filesystem::path exeDir = GetPackagerExecutableDirectory();
    if (exeDir.empty()) {
        std::cerr << "ERROR: Failed to resolve packager executable directory while locating installer template"
                  << std::endl;
        return {};
    }

    const std::filesystem::path installerPath = GetDefaultInstallerTemplatePath();
    if (std::filesystem::exists(installerPath)) {
        return Utf8FromPath(installerPath);
    }

    std::cerr << "ERROR: Installer template not found. Packager directory: "
              << Utf8FromPath(exeDir) << ", expected template: "
              << Utf8FromPath(installerPath) << std::endl;

    return {};
}

bool InstallerGenerator::createSelfExtractingExecutable(const std::string& outputPath,
                                                      const std::vector<uint8_t>& metadata,
                                                      const std::vector<CompressionResult>& compressionResults) {
    try {
        lastError_.clear();

        const std::filesystem::path templatePath =
            installerTemplatePath.empty() ? GetDefaultInstallerTemplatePath()
                                          : PathFromUtf8(installerTemplatePath);
        if (templatePath.empty()) {
            lastError_ = "Failed to resolve installer template path";
            std::cerr << "ERROR: " << lastError_ << std::endl;
            return false;
        }

        std::vector<uint8_t> installerTemplate;
        std::string templateError;
        if (!LoadInstallerTemplate(templatePath, installerTemplate, templateError)) {
            lastError_ = templateError.empty()
                             ? "Failed to load installer template executable"
                             : templateError;
            std::cerr << "ERROR: " << lastError_ << std::endl;
            return false;
        }

        if (resourceDirectoryPath_.empty()) {
            lastError_ = "UI resources directory is not configured";
            std::cerr << "ERROR: " << lastError_ << std::endl;
            return false;
        }
        std::filesystem::path resourceDir = PathFromUtf8(resourceDirectoryPath_);
        std::filesystem::path uninstallerTemplatePath = GetDefaultUninstallerTemplatePath();
        std::cout << "Resolved UI resources directory: " << Utf8FromPath(resourceDir) << std::endl;
        if (!std::filesystem::exists(resourceDir) || !std::filesystem::is_directory(resourceDir)) {
            lastError_ = "UI resources directory not found: " + Utf8FromPath(resourceDir) +
                         ". Packaging requires embedded UI resources and external resources are disabled.";
            std::cerr << "ERROR: " << lastError_ << std::endl;
            return false;
        }

        std::vector<uint8_t> uninstallerTemplate;
        std::string uninstallerError;
        if (!LoadInstallerTemplate(uninstallerTemplatePath, uninstallerTemplate, uninstallerError)) {
            lastError_ = uninstallerError.empty()
                             ? "Failed to load uninstaller template executable: " +
                                   Utf8FromPath(uninstallerTemplatePath)
                             : uninstallerError;
            std::cerr << "ERROR: " << lastError_ << std::endl;
            return false;
        }

        std::string uninstallerEmbedError;
        if (!AppendEmbeddedResources(uninstallerTemplate,
                                     resourceDir,
                                     {},
                                     "uninstaller template",
                                     uninstallerEmbedError)) {
            lastError_ = uninstallerEmbedError.empty()
                             ? "Failed to embed UI resources into uninstaller template."
                             : uninstallerEmbedError;
            std::cerr << "ERROR: " << lastError_ << std::endl;
            return false;
        }

        std::string embedError;
        if (!AppendEmbeddedResources(installerTemplate,
                                     resourceDir,
                                     uninstallerTemplate,
                                     "installer template",
                                     embedError)) {
            lastError_ = embedError.empty()
                             ? "Failed to embed UI resources from: " + Utf8FromPath(resourceDir) +
                                   ". Packaging aborted because embedded UI resources are required."
                             : embedError;
            std::cerr << "ERROR: " << lastError_ << std::endl;
            return false;
        }
        

        std::filesystem::path outputDir = PathFromUtf8(outputPath).parent_path();
        if (!outputDir.empty() && !std::filesystem::exists(outputDir)) {
            std::filesystem::create_directories(outputDir);
        }
        

        std::ofstream outFile(PathFromUtf8(outputPath), std::ios::binary);
        if (!outFile) {
            std::cerr << "Failed to create output file: " << outputPath << std::endl;
            return false;
        }
        

        uint64_t executableSize = installerTemplate.size();
        uint64_t metadataOffset = executableSize;
        uint64_t dataOffset = metadataOffset + metadata.size();
        

        DataLocator locator;
        locator.magic = Constants::MAGIC_NUMBER;
        locator.metadataOffset = metadataOffset;
        locator.metadataSize = metadata.size();
        locator.dataOffset = dataOffset;
        

        uint64_t totalDataSize = 0;
        for (const auto& result : compressionResults) {
            totalDataSize += result.compressedData.size();
        }
        locator.dataSize = totalDataSize;
        

        outFile.write(reinterpret_cast<const char*>(installerTemplate.data()), installerTemplate.size());
        

        outFile.write(reinterpret_cast<const char*>(metadata.data()), metadata.size());
        

        for (const auto& result : compressionResults) {
            outFile.write(reinterpret_cast<const char*>(result.compressedData.data()),
                          static_cast<std::streamsize>(result.compressedData.size()));
        }
        

        outFile.write(reinterpret_cast<const char*>(&locator), sizeof(DataLocator));
        

        uint32_t endMagic = Constants::MAGIC_NUMBER;
        outFile.write(reinterpret_cast<const char*>(&endMagic), sizeof(uint32_t));
        
        outFile.close();
        

        if (!setExecutablePermissions(outputPath)) {
            std::cerr << "Warning: Failed to set executable permissions" << std::endl;
        }
        

        if (!copyRuntimeDependencies(outputPath)) {
            std::cerr << "Warning: Failed to copy some runtime dependencies" << std::endl;
        }
        
        std::cout << "Successfully created installer: " << outputPath << std::endl;
        std::cout << "  Executable size: " << executableSize << " bytes" << std::endl;
        std::cout << "  Metadata size: " << metadata.size() << " bytes" << std::endl;
        std::cout << "  Compressed data size: " << totalDataSize << " bytes" << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        lastError_ = std::string("Error creating installer: ") + e.what();
        std::cerr << "Error creating installer: " << e.what() << std::endl;
        return false;
    }
}

bool InstallerGenerator::setExecutablePermissions(const std::string& filePath) {
    try {
#ifdef _WIN32
        // Windows doesn't need explicit executable permissions for .exe files
        return true;
#else
        // Unix systems need executable permissions
        if (chmod(filePath.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
            return false;
        }
        return true;
#endif
    } catch (const std::exception& e) {
        std::cerr << "Error setting executable permissions: " << e.what() << std::endl;
        return false;
    }
}

bool InstallerGenerator::copyRuntimeDependencies(const std::string& installerPath) {
    try {
        std::filesystem::path installerFile = PathFromUtf8(installerPath);
        std::filesystem::path outputDir = installerFile.parent_path();
        

        if (outputDir.empty()) {
            outputDir = ".";
        }
        
        bool allSuccess = true;
        
        std::filesystem::path templateDir = GetPackagerExecutableDirectory();
        
        if (templateDir.empty()) {
            std::cerr << "Warning: Could not find template directory with runtime dependencies" << std::endl;
            return false;
        }
        
        std::cout << "Copying runtime dependencies from: " << templateDir << std::endl;
        


        std::filesystem::path duilib = templateDir / "DuiLib.dll";
        if (std::filesystem::exists(duilib)) {
            std::filesystem::path dest = outputDir / "DuiLib.dll";
            try {
                std::filesystem::copy_file(duilib, dest, std::filesystem::copy_options::overwrite_existing);
                std::cout << "  Copied: DuiLib.dll" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "  Failed to copy DuiLib.dll: " << e.what() << std::endl;

                std::cout << "  Note: If using static DuiLib, this is expected" << std::endl;
            }
        } else {
            std::cout << "  DuiLib.dll not found - assuming static linking" << std::endl;
        }
        
        std::cout << "  Skipped resources copy (embedded-only mode)" << std::endl;
        
        return allSuccess;
        
    } catch (const std::exception& e) {
        std::cerr << "Error copying runtime dependencies: " << e.what() << std::endl;
        return false;
    }
}

} // namespace MultiThreadedInstaller
