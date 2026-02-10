#include "installer/metadata_parser.h"
#include "common/utf8_utils.h"
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace MultiThreadedInstaller {

InstallationMetadata MetadataParser::parseEmbeddedMetadata() {
    std::vector<uint8_t> embeddedData = dataPackagePath_.empty() ? readEmbeddedData() : readExternalMetadata();
    if (embeddedData.empty()) {
        std::cerr << "No embedded data found" << std::endl;
        return InstallationMetadata{};
    }
    
    return deserializeMetadata(embeddedData);
}

ExtendedInstallationMetadata MetadataParser::parseExtendedEmbeddedMetadata() {
    std::vector<uint8_t> embeddedData = dataPackagePath_.empty() ? readEmbeddedData() : readExternalMetadata();
    if (embeddedData.empty()) {
        std::cerr << "No embedded data found" << std::endl;
        return ExtendedInstallationMetadata{};
    }
    
    return deserializeExtendedMetadata(embeddedData);
}

bool MetadataParser::validateMetadata(const InstallationMetadata& metadata) {
    if (metadata.version != Constants::VERSION && metadata.version != 5) {
        std::cerr << "Unsupported metadata version: " << metadata.version << std::endl;
        return false;
    }
    
    if (metadata.folderCount == 0) {
        std::cerr << "No folders found in metadata" << std::endl;
        return false;
    }
    
    if (metadata.folderMappings.size() != metadata.folderCount) {
        std::cerr << "Folder mapping count mismatch" << std::endl;
        return false;
    }
    
    return true;
}

std::vector<uint8_t> MetadataParser::readEmbeddedData() {
    std::string executablePath = getCurrentExecutablePath();
    if (executablePath.empty()) {
        return {};
    }
    
    std::ifstream file(PathFromUtf8(executablePath), std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open executable file: " << executablePath << std::endl;
        return {};
    }
    
    // 移动到文件末尾
    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    
    // 从文件末尾读取DataLocator结构
    // 文件结构：[executable][metadata][data][DataLocator][magic]
    size_t locatorSize = sizeof(DataLocator) + sizeof(uint32_t); // DataLocator + end magic
    
    if (static_cast<size_t>(fileSize) < locatorSize) {
        std::cerr << "File too small to contain embedded data" << std::endl;
        return {};
    }
    
    // 读取文件末尾的魔数
    file.seekg(-static_cast<std::streamoff>(sizeof(uint32_t)), std::ios::end);
    uint32_t endMagic;
    file.read(reinterpret_cast<char*>(&endMagic), sizeof(uint32_t));
    
    if (endMagic != Constants::MAGIC_NUMBER) {
        std::cerr << "Invalid end magic number, no embedded data found" << std::endl;
        return {};
    }
    
    // 读取DataLocator
    file.seekg(-static_cast<std::streamoff>(locatorSize), std::ios::end);
    DataLocator locator;
    file.read(reinterpret_cast<char*>(&locator), sizeof(DataLocator));
    
    if (locator.magic != Constants::MAGIC_NUMBER) {
        std::cerr << "Invalid locator magic number" << std::endl;
        return {};
    }
    
    // 验证偏移量的合理性
    if (locator.metadataOffset >= static_cast<uint64_t>(fileSize) ||
        locator.metadataOffset + locator.metadataSize > static_cast<uint64_t>(fileSize)) {
        std::cerr << "Invalid metadata offset or size" << std::endl;
        return {};
    }
    
    // 读取元数据
    file.seekg(locator.metadataOffset);
    std::vector<uint8_t> metadata(locator.metadataSize);
    file.read(reinterpret_cast<char*>(metadata.data()), locator.metadataSize);
    
    if (file.gcount() != static_cast<std::streamsize>(locator.metadataSize)) {
        std::cerr << "Failed to read complete metadata" << std::endl;
        return {};
    }
    
    return metadata;
}

InstallationMetadata MetadataParser::deserializeMetadata(const std::vector<uint8_t>& data) {
    InstallationMetadata metadata;
    
    if (data.size() < sizeof(BinaryMetadata)) {
        std::cerr << "Insufficient data for metadata header" << std::endl;
        return metadata;
    }
    
    // 解析头部
    const BinaryMetadata* header = reinterpret_cast<const BinaryMetadata*>(data.data());
    
    if (!validateHeader(*header)) {
        return metadata;
    }
    
    metadata.version = header->version;
    metadata.folderCount = header->folderCount;
    
    // 解析文件夹映射
    size_t offset = sizeof(BinaryMetadata);
    for (uint32_t i = 0; i < header->folderCount; ++i) {
        FolderMapping mapping;
        
        // 检查是否有足够的数据读取数值字段
        if (offset + sizeof(uint64_t) * 3 + sizeof(uint32_t) * 3 > data.size()) {
            std::cerr << "Insufficient data for folder mapping " << i << " numeric fields" << std::endl;
            break;
        }
        
        // 读取数值字段
        mapping.offset = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        offset += sizeof(uint64_t);
        
        mapping.compressedSize = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        offset += sizeof(uint64_t);
        
        mapping.originalSize = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        offset += sizeof(uint64_t);
        
        mapping.checksum = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        
        mapping.algorithm = *reinterpret_cast<const CompressionAlgorithm*>(data.data() + offset);
        offset += sizeof(CompressionAlgorithm);
        
        // 读取文件夹名称
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for folder name length" << std::endl;
            break;
        }
        
        uint32_t folderNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        
        if (offset + folderNameLen > data.size()) {
            std::cerr << "Insufficient data for folder name" << std::endl;
            break;
        }
        
        mapping.folderName = std::string(reinterpret_cast<const char*>(data.data() + offset), folderNameLen);
        offset += folderNameLen;
        
        // 读取目标路径
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for target path length" << std::endl;
            break;
        }
        
        uint32_t targetPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        
        if (offset + targetPathLen > data.size()) {
            std::cerr << "Insufficient data for target path" << std::endl;
            break;
        }
        
        mapping.targetPath = std::string(reinterpret_cast<const char*>(data.data() + offset), targetPathLen);
        offset += targetPathLen;
        
        metadata.folderMappings.push_back(mapping);
    }
    
    // 计算总压缩大小
    metadata.totalCompressedSize = 0;
    for (const auto& mapping : metadata.folderMappings) {
        metadata.totalCompressedSize += mapping.compressedSize;
    }
    
    return metadata;
}

ExtendedInstallationMetadata MetadataParser::deserializeExtendedMetadata(const std::vector<uint8_t>& data) {
    ExtendedInstallationMetadata metadata;
    
    if (data.size() < sizeof(BinaryMetadata)) {
        std::cerr << "Insufficient data for metadata header" << std::endl;
        return metadata;
    }
    
    const BinaryMetadata* header = reinterpret_cast<const BinaryMetadata*>(data.data());
    
    if (!validateHeader(*header)) {
        return metadata;
    }
    
    metadata.version = header->version;
    metadata.folderCount = header->folderCount;
    
    size_t offset = sizeof(BinaryMetadata);
    
    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing extended marker" << std::endl;
        return metadata;
    }
    
    uint32_t extendedMarker = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    if (extendedMarker != 0x45585444) {
        std::cerr << "Invalid extended marker" << std::endl;
        return metadata;
    }
    offset += sizeof(uint32_t);
    
    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing application name length" << std::endl;
        return metadata;
    }
    uint32_t appNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + appNameLen > data.size()) {
        std::cerr << "Insufficient data for application name" << std::endl;
        return metadata;
    }
    metadata.applicationName = std::string(reinterpret_cast<const char*>(data.data() + offset), appNameLen);
    offset += appNameLen;
    
    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install dir length" << std::endl;
        return metadata;
    }
    uint32_t installDirLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + installDirLen > data.size()) {
        std::cerr << "Insufficient data for install dir" << std::endl;
        return metadata;
    }
    metadata.defaultInstallDir = std::string(reinterpret_cast<const char*>(data.data() + offset), installDirLen);
    offset += installDirLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing config version length" << std::endl;
        return metadata;
    }
    uint32_t configVersionLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + configVersionLen > data.size()) {
        std::cerr << "Insufficient data for config version" << std::endl;
        return metadata;
    }
    metadata.configVersion = std::string(reinterpret_cast<const char*>(data.data() + offset), configVersionLen);
    offset += configVersionLen;

    if (header->version >= 10) {
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing web page URL length" << std::endl;
            return metadata;
        }
        uint32_t webUrlLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + webUrlLen > data.size()) {
            std::cerr << "Insufficient data for web page URL" << std::endl;
            return metadata;
        }
        metadata.webPageUrl = std::string(reinterpret_cast<const char*>(data.data() + offset), webUrlLen);
        offset += webUrlLen;
    } else {
        metadata.webPageUrl.clear();
    }

    if (header->version >= 7) {
        size_t flagCount = header->version >= 9 ? 4 : 3;
        if (offset + sizeof(uint8_t) * flagCount > data.size()) {
            std::cerr << "Missing startup/desktop/admin flags" << std::endl;
            return metadata;
        }
        metadata.autoStartup = data[offset] != 0;
        metadata.desktopIcons = data[offset + 1] != 0;
        metadata.requireAdmin = data[offset + 2] != 0;
        metadata.autoCleanOldInstall = header->version >= 9 ? (data[offset + 3] != 0) : false;
        offset += sizeof(uint8_t) * flagCount;
    } else {
        if (offset + sizeof(uint8_t) * 2 > data.size()) {
            std::cerr << "Missing startup/desktop flags" << std::endl;
            return metadata;
        }
        metadata.autoStartup = data[offset] != 0;
        metadata.desktopIcons = data[offset + 1] != 0;
        metadata.requireAdmin = false;
        metadata.autoCleanOldInstall = false;
        offset += sizeof(uint8_t) * 2;
    }

    if (header->version >= 8) {
        if (offset + sizeof(uint16_t) * 2 + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing minimum Windows version" << std::endl;
            return metadata;
        }
        metadata.minWindowsMajor = *reinterpret_cast<const uint16_t*>(data.data() + offset);
        offset += sizeof(uint16_t);
        metadata.minWindowsMinor = *reinterpret_cast<const uint16_t*>(data.data() + offset);
        offset += sizeof(uint16_t);
        metadata.minWindowsBuild = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
    } else {
        metadata.minWindowsMajor = 0;
        metadata.minWindowsMinor = 0;
        metadata.minWindowsBuild = 0;
    }

    if (header->version >= 6) {
        if (offset + sizeof(uint64_t) > data.size()) {
            std::cerr << "Missing sparse file threshold" << std::endl;
            return metadata;
        }
        metadata.sparseFileThresholdBytes = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        offset += sizeof(uint64_t);
    } else {
        metadata.sparseFileThresholdBytes = 4 * 1024 * 1024;
    }

    if (offset + sizeof(uint8_t) * 2 > data.size()) {
        std::cerr << "Missing install state flags" << std::endl;
        return metadata;
    }
    metadata.installState.mode = static_cast<InstallStateMode>(data[offset]);
    metadata.installState.useMutex = data[offset + 1] != 0;
    offset += sizeof(uint8_t) * 2;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install state registry path length" << std::endl;
        return metadata;
    }
    uint32_t regPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + regPathLen > data.size()) {
        std::cerr << "Insufficient data for install state registry path" << std::endl;
        return metadata;
    }
    metadata.installState.registryPath = std::string(reinterpret_cast<const char*>(data.data() + offset), regPathLen);
    offset += regPathLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install state registry key length" << std::endl;
        return metadata;
    }
    uint32_t regKeyLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + regKeyLen > data.size()) {
        std::cerr << "Insufficient data for install state registry key" << std::endl;
        return metadata;
    }
    metadata.installState.registryKey = std::string(reinterpret_cast<const char*>(data.data() + offset), regKeyLen);
    offset += regKeyLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install state file path length" << std::endl;
        return metadata;
    }
    uint32_t filePathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + filePathLen > data.size()) {
        std::cerr << "Insufficient data for install state file path" << std::endl;
        return metadata;
    }
    metadata.installState.filePath = std::string(reinterpret_cast<const char*>(data.data() + offset), filePathLen);
    offset += filePathLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install state mutex name length" << std::endl;
        return metadata;
    }
    uint32_t mutexNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + mutexNameLen > data.size()) {
        std::cerr << "Insufficient data for install state mutex name" << std::endl;
        return metadata;
    }
    metadata.installState.mutexName = std::string(reinterpret_cast<const char*>(data.data() + offset), mutexNameLen);
    offset += mutexNameLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing registry entry count" << std::endl;
        return metadata;
    }
    uint32_t registryCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    metadata.registry.reserve(registryCount);
    for (uint32_t r = 0; r < registryCount; ++r) {
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing registry path length" << std::endl;
            return metadata;
        }
        uint32_t pathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + pathLen > data.size()) {
            std::cerr << "Insufficient data for registry path" << std::endl;
            return metadata;
        }
        RegistryEntry reg;
        reg.path = std::string(reinterpret_cast<const char*>(data.data() + offset), pathLen);
        offset += pathLen;
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing registry key length" << std::endl;
            return metadata;
        }
        uint32_t keyLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + keyLen > data.size()) {
            std::cerr << "Insufficient data for registry key" << std::endl;
            return metadata;
        }
        reg.key = std::string(reinterpret_cast<const char*>(data.data() + offset), keyLen);
        offset += keyLen;
        
        if (offset + sizeof(uint8_t) > data.size()) {
            std::cerr << "Missing registry value type" << std::endl;
            return metadata;
        }
        reg.type = static_cast<RegistryValueType>(data[offset]);
        offset += sizeof(uint8_t);
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing registry value length" << std::endl;
            return metadata;
        }
        uint32_t valueLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + valueLen > data.size()) {
            std::cerr << "Insufficient data for registry value" << std::endl;
            return metadata;
        }
        reg.value = std::string(reinterpret_cast<const char*>(data.data() + offset), valueLen);
        offset += valueLen;
        
        metadata.registry.push_back(std::move(reg));
    }
    
    if (header->version >= 12) {
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing kill process count" << std::endl;
            return metadata;
        }
        uint32_t killCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        metadata.installKillProcesses.reserve(killCount);
        for (uint32_t k = 0; k < killCount; ++k) {
            if (offset + sizeof(uint32_t) > data.size()) {
                std::cerr << "Missing kill process name length" << std::endl;
                return metadata;
            }
            uint32_t nameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            if (offset + nameLen > data.size()) {
                std::cerr << "Insufficient data for kill process name" << std::endl;
                return metadata;
            }
            metadata.installKillProcesses.emplace_back(reinterpret_cast<const char*>(data.data() + offset), nameLen);
            offset += nameLen;
        }
    } else {
        metadata.installKillProcesses.clear();
    }
    for (uint32_t i = 0; i < header->folderCount; ++i) {
        ExtendedFolderMapping mapping;
        
        if (offset + sizeof(uint64_t) * 3 + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for folder mapping numeric fields" << std::endl;
            return metadata;
        }
        
        mapping.offset = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        offset += sizeof(uint64_t);
        mapping.compressedSize = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        offset += sizeof(uint64_t);
        mapping.originalSize = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        offset += sizeof(uint64_t);
        mapping.checksum = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        mapping.algorithm = *reinterpret_cast<const CompressionAlgorithm*>(data.data() + offset);
        offset += sizeof(CompressionAlgorithm);
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for folder name length" << std::endl;
            return metadata;
        }
        uint32_t folderNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + folderNameLen > data.size()) {
            std::cerr << "Insufficient data for folder name" << std::endl;
            return metadata;
        }
        mapping.folderName = std::string(reinterpret_cast<const char*>(data.data() + offset), folderNameLen);
        offset += folderNameLen;
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for target path length" << std::endl;
            return metadata;
        }
        uint32_t targetPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + targetPathLen > data.size()) {
            std::cerr << "Insufficient data for target path" << std::endl;
            return metadata;
        }
        mapping.targetPath = std::string(reinterpret_cast<const char*>(data.data() + offset), targetPathLen);
        offset += targetPathLen;
        
        if (offset + sizeof(SpecialDirectoryType) > data.size()) {
            std::cerr << "Insufficient data for target dir type" << std::endl;
            return metadata;
        }
        mapping.targetDirType = *reinterpret_cast<const SpecialDirectoryType*>(data.data() + offset);
        offset += sizeof(SpecialDirectoryType);
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for custom path length" << std::endl;
            return metadata;
        }
        uint32_t customPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + customPathLen > data.size()) {
            std::cerr << "Insufficient data for custom path" << std::endl;
            return metadata;
        }
        mapping.customTargetPath = std::string(reinterpret_cast<const char*>(data.data() + offset), customPathLen);
        offset += customPathLen;
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for file count" << std::endl;
            return metadata;
        }
        uint32_t fileCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        mapping.fileIndex.reserve(fileCount);
        for (uint32_t f = 0; f < fileCount; ++f) {
            if (offset + sizeof(uint32_t) > data.size()) {
                std::cerr << "Insufficient data for file path length" << std::endl;
                return metadata;
            }
            uint32_t pathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            if (offset + pathLen > data.size()) {
                std::cerr << "Insufficient data for file path" << std::endl;
                return metadata;
            }
            FileIndexEntry fileEntry;
            fileEntry.relativePath = std::string(reinterpret_cast<const char*>(data.data() + offset), pathLen);
            offset += pathLen;
            if (offset + sizeof(uint64_t) * 2 > data.size()) {
                std::cerr << "Insufficient data for file entry" << std::endl;
                return metadata;
            }
            fileEntry.offset = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            fileEntry.size = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            mapping.fileIndex.push_back(std::move(fileEntry));
        }
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for block count" << std::endl;
            return metadata;
        }
        uint32_t blockCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        mapping.blockIndex.reserve(blockCount);
        for (uint32_t b = 0; b < blockCount; ++b) {
            if (offset + sizeof(uint32_t) + sizeof(uint64_t) * 3 + sizeof(uint32_t) > data.size()) {
                std::cerr << "Insufficient data for block entry" << std::endl;
                return metadata;
            }
            BlockIndexEntry blockEntry;
            blockEntry.blockId = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            blockEntry.offset = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            blockEntry.compressedSize = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            blockEntry.originalSize = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            blockEntry.checksum = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            mapping.blockIndex.push_back(std::move(blockEntry));
        }
        
        metadata.extendedMappings.push_back(mapping);
        
        FolderMapping baseMapping;
        baseMapping.folderName = mapping.folderName;
        baseMapping.targetPath = mapping.targetPath;
        baseMapping.offset = mapping.offset;
        baseMapping.compressedSize = mapping.compressedSize;
        baseMapping.originalSize = mapping.originalSize;
        baseMapping.checksum = mapping.checksum;
        baseMapping.algorithm = mapping.algorithm;
        metadata.folderMappings.push_back(baseMapping);
    }
    
    metadata.totalCompressedSize = 0;
    for (const auto& mapping : metadata.folderMappings) {
        metadata.totalCompressedSize += mapping.compressedSize;
    }
    
    return metadata;
}

std::vector<uint8_t> MetadataParser::readCompressedData(uint64_t offset, uint64_t size) {
    if (!dataPackagePath_.empty()) {
        return readExternalCompressedData(offset, size);
    }
    
    std::string executablePath = getCurrentExecutablePath();
    if (executablePath.empty()) {
        return {};
    }
    
    std::ifstream file(PathFromUtf8(executablePath), std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open executable file: " << executablePath << std::endl;
        return {};
    }
    
    // 移动到文件末尾获取文件大小
    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    
    // 从文件末尾读取DataLocator结构
    size_t locatorSize = sizeof(DataLocator) + sizeof(uint32_t);
    
    if (static_cast<size_t>(fileSize) < locatorSize) {
        std::cerr << "File too small to contain embedded data" << std::endl;
        return {};
    }
    
    // 读取DataLocator
    file.seekg(-static_cast<std::streamoff>(locatorSize), std::ios::end);
    DataLocator locator;
    file.read(reinterpret_cast<char*>(&locator), sizeof(DataLocator));
    
    if (locator.magic != Constants::MAGIC_NUMBER) {
        std::cerr << "Invalid locator magic number" << std::endl;
        return {};
    }
    
    // 计算绝对偏移量（相对于数据区域开始）
    uint64_t absoluteOffset = locator.dataOffset + offset;
    
    // 验证偏移量和大小的合理性
    if (absoluteOffset >= static_cast<uint64_t>(fileSize) ||
        absoluteOffset + size > static_cast<uint64_t>(fileSize)) {
        std::cerr << "Invalid data offset or size" << std::endl;
        return {};
    }
    
    // 读取压缩数据
    file.seekg(absoluteOffset);
    std::vector<uint8_t> compressedData(size);
    file.read(reinterpret_cast<char*>(compressedData.data()), size);
    
    if (file.gcount() != static_cast<std::streamsize>(size)) {
        std::cerr << "Failed to read complete compressed data" << std::endl;
        return {};
    }
    
    return compressedData;
}

std::vector<uint8_t> MetadataParser::readExternalMetadata() {
    std::ifstream file(PathFromUtf8(dataPackagePath_), std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open data package: " << dataPackagePath_ << std::endl;
        return {};
    }
    
    DataPackageHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(DataPackageHeader));
    if (!file || header.magic != Constants::DATA_MAGIC_NUMBER) {
        std::cerr << "Invalid data package header" << std::endl;
        return {};
    }
    
    file.seekg(static_cast<std::streamoff>(header.metadataOffset));
    std::vector<uint8_t> metadata(header.metadataSize);
    file.read(reinterpret_cast<char*>(metadata.data()), header.metadataSize);
    
    if (file.gcount() != static_cast<std::streamsize>(header.metadataSize)) {
        std::cerr << "Failed to read complete metadata from data package" << std::endl;
        return {};
    }
    
    return metadata;
}

std::vector<uint8_t> MetadataParser::readExternalCompressedData(uint64_t offset, uint64_t size) {
    std::ifstream file(PathFromUtf8(dataPackagePath_), std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open data package: " << dataPackagePath_ << std::endl;
        return {};
    }
    
    DataPackageHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(DataPackageHeader));
    if (!file || header.magic != Constants::DATA_MAGIC_NUMBER) {
        std::cerr << "Invalid data package header" << std::endl;
        return {};
    }
    
    uint64_t absoluteOffset = header.dataOffset + offset;
    if (absoluteOffset + size > header.dataOffset + header.dataSize) {
        std::cerr << "Invalid data offset or size for data package" << std::endl;
        return {};
    }
    
    file.seekg(static_cast<std::streamoff>(absoluteOffset));
    std::vector<uint8_t> compressedData(size);
    file.read(reinterpret_cast<char*>(compressedData.data()), size);
    
    if (file.gcount() != static_cast<std::streamsize>(size)) {
        std::cerr << "Failed to read complete compressed data from data package" << std::endl;
        return {};
    }
    
    return compressedData;
}

bool MetadataParser::validateHeader(const BinaryMetadata& header) {
    if (header.magic != Constants::MAGIC_NUMBER) {
        std::cerr << "Invalid magic number in metadata header" << std::endl;
        return false;
    }
    
    if (header.version != Constants::VERSION && header.version != 5) {
        std::cerr << "Unsupported metadata version: " << header.version << std::endl;
        return false;
    }
    
    return true;
}

std::string MetadataParser::getCurrentExecutablePath() {
    #ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (len == 0) {
        return std::string();
    }
    return WideToUtf8(std::wstring(buffer, len));
    #else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return std::string(buffer);
    }
    return {};
    #endif
}

} // namespace MultiThreadedInstaller

