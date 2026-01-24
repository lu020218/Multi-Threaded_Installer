#include "packager/installer_generator.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <cctype>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace MultiThreadedInstaller {

namespace {

struct ZipFileEntry {
    std::string name;
    std::filesystem::path path;
};

static uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void appendUint16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

static void appendUint32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

static bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    std::streamsize size = file.tellg();
    if (size < 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size)) {
        return false;
    }
    return true;
}

static void collectResourceFiles(const std::filesystem::path& resourceDir,
                                 std::vector<ZipFileEntry>& outFiles) {
    std::unordered_set<std::string> seen;
    auto addEntry = [&](const std::string& name, const std::filesystem::path& path) {
        if (seen.insert(name).second) {
            outFiles.push_back({name, path});
        }
    };

    auto addDir = [&](const std::filesystem::path& dir, const std::string& prefix,
                      const std::vector<std::string>& extraPrefixes) {
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            return;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string fileName = entry.path().filename().string();
            addEntry(prefix + fileName, entry.path());
            for (const auto& extra : extraPrefixes) {
                addEntry(extra + fileName, entry.path());
            }
        }
    };

    addDir(resourceDir / "skins", "skins/", {""});
    addDir(resourceDir / "images", "images/", {"../images/"});
    addDir(resourceDir / "lang", "lang/", {"../lang/"});

    std::filesystem::path licensePath = resourceDir / "license.txt";
    if (std::filesystem::exists(licensePath) && std::filesystem::is_regular_file(licensePath)) {
        addEntry("license.txt", licensePath);
        addEntry("../license.txt", licensePath);
    }

    std::sort(outFiles.begin(), outFiles.end(),
              [](const ZipFileEntry& a, const ZipFileEntry& b) {
                  return a.name < b.name;
              });
}

static bool buildResourceZip(const std::filesystem::path& resourceDir,
                             std::vector<uint8_t>& outZip) {
    std::vector<ZipFileEntry> files;
    collectResourceFiles(resourceDir, files);
    if (files.empty()) {
        return false;
    }

    struct CentralEntry {
        std::string name;
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
    };

    std::vector<CentralEntry> central;
    outZip.clear();

    for (const auto& entry : files) {
        std::vector<uint8_t> data;
        if (!readFileBytes(entry.path, data)) {
            return false;
        }

        uint32_t crc = crc32(data.data(), data.size());
        uint32_t size = static_cast<uint32_t>(data.size());
        uint32_t offset = static_cast<uint32_t>(outZip.size());
        std::string name = entry.name;

        appendUint32(outZip, 0x04034b50);
        appendUint16(outZip, 20);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint32(outZip, crc);
        appendUint32(outZip, size);
        appendUint32(outZip, size);
        appendUint16(outZip, static_cast<uint16_t>(name.size()));
        appendUint16(outZip, 0);
        outZip.insert(outZip.end(), name.begin(), name.end());
        outZip.insert(outZip.end(), data.begin(), data.end());

        central.push_back({name, crc, size, offset});
    }

    uint32_t centralOffset = static_cast<uint32_t>(outZip.size());
    for (const auto& entry : central) {
        appendUint32(outZip, 0x02014b50);
        appendUint16(outZip, 20);
        appendUint16(outZip, 20);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint32(outZip, entry.crc);
        appendUint32(outZip, entry.size);
        appendUint32(outZip, entry.size);
        appendUint16(outZip, static_cast<uint16_t>(entry.name.size()));
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint32(outZip, 0);
        appendUint32(outZip, entry.offset);
        outZip.insert(outZip.end(), entry.name.begin(), entry.name.end());
    }

    uint32_t centralSize = static_cast<uint32_t>(outZip.size() - centralOffset);
    appendUint32(outZip, 0x06054b50);
    appendUint16(outZip, 0);
    appendUint16(outZip, 0);
    appendUint16(outZip, static_cast<uint16_t>(central.size()));
    appendUint16(outZip, static_cast<uint16_t>(central.size()));
    appendUint32(outZip, centralSize);
    appendUint32(outZip, centralOffset);
    appendUint16(outZip, 0);
    return true;
}

} // namespace

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

        std::filesystem::path templateDir = resolveTemplateDirectory();
        bool resourcesEmbedded = false;
        if (!templateDir.empty()) {
            resourcesEmbedded = appendEmbeddedResources(installerTemplate, templateDir / "resources");
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
        if (!copyRuntimeDependencies(outputPath, resourcesEmbedded)) {
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

std::filesystem::path InstallerGenerator::resolveTemplateDirectory() const {
    if (!installerTemplatePath.empty()) {
        return std::filesystem::path(installerTemplatePath).parent_path();
    }

    std::vector<std::string> possibleDirs = {
        "build/Release",
        "build/Debug",
        "build",
        "."
    };

    for (const auto& dir : possibleDirs) {
        if (std::filesystem::exists(std::filesystem::path(dir) / "installer.exe") ||
            std::filesystem::exists(std::filesystem::path(dir) / "installer")) {
            return std::filesystem::path(dir);
        }
    }

    return {};
}

bool InstallerGenerator::hasEmbeddedResourceTable(const std::vector<uint8_t>& installerTemplate) const {
#ifdef _WIN32
    if (installerTemplate.size() < sizeof(IMAGE_DOS_HEADER) + sizeof(uint32_t)) {
        return false;
    }

    auto readAt = [&](uint64_t offset, void* out, size_t bytes) -> bool {
        if (offset + bytes > installerTemplate.size()) {
            return false;
        }
        std::memcpy(out, installerTemplate.data() + offset, bytes);
        return true;
    };

    IMAGE_DOS_HEADER dosHeader{};
    if (!readAt(0, &dosHeader, sizeof(dosHeader))) {
        return false;
    }
    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    uint64_t ntOffset = static_cast<uint64_t>(dosHeader.e_lfanew);
    uint32_t peSignature = 0;
    if (!readAt(ntOffset, &peSignature, sizeof(peSignature))) {
        return false;
    }
    if (peSignature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    IMAGE_FILE_HEADER fileHeader{};
    if (!readAt(ntOffset + sizeof(uint32_t), &fileHeader, sizeof(fileHeader))) {
        return false;
    }

    uint64_t sectionOffset = ntOffset + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                             static_cast<uint64_t>(fileHeader.SizeOfOptionalHeader);
    uint64_t peEnd = sectionOffset;
    for (uint16_t i = 0; i < fileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!readAt(sectionOffset + static_cast<uint64_t>(i) * sizeof(IMAGE_SECTION_HEADER),
                    &section, sizeof(section))) {
            return false;
        }
        uint64_t sectionEnd = static_cast<uint64_t>(section.PointerToRawData) +
                              static_cast<uint64_t>(section.SizeOfRawData);
        if (sectionEnd > peEnd) {
            peEnd = sectionEnd;
        }
    }

    if (peEnd >= installerTemplate.size()) {
        return false;
    }

    const uint32_t magic = 0x52534D45; // "EMSR"

    auto parseTable = [&](uint64_t magicOffset) -> bool {
        if (magicOffset <= peEnd || magicOffset > installerTemplate.size()) {
            return false;
        }

        uint64_t offset = peEnd;
        while (offset < magicOffset) {
            if (offset + sizeof(uint32_t) + sizeof(uint64_t) > magicOffset) {
                return false;
            }

            uint32_t nameLen = 0;
            if (!readAt(offset, &nameLen, sizeof(nameLen))) {
                return false;
            }
            offset += sizeof(nameLen);
            if (nameLen == 0 || offset + nameLen + sizeof(uint64_t) > magicOffset) {
                return false;
            }

            offset += nameLen;

            uint64_t dataLen = 0;
            if (!readAt(offset, &dataLen, sizeof(dataLen))) {
                return false;
            }
            offset += sizeof(dataLen);
            if (dataLen == 0 || offset + dataLen > magicOffset) {
                return false;
            }
            offset += dataLen;
        }

        return offset == magicOffset;
    };

    for (uint64_t i = installerTemplate.size() - sizeof(uint32_t);
         i + sizeof(uint32_t) <= installerTemplate.size();
         --i) {
        uint32_t candidate = 0;
        if (!readAt(i, &candidate, sizeof(candidate))) {
            return false;
        }
        if (candidate == magic && parseTable(i)) {
            return true;
        }
        if (i == 0) {
            break;
        }
    }
#endif
    return false;
}

bool InstallerGenerator::appendEmbeddedResources(std::vector<uint8_t>& installerTemplate,
                                                 const std::filesystem::path& resourceDir) {
    try {
        if (!std::filesystem::exists(resourceDir) || !std::filesystem::is_directory(resourceDir)) {
            return false;
        }

        if (hasEmbeddedResourceTable(installerTemplate)) {
            std::cout << "Installer template already contains embedded resources; appending updated resources" << std::endl;
        }

        auto appendBytes = [&](const void* data, size_t size) {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            installerTemplate.insert(installerTemplate.end(), bytes, bytes + size);
        };

        auto appendEntry = [&](const std::string& name, const std::filesystem::path& filePath) -> bool {
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file) {
                return false;
            }
            std::streamsize size = file.tellg();
            if (size <= 0) {
                return false;
            }
            file.seekg(0, std::ios::beg);

            std::vector<uint8_t> data(static_cast<size_t>(size));
            if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
                return false;
            }

            uint32_t nameLen = static_cast<uint32_t>(name.size());
            uint64_t dataLen = static_cast<uint64_t>(data.size());
            appendBytes(&nameLen, sizeof(nameLen));
            appendBytes(name.data(), name.size());
            appendBytes(&dataLen, sizeof(dataLen));
            appendBytes(data.data(), data.size());
            return true;
        };
        auto appendRawEntry = [&](const std::string& name, const std::vector<uint8_t>& data) -> bool {
            if (data.empty()) {
                return false;
            }
            uint32_t nameLen = static_cast<uint32_t>(name.size());
            uint64_t dataLen = static_cast<uint64_t>(data.size());
            appendBytes(&nameLen, sizeof(nameLen));
            appendBytes(name.data(), name.size());
            appendBytes(&dataLen, sizeof(dataLen));
            appendBytes(data.data(), data.size());
            return true;
        };

        auto toResourceName = [](const std::string& prefix, const std::string& fileName) {
            std::string name = prefix + fileName;
            for (char& ch : name) {
                if (ch == '.') {
                    ch = '_';
                    continue;
                }
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            return name;
        };

        bool anyEmbedded = false;
        bool useZip = false;
        std::vector<uint8_t> zipData;
        if (buildResourceZip(resourceDir, zipData)) {
            if (appendRawEntry("RES_ZIP", zipData)) {
                std::cout << "  Embedded: resources.zip" << std::endl;
                anyEmbedded = true;
                useZip = true;
            }
        }

        std::vector<std::string> imageNames;
        std::vector<std::string> langFiles;
        if (!useZip) {
            std::filesystem::path skinsDir = resourceDir / "skins";
            if (std::filesystem::exists(skinsDir) && std::filesystem::is_directory(skinsDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(skinsDir)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    if (entry.path().extension() != ".xml") {
                        continue;
                    }
                    std::string fileName = entry.path().filename().string();
                    std::string resourceName = toResourceName("XML_", fileName);
                    if (appendEntry(resourceName, entry.path())) {
                        std::cout << "  Embedded: " << fileName << std::endl;
                        anyEmbedded = true;
                    }
                }
            }
            
            std::filesystem::path imagesDir = resourceDir / "images";
            if (std::filesystem::exists(imagesDir) && std::filesystem::is_directory(imagesDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(imagesDir)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    std::string fileName = entry.path().filename().string();
                    if (fileName.empty() || fileName.front() == '.') {
                        continue;
                    }
                    std::string resourceName = toResourceName("IMG_", fileName);
                    if (appendEntry(resourceName, entry.path())) {
                        std::cout << "  Embedded: " << fileName << std::endl;
                        anyEmbedded = true;
                        imageNames.push_back(fileName);
                    }
                }
            }

            std::filesystem::path langDir = resourceDir / "lang";
            if (std::filesystem::exists(langDir) && std::filesystem::is_directory(langDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(langDir)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    std::string fileName = entry.path().filename().string();
                    if (fileName.empty() || fileName.front() == '.') {
                        continue;
                    }
                    std::string resourceName = toResourceName("LANG_", fileName);
                    if (appendEntry(resourceName, entry.path())) {
                        std::cout << "  Embedded: " << fileName << std::endl;
                        anyEmbedded = true;
                        langFiles.push_back(fileName);
                    }
                }
            }
        }

        if (!useZip && !imageNames.empty()) {
            std::string listText;
            for (const auto& name : imageNames) {
                listText += name;
                listText += "\n";
            }
            std::vector<uint8_t> listData(listText.begin(), listText.end());
            if (appendRawEntry("IMAGES_LIST", listData)) {
                anyEmbedded = true;
            }
        }

        if (!useZip && !langFiles.empty()) {
            std::string listText;
            for (const auto& name : langFiles) {
                listText += name;
                listText += "\n";
            }
            std::vector<uint8_t> listData(listText.begin(), listText.end());
            if (appendRawEntry("LANG_LIST", listData)) {
                anyEmbedded = true;
            }
        }

        if (!useZip) {
            std::filesystem::path licensePath = resourceDir / "license.txt";
            if (std::filesystem::exists(licensePath)) {
                if (appendEntry("LICENSE_TXT", licensePath)) {
                    std::cout << "  Embedded: license.txt" << std::endl;
                    anyEmbedded = true;
                }
            }
        }

        if (!anyEmbedded) {
            return false;
        }

        uint32_t magic = 0x52534D45; // "EMSR"
        appendBytes(&magic, sizeof(magic));

        std::cout << "Embedded UI resources into installer template" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to embed UI resources: " << e.what() << std::endl;
        return false;
    }
}

bool InstallerGenerator::copyRuntimeDependencies(const std::string& installerPath, bool resourcesEmbedded) {
    try {
        std::filesystem::path installerFile(installerPath);
        std::filesystem::path outputDir = installerFile.parent_path();
        
        // 如果输出目录为空，使用当前目录
        if (outputDir.empty()) {
            outputDir = ".";
        }
        
        bool allSuccess = true;
        
        std::filesystem::path templateDir = resolveTemplateDirectory();
        
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
        
        if (!resourcesEmbedded) {
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
        } else {
            std::cout << "  Skipped resources copy (embedded)" << std::endl;
        }
        
        return allSuccess;
        
    } catch (const std::exception& e) {
        std::cerr << "Error copying runtime dependencies: " << e.what() << std::endl;
        return false;
    }
}

} // namespace MultiThreadedInstaller
