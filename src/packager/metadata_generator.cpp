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

uint64_t MetadataGenerator::calculateTotalCompressedSize(const std::vector<CompressionResult>& results) {
    uint64_t total = 0;
    for (const auto& result : results) {
        total += result.compressedSize;
    }
    return total;
}

} // namespace MultiThreadedInstaller