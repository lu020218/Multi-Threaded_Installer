#include "installer/metadata_parser.h"
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
    std::vector<uint8_t> embeddedData = readEmbeddedData();
    if (embeddedData.empty()) {
        std::cerr << "No embedded data found" << std::endl;
        return InstallationMetadata{};
    }
    
    return deserializeMetadata(embeddedData);
}

ExtendedInstallationMetadata MetadataParser::parseExtendedEmbeddedMetadata() {
    std::vector<uint8_t> embeddedData = readEmbeddedData();
    if (embeddedData.empty()) {
        std::cerr << "No embedded data found" << std::endl;
        return ExtendedInstallationMetadata{};
    }
    
    return deserializeExtendedMetadata(embeddedData);
}

bool MetadataParser::validateMetadata(const InstallationMetadata& metadata) {
    if (metadata.version != Constants::VERSION) {
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
    
    std::ifstream file(executablePath, std::ios::binary);
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
    
    // 解析头部
    const BinaryMetadata* header = reinterpret_cast<const BinaryMetadata*>(data.data());
    
    if (!validateHeader(*header)) {
        return metadata;
    }
    
    metadata.version = header->version;
    metadata.folderCount = header->folderCount;
    
    // 解析文件夹映射
    size_t offset = sizeof(BinaryMetadata);
    
    // 尝试读取扩展字段（applicationName 和 defaultInstallDir）
    // 这些字段在基本元数据之后
    bool hasExtendedFields = false;
    
    // 首先检查是否有扩展字段标记
    if (offset + sizeof(uint32_t) <= data.size()) {
        uint32_t extendedMarker = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        // 使用特殊标记 0x45585444 ("EXTD") 来标识扩展元数据
        if (extendedMarker == 0x45585444) {
            hasExtendedFields = true;
            offset += sizeof(uint32_t);
            
            // 读取 applicationName
            if (offset + sizeof(uint32_t) <= data.size()) {
                uint32_t appNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
                offset += sizeof(uint32_t);
                
                if (offset + appNameLen <= data.size()) {
                    metadata.applicationName = std::string(reinterpret_cast<const char*>(data.data() + offset), appNameLen);
                    offset += appNameLen;
                }
            }
            
            // 读取 defaultInstallDir
            if (offset + sizeof(uint32_t) <= data.size()) {
                uint32_t installDirLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
                offset += sizeof(uint32_t);
                
                if (offset + installDirLen <= data.size()) {
                    metadata.defaultInstallDir = std::string(reinterpret_cast<const char*>(data.data() + offset), installDirLen);
                    offset += installDirLen;
                }
            }
        }
    }
    
    // 解析文件夹映射（扩展或基本）
    for (uint32_t i = 0; i < header->folderCount; ++i) {
        ExtendedFolderMapping mapping;
        
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
        
        // 如果有扩展字段，读取 targetDirType 和 customTargetPath
        if (hasExtendedFields) {
            if (offset + sizeof(SpecialDirectoryType) <= data.size()) {
                mapping.targetDirType = *reinterpret_cast<const SpecialDirectoryType*>(data.data() + offset);
                offset += sizeof(SpecialDirectoryType);
            }
            
            if (offset + sizeof(uint32_t) <= data.size()) {
                uint32_t customPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
                offset += sizeof(uint32_t);
                
                if (offset + customPathLen <= data.size()) {
                    mapping.customTargetPath = std::string(reinterpret_cast<const char*>(data.data() + offset), customPathLen);
                    offset += customPathLen;
                }
            }
        }
        
        metadata.extendedMappings.push_back(mapping);
        
        // 同时填充基类的 folderMappings 以保持向后兼容
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
    
    // 计算总压缩大小
    metadata.totalCompressedSize = 0;
    for (const auto& mapping : metadata.folderMappings) {
        metadata.totalCompressedSize += mapping.compressedSize;
    }
    
    return metadata;
}

std::vector<uint8_t> MetadataParser::readCompressedData(uint64_t offset, uint64_t size) {
    std::string executablePath = getCurrentExecutablePath();
    if (executablePath.empty()) {
        return {};
    }
    
    std::ifstream file(executablePath, std::ios::binary);
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

bool MetadataParser::validateHeader(const BinaryMetadata& header) {
    if (header.magic != Constants::MAGIC_NUMBER) {
        std::cerr << "Invalid magic number in metadata header" << std::endl;
        return false;
    }
    
    if (header.version != Constants::VERSION) {
        std::cerr << "Unsupported metadata version: " << header.version << std::endl;
        return false;
    }
    
    return true;
}

std::string MetadataParser::getCurrentExecutablePath() {
    #ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::string(buffer);
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