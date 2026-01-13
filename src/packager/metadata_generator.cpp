#include "packager/metadata_generator.h"
#include <cstring>

namespace MultiThreadedInstaller {

InstallationMetadata MetadataGenerator::generateMetadata(const std::vector<CompressionResult>& results,
                                                       const std::vector<FolderInfo>& folderInfos) {
    InstallationMetadata metadata;
    metadata.version = Constants::VERSION;
    metadata.folderCount = static_cast<uint32_t>(results.size());
    metadata.totalCompressedSize = calculateTotalCompressedSize(results);
    
    uint64_t currentOffset = 0;
    for (size_t i = 0; i < results.size() && i < folderInfos.size(); ++i) {
        FolderMapping mapping = createFolderMapping(results[i], folderInfos[i], currentOffset);
        metadata.folderMappings.push_back(mapping);
        currentOffset += results[i].compressedSize;
    }
    
    return metadata;
}

ExtendedInstallationMetadata MetadataGenerator::generateExtendedMetadata(const std::vector<CompressionResult>& results,
                                                                        const std::vector<FolderInfo>& folderInfos,
                                                                        const PackagerConfiguration& config) {
    ExtendedInstallationMetadata metadata;
    metadata.version = Constants::VERSION;
    metadata.folderCount = static_cast<uint32_t>(results.size());
    metadata.totalCompressedSize = calculateTotalCompressedSize(results);
    
    // 设置配置信息
    metadata.applicationName = config.applicationName;
    metadata.defaultInstallDir = config.defaultInstallDir;
    
    uint64_t currentOffset = 0;
    for (size_t i = 0; i < results.size() && i < folderInfos.size(); ++i) {
        // 创建扩展文件夹映射
        ExtendedFolderMapping extMapping = createExtendedFolderMapping(results[i], folderInfos[i], currentOffset, config);
        metadata.extendedMappings.push_back(extMapping);
        
        // 同时填充基类的folderMappings以保持向后兼容
        FolderMapping baseMapping = createFolderMapping(results[i], folderInfos[i], currentOffset);
        metadata.folderMappings.push_back(baseMapping);
        
        currentOffset += results[i].compressedSize;
    }
    
    return metadata;
}

std::vector<uint8_t> MetadataGenerator::serializeMetadata(const InstallationMetadata& metadata) {
    std::vector<uint8_t> serialized;
    
    // 计算字符串数据的总大小
    size_t stringDataSize = 0;
    for (const auto& mapping : metadata.folderMappings) {
        stringDataSize += mapping.folderName.length() + 1; // +1 for null terminator
        stringDataSize += mapping.targetPath.length() + 1; // +1 for null terminator
    }
    
    // 创建二进制头
    BinaryMetadata header;
    header.magic = Constants::MAGIC_NUMBER;
    header.version = metadata.version;
    header.folderCount = metadata.folderCount;
    header.metadataSize = sizeof(BinaryMetadata) + 
                         metadata.folderMappings.size() * (sizeof(uint64_t) * 4 + sizeof(uint32_t) * 2) + 
                         stringDataSize;
    header.dataOffset = header.metadataSize;
    
    // 序列化头部
    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    serialized.insert(serialized.end(), headerBytes, headerBytes + sizeof(BinaryMetadata));
    
    // 序列化文件夹映射
    for (const auto& mapping : metadata.folderMappings) {
        // 序列化数值字段
        const uint8_t* offsetBytes = reinterpret_cast<const uint8_t*>(&mapping.offset);
        serialized.insert(serialized.end(), offsetBytes, offsetBytes + sizeof(uint64_t));
        
        const uint8_t* compressedSizeBytes = reinterpret_cast<const uint8_t*>(&mapping.compressedSize);
        serialized.insert(serialized.end(), compressedSizeBytes, compressedSizeBytes + sizeof(uint64_t));
        
        const uint8_t* originalSizeBytes = reinterpret_cast<const uint8_t*>(&mapping.originalSize);
        serialized.insert(serialized.end(), originalSizeBytes, originalSizeBytes + sizeof(uint64_t));
        
        const uint8_t* checksumBytes = reinterpret_cast<const uint8_t*>(&mapping.checksum);
        serialized.insert(serialized.end(), checksumBytes, checksumBytes + sizeof(uint32_t));
        
        const uint8_t* algorithmBytes = reinterpret_cast<const uint8_t*>(&mapping.algorithm);
        serialized.insert(serialized.end(), algorithmBytes, algorithmBytes + sizeof(CompressionAlgorithm));
        
        // 序列化字符串长度和内容
        uint32_t folderNameLen = static_cast<uint32_t>(mapping.folderName.length());
        const uint8_t* folderNameLenBytes = reinterpret_cast<const uint8_t*>(&folderNameLen);
        serialized.insert(serialized.end(), folderNameLenBytes, folderNameLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), mapping.folderName.begin(), mapping.folderName.end());
        
        uint32_t targetPathLen = static_cast<uint32_t>(mapping.targetPath.length());
        const uint8_t* targetPathLenBytes = reinterpret_cast<const uint8_t*>(&targetPathLen);
        serialized.insert(serialized.end(), targetPathLenBytes, targetPathLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), mapping.targetPath.begin(), mapping.targetPath.end());
    }
    
    return serialized;
}

std::vector<uint8_t> MetadataGenerator::serializeExtendedMetadata(const ExtendedInstallationMetadata& metadata) {
    std::vector<uint8_t> serialized;
    
    // 计算字符串数据的总大小
    size_t stringDataSize = 0;
    
    // 基本字段的字符串大小
    for (const auto& mapping : metadata.folderMappings) {
        stringDataSize += mapping.folderName.length() + 1;
        stringDataSize += mapping.targetPath.length() + 1;
    }
    
    // 扩展字段的字符串大小
    stringDataSize += metadata.applicationName.length() + 1;
    stringDataSize += metadata.defaultInstallDir.length() + 1;
    
    for (const auto& extMapping : metadata.extendedMappings) {
        stringDataSize += extMapping.customTargetPath.length() + 1;
    }
    
    // 创建二进制头（版本号设为2表示扩展格式）
    BinaryMetadata header;
    header.magic = Constants::MAGIC_NUMBER;
    header.version = 2; // 扩展版本
    header.folderCount = metadata.folderCount;
    
    // 计算元数据大小：头部 + 基本映射 + 扩展字段
    header.metadataSize = sizeof(BinaryMetadata) + 
                         metadata.folderMappings.size() * (sizeof(uint64_t) * 4 + sizeof(uint32_t) * 2) + 
                         metadata.extendedMappings.size() * (sizeof(SpecialDirectoryType) + sizeof(uint32_t)) +
                         stringDataSize;
    header.dataOffset = header.metadataSize;
    
    // 序列化头部
    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    serialized.insert(serialized.end(), headerBytes, headerBytes + sizeof(BinaryMetadata));
    
    // 序列化应用程序名称
    uint32_t appNameLen = static_cast<uint32_t>(metadata.applicationName.length());
    const uint8_t* appNameLenBytes = reinterpret_cast<const uint8_t*>(&appNameLen);
    serialized.insert(serialized.end(), appNameLenBytes, appNameLenBytes + sizeof(uint32_t));
    serialized.insert(serialized.end(), metadata.applicationName.begin(), metadata.applicationName.end());
    
    // 序列化默认安装目录
    uint32_t defaultInstallDirLen = static_cast<uint32_t>(metadata.defaultInstallDir.length());
    const uint8_t* defaultInstallDirLenBytes = reinterpret_cast<const uint8_t*>(&defaultInstallDirLen);
    serialized.insert(serialized.end(), defaultInstallDirLenBytes, defaultInstallDirLenBytes + sizeof(uint32_t));
    serialized.insert(serialized.end(), metadata.defaultInstallDir.begin(), metadata.defaultInstallDir.end());
    
    // 序列化文件夹映射（基本字段）
    for (const auto& mapping : metadata.folderMappings) {
        const uint8_t* offsetBytes = reinterpret_cast<const uint8_t*>(&mapping.offset);
        serialized.insert(serialized.end(), offsetBytes, offsetBytes + sizeof(uint64_t));
        
        const uint8_t* compressedSizeBytes = reinterpret_cast<const uint8_t*>(&mapping.compressedSize);
        serialized.insert(serialized.end(), compressedSizeBytes, compressedSizeBytes + sizeof(uint64_t));
        
        const uint8_t* originalSizeBytes = reinterpret_cast<const uint8_t*>(&mapping.originalSize);
        serialized.insert(serialized.end(), originalSizeBytes, originalSizeBytes + sizeof(uint64_t));
        
        const uint8_t* checksumBytes = reinterpret_cast<const uint8_t*>(&mapping.checksum);
        serialized.insert(serialized.end(), checksumBytes, checksumBytes + sizeof(uint32_t));
        
        const uint8_t* algorithmBytes = reinterpret_cast<const uint8_t*>(&mapping.algorithm);
        serialized.insert(serialized.end(), algorithmBytes, algorithmBytes + sizeof(CompressionAlgorithm));
        
        uint32_t folderNameLen = static_cast<uint32_t>(mapping.folderName.length());
        const uint8_t* folderNameLenBytes = reinterpret_cast<const uint8_t*>(&folderNameLen);
        serialized.insert(serialized.end(), folderNameLenBytes, folderNameLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), mapping.folderName.begin(), mapping.folderName.end());
        
        uint32_t targetPathLen = static_cast<uint32_t>(mapping.targetPath.length());
        const uint8_t* targetPathLenBytes = reinterpret_cast<const uint8_t*>(&targetPathLen);
        serialized.insert(serialized.end(), targetPathLenBytes, targetPathLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), mapping.targetPath.begin(), mapping.targetPath.end());
    }
    
    // 序列化扩展映射字段
    for (const auto& extMapping : metadata.extendedMappings) {
        // 序列化目标目录类型
        const uint8_t* dirTypeBytes = reinterpret_cast<const uint8_t*>(&extMapping.targetDirType);
        serialized.insert(serialized.end(), dirTypeBytes, dirTypeBytes + sizeof(SpecialDirectoryType));
        
        // 序列化自定义目标路径
        uint32_t customPathLen = static_cast<uint32_t>(extMapping.customTargetPath.length());
        const uint8_t* customPathLenBytes = reinterpret_cast<const uint8_t*>(&customPathLen);
        serialized.insert(serialized.end(), customPathLenBytes, customPathLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), extMapping.customTargetPath.begin(), extMapping.customTargetPath.end());
    }
    
    return serialized;
}

FolderMapping MetadataGenerator::createFolderMapping(const CompressionResult& result, 
                                                   const FolderInfo& folderInfo, 
                                                   uint64_t offset) {
    FolderMapping mapping;
    mapping.folderName = folderInfo.targetPath;
    mapping.targetPath = folderInfo.targetPath;
    mapping.offset = offset;
    mapping.compressedSize = result.compressedSize;
    mapping.originalSize = result.originalSize;
    mapping.checksum = result.checksum;
    mapping.algorithm = result.algorithm;
    
    return mapping;
}

ExtendedFolderMapping MetadataGenerator::createExtendedFolderMapping(const CompressionResult& result, 
                                                                    const FolderInfo& folderInfo, 
                                                                    uint64_t offset,
                                                                    const PackagerConfiguration& config) {
    ExtendedFolderMapping mapping;
    
    // 填充基类字段
    mapping.folderName = folderInfo.targetPath;
    mapping.targetPath = folderInfo.targetPath;
    mapping.offset = offset;
    mapping.compressedSize = result.compressedSize;
    mapping.originalSize = result.originalSize;
    mapping.checksum = result.checksum;
    mapping.algorithm = result.algorithm;
    
    // 查找该文件夹的目标配置
    mapping.targetDirType = SpecialDirectoryType::INSTALL_DIRECTORY; // 默认值
    mapping.customTargetPath = "";
    
    for (const auto& folderTarget : config.folderTargets) {
        if (folderTarget.folderName == folderInfo.targetPath) {
            mapping.targetDirType = folderTarget.dirType;
            mapping.customTargetPath = folderTarget.targetDirectory;
            break;
        }
    }
    
    return mapping;
}

uint64_t MetadataGenerator::calculateTotalCompressedSize(const std::vector<CompressionResult>& results) {
    uint64_t total = 0;
    for (const auto& result : results) {
        total += result.compressedSize;
    }
    return total;
}

} // namespace MultiThreadedInstaller