#include "packager/metadata_generator.h"
#include <cstring>
#include <filesystem>

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
    metadata.configVersion = config.version;
    metadata.defaultInstallDir = config.defaultInstallDir;
    metadata.autoStartup = config.autoStartup;
    metadata.desktopIcons = config.desktopIcons;
    metadata.installState = config.installState;
    metadata.registry = config.registry;
    
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
    
    BinaryMetadata header;
    header.magic = Constants::MAGIC_NUMBER;
    header.version = Constants::VERSION;
    header.folderCount = metadata.folderCount;
    header.metadataSize = 0;
    header.dataOffset = 0;
    
    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    serialized.insert(serialized.end(), headerBytes, headerBytes + sizeof(BinaryMetadata));
    
    uint32_t extendedMarker = 0x45585444;
    const uint8_t* markerBytes = reinterpret_cast<const uint8_t*>(&extendedMarker);
    serialized.insert(serialized.end(), markerBytes, markerBytes + sizeof(uint32_t));
    
    uint32_t appNameLen = static_cast<uint32_t>(metadata.applicationName.length());
    const uint8_t* appNameLenBytes = reinterpret_cast<const uint8_t*>(&appNameLen);
    serialized.insert(serialized.end(), appNameLenBytes, appNameLenBytes + sizeof(uint32_t));
    serialized.insert(serialized.end(), metadata.applicationName.begin(), metadata.applicationName.end());
    
    uint32_t defaultInstallDirLen = static_cast<uint32_t>(metadata.defaultInstallDir.length());
    const uint8_t* defaultInstallDirLenBytes = reinterpret_cast<const uint8_t*>(&defaultInstallDirLen);
    serialized.insert(serialized.end(), defaultInstallDirLenBytes, defaultInstallDirLenBytes + sizeof(uint32_t));
    serialized.insert(serialized.end(), metadata.defaultInstallDir.begin(), metadata.defaultInstallDir.end());

    uint32_t configVersionLen = static_cast<uint32_t>(metadata.configVersion.length());
    const uint8_t* configVersionLenBytes = reinterpret_cast<const uint8_t*>(&configVersionLen);
    serialized.insert(serialized.end(), configVersionLenBytes, configVersionLenBytes + sizeof(uint32_t));
    serialized.insert(serialized.end(), metadata.configVersion.begin(), metadata.configVersion.end());

    uint8_t autoStartupFlag = metadata.autoStartup ? 1 : 0;
    uint8_t desktopIconsFlag = metadata.desktopIcons ? 1 : 0;
    serialized.push_back(autoStartupFlag);
    serialized.push_back(desktopIconsFlag);

    uint8_t installMode = static_cast<uint8_t>(metadata.installState.mode);
    uint8_t installMutex = metadata.installState.useMutex ? 1 : 0;
    serialized.push_back(installMode);
    serialized.push_back(installMutex);
    
    uint32_t regPathLen = static_cast<uint32_t>(metadata.installState.registryPath.length());
    const uint8_t* regPathLenBytes = reinterpret_cast<const uint8_t*>(&regPathLen);
    serialized.insert(serialized.end(), regPathLenBytes, regPathLenBytes + sizeof(uint32_t));
    serialized.insert(serialized.end(), metadata.installState.registryPath.begin(), metadata.installState.registryPath.end());
    
    uint32_t regKeyLen = static_cast<uint32_t>(metadata.installState.registryKey.length());
    const uint8_t* regKeyLenBytes = reinterpret_cast<const uint8_t*>(&regKeyLen);
    serialized.insert(serialized.end(), regKeyLenBytes, regKeyLenBytes + sizeof(uint32_t));
    serialized.insert(serialized.end(), metadata.installState.registryKey.begin(), metadata.installState.registryKey.end());
    
    uint32_t filePathLen = static_cast<uint32_t>(metadata.installState.filePath.length());
    const uint8_t* filePathLenBytes = reinterpret_cast<const uint8_t*>(&filePathLen);
    serialized.insert(serialized.end(), filePathLenBytes, filePathLenBytes + sizeof(uint32_t));
    serialized.insert(serialized.end(), metadata.installState.filePath.begin(), metadata.installState.filePath.end());
    
    uint32_t mutexNameLen = static_cast<uint32_t>(metadata.installState.mutexName.length());
    const uint8_t* mutexNameLenBytes = reinterpret_cast<const uint8_t*>(&mutexNameLen);
    serialized.insert(serialized.end(), mutexNameLenBytes, mutexNameLenBytes + sizeof(uint32_t));
    serialized.insert(serialized.end(), metadata.installState.mutexName.begin(), metadata.installState.mutexName.end());

    uint32_t registryCount = static_cast<uint32_t>(metadata.registry.size());
    const uint8_t* registryCountBytes = reinterpret_cast<const uint8_t*>(&registryCount);
    serialized.insert(serialized.end(), registryCountBytes, registryCountBytes + sizeof(uint32_t));
    
    for (const auto& reg : metadata.registry) {
        uint32_t pathLen = static_cast<uint32_t>(reg.path.length());
        const uint8_t* pathLenBytes = reinterpret_cast<const uint8_t*>(&pathLen);
        serialized.insert(serialized.end(), pathLenBytes, pathLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), reg.path.begin(), reg.path.end());
        
        uint32_t keyLen = static_cast<uint32_t>(reg.key.length());
        const uint8_t* keyLenBytes = reinterpret_cast<const uint8_t*>(&keyLen);
        serialized.insert(serialized.end(), keyLenBytes, keyLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), reg.key.begin(), reg.key.end());
        
        uint8_t valueType = static_cast<uint8_t>(reg.type);
        serialized.push_back(valueType);
        
        uint32_t valueLen = static_cast<uint32_t>(reg.value.length());
        const uint8_t* valueLenBytes = reinterpret_cast<const uint8_t*>(&valueLen);
        serialized.insert(serialized.end(), valueLenBytes, valueLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), reg.value.begin(), reg.value.end());
    }
    
    for (size_t i = 0; i < metadata.extendedMappings.size(); ++i) {
        const auto& extMapping = metadata.extendedMappings[i];
        
        const uint8_t* offsetBytes = reinterpret_cast<const uint8_t*>(&extMapping.offset);
        serialized.insert(serialized.end(), offsetBytes, offsetBytes + sizeof(uint64_t));
        
        const uint8_t* compressedSizeBytes = reinterpret_cast<const uint8_t*>(&extMapping.compressedSize);
        serialized.insert(serialized.end(), compressedSizeBytes, compressedSizeBytes + sizeof(uint64_t));
        
        const uint8_t* originalSizeBytes = reinterpret_cast<const uint8_t*>(&extMapping.originalSize);
        serialized.insert(serialized.end(), originalSizeBytes, originalSizeBytes + sizeof(uint64_t));
        
        const uint8_t* checksumBytes = reinterpret_cast<const uint8_t*>(&extMapping.checksum);
        serialized.insert(serialized.end(), checksumBytes, checksumBytes + sizeof(uint32_t));
        
        const uint8_t* algorithmBytes = reinterpret_cast<const uint8_t*>(&extMapping.algorithm);
        serialized.insert(serialized.end(), algorithmBytes, algorithmBytes + sizeof(CompressionAlgorithm));
        
        uint32_t folderNameLen = static_cast<uint32_t>(extMapping.folderName.length());
        const uint8_t* folderNameLenBytes = reinterpret_cast<const uint8_t*>(&folderNameLen);
        serialized.insert(serialized.end(), folderNameLenBytes, folderNameLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), extMapping.folderName.begin(), extMapping.folderName.end());
        
        uint32_t targetPathLen = static_cast<uint32_t>(extMapping.targetPath.length());
        const uint8_t* targetPathLenBytes = reinterpret_cast<const uint8_t*>(&targetPathLen);
        serialized.insert(serialized.end(), targetPathLenBytes, targetPathLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), extMapping.targetPath.begin(), extMapping.targetPath.end());
        
        const uint8_t* dirTypeBytes = reinterpret_cast<const uint8_t*>(&extMapping.targetDirType);
        serialized.insert(serialized.end(), dirTypeBytes, dirTypeBytes + sizeof(SpecialDirectoryType));
        
        uint32_t customPathLen = static_cast<uint32_t>(extMapping.customTargetPath.length());
        const uint8_t* customPathLenBytes = reinterpret_cast<const uint8_t*>(&customPathLen);
        serialized.insert(serialized.end(), customPathLenBytes, customPathLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), extMapping.customTargetPath.begin(), extMapping.customTargetPath.end());
        
        uint32_t fileCount = static_cast<uint32_t>(extMapping.fileIndex.size());
        const uint8_t* fileCountBytes = reinterpret_cast<const uint8_t*>(&fileCount);
        serialized.insert(serialized.end(), fileCountBytes, fileCountBytes + sizeof(uint32_t));
        
        for (const auto& fileEntry : extMapping.fileIndex) {
            uint32_t pathLen = static_cast<uint32_t>(fileEntry.relativePath.length());
            const uint8_t* pathLenBytes = reinterpret_cast<const uint8_t*>(&pathLen);
            serialized.insert(serialized.end(), pathLenBytes, pathLenBytes + sizeof(uint32_t));
            serialized.insert(serialized.end(), fileEntry.relativePath.begin(), fileEntry.relativePath.end());
            
            const uint8_t* entryOffsetBytes = reinterpret_cast<const uint8_t*>(&fileEntry.offset);
            serialized.insert(serialized.end(), entryOffsetBytes, entryOffsetBytes + sizeof(uint64_t));
            
            const uint8_t* entrySizeBytes = reinterpret_cast<const uint8_t*>(&fileEntry.size);
            serialized.insert(serialized.end(), entrySizeBytes, entrySizeBytes + sizeof(uint64_t));
        }
        
        uint32_t blockCount = static_cast<uint32_t>(extMapping.blockIndex.size());
        const uint8_t* blockCountBytes = reinterpret_cast<const uint8_t*>(&blockCount);
        serialized.insert(serialized.end(), blockCountBytes, blockCountBytes + sizeof(uint32_t));
        
        for (const auto& blockEntry : extMapping.blockIndex) {
            const uint8_t* blockIdBytes = reinterpret_cast<const uint8_t*>(&blockEntry.blockId);
            serialized.insert(serialized.end(), blockIdBytes, blockIdBytes + sizeof(uint32_t));
            
            const uint8_t* blockOffsetBytes = reinterpret_cast<const uint8_t*>(&blockEntry.offset);
            serialized.insert(serialized.end(), blockOffsetBytes, blockOffsetBytes + sizeof(uint64_t));
            
            const uint8_t* compSizeBytes = reinterpret_cast<const uint8_t*>(&blockEntry.compressedSize);
            serialized.insert(serialized.end(), compSizeBytes, compSizeBytes + sizeof(uint64_t));
            
            const uint8_t* origSizeBytes = reinterpret_cast<const uint8_t*>(&blockEntry.originalSize);
            serialized.insert(serialized.end(), origSizeBytes, origSizeBytes + sizeof(uint64_t));
            
            const uint8_t* blockChecksumBytes = reinterpret_cast<const uint8_t*>(&blockEntry.checksum);
            serialized.insert(serialized.end(), blockChecksumBytes, blockChecksumBytes + sizeof(uint32_t));
        }
    }
    
    header.metadataSize = static_cast<uint64_t>(serialized.size());
    header.dataOffset = header.metadataSize;
    std::memcpy(serialized.data(), &header, sizeof(BinaryMetadata));
    
    return serialized;
}

FolderMapping MetadataGenerator::createFolderMapping(const CompressionResult& result, 
                                                   const FolderInfo& folderInfo, 
                                                   uint64_t offset) {
    FolderMapping mapping;
    
    // 从sourcePath提取文件夹名称（最后一个路径组件）
    std::filesystem::path sourcePath(folderInfo.sourcePath);
    std::string folderName = sourcePath.filename().string();
    
    mapping.folderName = folderName;
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
    
    // 从sourcePath提取文件夹名称（最后一个路径组件）
    std::filesystem::path sourcePath(folderInfo.sourcePath);
    std::string folderName = sourcePath.filename().string();
    
    // 填充基类字段
    mapping.folderName = folderName;
    mapping.targetPath = folderInfo.targetPath;
    mapping.offset = offset;
    mapping.compressedSize = result.compressedSize;
    mapping.originalSize = result.originalSize;
    mapping.checksum = result.checksum;
    mapping.algorithm = result.algorithm;
    mapping.fileIndex = result.fileIndex;
    mapping.blockIndex = result.blockIndex;
    
    // 查找该文件夹的目标配置
    mapping.targetDirType = SpecialDirectoryType::INSTALL_DIRECTORY; // 默认值
    mapping.customTargetPath = "";
    
    for (const auto& folderTarget : config.folderTargets) {
        if (folderTarget.folderName == folderName) {
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


