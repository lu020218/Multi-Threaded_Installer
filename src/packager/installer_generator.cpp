#include "packager/installer_generator.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace MultiThreadedInstaller {

bool InstallerGenerator::generateInstaller(const std::string& outputPath,
                                          const std::vector<uint8_t>& metadata,
                                          const std::vector<std::vector<uint8_t>>& compressedData) {
    return createSelfExtractingExecutable(outputPath, metadata, compressedData);
}

bool InstallerGenerator::generateDataPackage(const std::string& outputPath,
                                             const std::vector<uint8_t>& metadata,
                                             const std::vector<std::vector<uint8_t>>& compressedData) {
    try {
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) {
            std::cerr << "Failed to create data package: " << outputPath << std::endl;
            return false;
        }
        
        uint64_t metadataOffset = sizeof(DataPackageHeader);
        uint64_t dataOffset = metadataOffset + metadata.size();
        uint64_t totalDataSize = 0;
        for (const auto& data : compressedData) {
            totalDataSize += data.size();
        }
        
        DataPackageHeader header;
        header.metadataOffset = metadataOffset;
        header.metadataSize = metadata.size();
        header.dataOffset = dataOffset;
        header.dataSize = totalDataSize;
        
        outFile.write(reinterpret_cast<const char*>(&header), sizeof(DataPackageHeader));
        outFile.write(reinterpret_cast<const char*>(metadata.data()), metadata.size());
        for (const auto& data : compressedData) {
            outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        
        outFile.close();
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error creating data package: " << e.what() << std::endl;
        return false;
    }
}

bool InstallerGenerator::embedInstallerTemplate(const std::string& templatePath) {
    // 验证模板文件是否存在
    if (!std::filesystem::exists(templatePath)) {
        std::cerr << "Installer template not found: " << templatePath << std::endl;
        return false;
    }
    
    installerTemplatePath = templatePath;
    return true;
}

bool InstallerGenerator::createSelfExtractingExecutable(const std::string& outputPath,
                                                      const std::vector<uint8_t>& metadata,
                                                      const std::vector<std::vector<uint8_t>>& compressedData) {
    try {
        // 获取安装程序模板
        std::vector<uint8_t> installerTemplate = getDefaultInstallerTemplate();
        if (installerTemplate.empty()) {
            std::cerr << "Failed to get installer template" << std::endl;
            return false;
        }
        
        // 创建输出目录（如果不存在）
        std::filesystem::path outputDir = std::filesystem::path(outputPath).parent_path();
        if (!outputDir.empty() && !std::filesystem::exists(outputDir)) {
            std::filesystem::create_directories(outputDir);
        }
        
        // 创建输出文件
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) {
            std::cerr << "Failed to create output file: " << outputPath << std::endl;
            return false;
        }
        
        // 计算数据偏移量
        uint64_t executableSize = installerTemplate.size();
        uint64_t metadataOffset = executableSize;
        uint64_t dataOffset = metadataOffset + metadata.size();
        
        // 创建数据定位结构
        DataLocator locator;
        locator.magic = Constants::MAGIC_NUMBER;
        locator.metadataOffset = metadataOffset;
        locator.metadataSize = metadata.size();
        locator.dataOffset = dataOffset;
        
        // 计算总数据大小
        uint64_t totalDataSize = 0;
        for (const auto& data : compressedData) {
            totalDataSize += data.size();
        }
        locator.dataSize = totalDataSize;
        
        // 写入安装程序可执行文件
        outFile.write(reinterpret_cast<const char*>(installerTemplate.data()), installerTemplate.size());
        
        // 写入元数据
        outFile.write(reinterpret_cast<const char*>(metadata.data()), metadata.size());
        
        // 写入压缩数据
        for (const auto& data : compressedData) {
            outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        
        // 写入数据定位器（在文件末尾）
        outFile.write(reinterpret_cast<const char*>(&locator), sizeof(DataLocator));
        
        // 写入定位器魔数（用于从文件末尾查找）
        uint32_t endMagic = Constants::MAGIC_NUMBER;
        outFile.write(reinterpret_cast<const char*>(&endMagic), sizeof(uint32_t));
        
        outFile.close();
        
        // 设置可执行权限
        if (!setExecutablePermissions(outputPath)) {
            std::cerr << "Warning: Failed to set executable permissions" << std::endl;
        }
        
        // 复制必需的运行时文件（DLL和resources）
        if (!copyRuntimeDependencies(outputPath)) {
            std::cerr << "Warning: Failed to copy some runtime dependencies" << std::endl;
        }
        
        std::cout << "Successfully created installer: " << outputPath << std::endl;
        std::cout << "  Executable size: " << executableSize << " bytes" << std::endl;
        std::cout << "  Metadata size: " << metadata.size() << " bytes" << std::endl;
        std::cout << "  Compressed data size: " << totalDataSize << " bytes" << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error creating installer: " << e.what() << std::endl;
        return false;
    }
}

std::vector<uint8_t> InstallerGenerator::getDefaultInstallerTemplate() {
    // 如果指定了模板路径，使用该模板
    if (!installerTemplatePath.empty()) {
        return loadInstallerTemplate(installerTemplatePath);
    }
    
    // 尝试查找预编译的安装程序可执行文件
    std::vector<std::string> possiblePaths = {
        "build/Release/installer.exe",  // Windows Release build
        "build/Debug/installer.exe",    // Windows Debug build
        "build/installer.exe",          // Windows build directory
        "installer.exe",                // Windows current directory
        "build/installer",              // Unix build directory
        "installer",                    // Unix current directory
        "./build/Release/installer.exe", // Relative Windows Release
        "./build/Debug/installer.exe",   // Relative Windows Debug
        "./installer.exe",              // Relative Windows current
        "./installer"                   // Relative Unix current
    };
    
    for (const auto& path : possiblePaths) {
        if (std::filesystem::exists(path)) {
            std::cout << "Using installer template: " << path << std::endl;
            return loadInstallerTemplate(path);
        }
    }
    
    // 如果找不到预编译的安装程序，创建一个最小的占位符
    std::cerr << "Warning: No installer template found, creating placeholder" << std::endl;
    return createPlaceholderTemplate();
}

std::vector<uint8_t> InstallerGenerator::loadInstallerTemplate(const std::string& templatePath) {
    try {
        std::ifstream file(templatePath, std::ios::binary | std::ios::ate);
        if (!file) {
            std::cerr << "Failed to open installer template: " << templatePath << std::endl;
            return {};
        }
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            std::cerr << "Failed to read installer template: " << templatePath << std::endl;
            return {};
        }
        
        return buffer;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading installer template: " << e.what() << std::endl;
        return {};
    }
}

std::vector<uint8_t> InstallerGenerator::createPlaceholderTemplate() {
    // 创建一个最小的可执行文件占位符
    // 这在实际使用中应该被真正的安装程序可执行文件替换
    std::string placeholder = 
        "#!/bin/bash\n"
        "# Multi-threaded Installer Placeholder\n"
        "# This is a placeholder installer template.\n"
        "# In production, this should be replaced with the actual installer executable.\n"
        "echo \"Installer placeholder - data embedded below\"\n"
        "exit 1\n";
    
    return std::vector<uint8_t>(placeholder.begin(), placeholder.end());
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

bool InstallerGenerator::appendDataToExecutable(const std::string& executablePath,
                                               const std::vector<uint8_t>& data) {
    try {
        std::ofstream file(executablePath, std::ios::binary | std::ios::app);
        if (!file) {
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        return file.good();
        
    } catch (const std::exception& e) {
        std::cerr << "Error appending data to executable: " << e.what() << std::endl;
        return false;
    }
}

bool InstallerGenerator::copyRuntimeDependencies(const std::string& installerPath) {
    try {
        std::filesystem::path installerFile(installerPath);
        std::filesystem::path outputDir = installerFile.parent_path();
        
        // 如果输出目录为空，使用当前目录
        if (outputDir.empty()) {
            outputDir = ".";
        }
        
        bool allSuccess = true;
        
        // 查找模板安装程序的目录（通常是 build/Release 或 build/Debug）
        std::filesystem::path templateDir;
        if (!installerTemplatePath.empty()) {
            templateDir = std::filesystem::path(installerTemplatePath).parent_path();
        } else {
            // 尝试常见的构建目录
            std::vector<std::string> possibleDirs = {
                "build/Release",
                "build/Debug",
                "build",
                "."
            };
            
            for (const auto& dir : possibleDirs) {
                // Prefer directories that contain an installer template.
                if (std::filesystem::exists(std::filesystem::path(dir) / "installer.exe") ||
                    std::filesystem::exists(std::filesystem::path(dir) / "installer")) {
                    templateDir = dir;
                    break;
                }
            }
        }
        
        if (templateDir.empty()) {
            std::cerr << "Warning: Could not find template directory with runtime dependencies" << std::endl;
            return false;
        }
        
        std::cout << "Copying runtime dependencies from: " << templateDir << std::endl;
        
        // 复制 DuiLib.dll（仅在使用动态库时）
        // 注意：如果使用静态库编译，则不需要复制DuiLib.dll
        std::filesystem::path duilib = templateDir / "DuiLib.dll";
        if (std::filesystem::exists(duilib)) {
            std::filesystem::path dest = outputDir / "DuiLib.dll";
            try {
                std::filesystem::copy_file(duilib, dest, std::filesystem::copy_options::overwrite_existing);
                std::cout << "  Copied: DuiLib.dll" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "  Failed to copy DuiLib.dll: " << e.what() << std::endl;
                // 不设置allSuccess=false，因为可能使用静态库
                std::cout << "  Note: If using static DuiLib, this is expected" << std::endl;
            }
        } else {
            std::cout << "  DuiLib.dll not found - assuming static linking" << std::endl;
        }
        
        // 复制 resources 目录
        std::filesystem::path resourcesDir = templateDir / "resources";
        if (std::filesystem::exists(resourcesDir) && std::filesystem::is_directory(resourcesDir)) {
            std::filesystem::path destResources = outputDir / "resources";
            try {
                // 如果目标目录存在，先删除
                if (std::filesystem::exists(destResources)) {
                    std::filesystem::remove_all(destResources);
                }
                
                // 递归复制整个 resources 目录
                std::filesystem::copy(resourcesDir, destResources, 
                                     std::filesystem::copy_options::recursive);
                std::cout << "  Copied: resources/ directory" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "  Failed to copy resources directory: " << e.what() << std::endl;
                allSuccess = false;
            }
        } else {
            std::cerr << "  Warning: resources directory not found at " << resourcesDir << std::endl;
            allSuccess = false;
        }
        
        return allSuccess;
        
    } catch (const std::exception& e) {
        std::cerr << "Error copying runtime dependencies: " << e.what() << std::endl;
        return false;
    }
}

} // namespace MultiThreadedInstaller
