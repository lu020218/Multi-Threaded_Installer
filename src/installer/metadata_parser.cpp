#include "installer/metadata_parser.h"
#include "installer/metadata_binary_reader.h"
#include "installer/installer_helpers.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace MultiThreadedInstaller {
namespace {

#define META_LOG() \
    do { \
        logInstallerError(std::string("[Metadata] Parse/read error at line ") + std::to_string(__LINE__)); \
    } while (0)

bool IsSupportedPayloadAlgorithm(CompressionAlgorithm algorithm) {
    switch (algorithm) {
    case CompressionAlgorithm::LZMA2_XZ:
    case CompressionAlgorithm::ZSTD:
        return true;
    default:
        return false;
    }
}

} // namespace

InstallationMetadata MetadataParser::parseEmbeddedMetadata() {
    std::vector<uint8_t> embeddedData = dataPackagePath_.empty() ? readEmbeddedData() : readExternalMetadata();
    if (embeddedData.empty()) {
        META_LOG();
        return InstallationMetadata{};
    }
    
    return deserializeMetadata(embeddedData);
}

ExtendedInstallationMetadata MetadataParser::parseExtendedEmbeddedMetadata() {
    std::vector<uint8_t> embeddedData = dataPackagePath_.empty() ? readEmbeddedData() : readExternalMetadata();
    if (embeddedData.empty()) {
        META_LOG();
        return ExtendedInstallationMetadata{};
    }
    
    return deserializeExtendedMetadata(embeddedData);
}

bool MetadataParser::validateMetadata(const InstallationMetadata& metadata) {
    if (!IsSupportedMetadataVersion(metadata.version)) {
        META_LOG();
        return false;
    }
    
    if (metadata.folderCount == 0) {
        META_LOG();
        return false;
    }
    
    if (metadata.folderMappings.size() != metadata.folderCount) {
        META_LOG();
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
        META_LOG();
        return {};
    }
    

    file.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(file.tellg());

    uint64_t logicalEnd = fileSize;
    DataLocator locator;
    if (!readEmbeddedLocator(file, fileSize, logicalEnd, locator)) {
        return {};
    }
    

    file.seekg(locator.metadataOffset);
    std::vector<uint8_t> metadata(locator.metadataSize);
    file.read(reinterpret_cast<char*>(metadata.data()), locator.metadataSize);
    
    if (file.gcount() != static_cast<std::streamsize>(locator.metadataSize)) {
        META_LOG();
        return {};
    }
    
    return metadata;
}

InstallationMetadata MetadataParser::deserializeMetadata(const std::vector<uint8_t>& data) {
    InstallationMetadata metadata;
    
    if (data.size() < sizeof(BinaryMetadata)) {
        META_LOG();
        return metadata;
    }
    

    const BinaryMetadata* header = reinterpret_cast<const BinaryMetadata*>(data.data());
    
    if (!validateHeader(*header)) {
        return metadata;
    }
    
    metadata.version = header->version;
    metadata.folderCount = header->folderCount;
    

    size_t offset = sizeof(BinaryMetadata);
    for (uint32_t i = 0; i < header->folderCount; ++i) {
        FolderMapping mapping;
        

        if (offset + sizeof(uint64_t) * 3 + sizeof(uint32_t) * 3 > data.size()) {
            META_LOG();
            break;
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
        if (!IsSupportedPayloadAlgorithm(mapping.algorithm)) {
            META_LOG();
            break;
        }
        

        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            break;
        }
        
        uint32_t folderNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        
        if (offset + folderNameLen > data.size()) {
            META_LOG();
            break;
        }
        
        mapping.folderName = std::string(reinterpret_cast<const char*>(data.data() + offset), folderNameLen);
        offset += folderNameLen;
        

        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            break;
        }
        
        uint32_t targetPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        
        if (offset + targetPathLen > data.size()) {
            META_LOG();
            break;
        }
        
        mapping.targetPath = std::string(reinterpret_cast<const char*>(data.data() + offset), targetPathLen);
        offset += targetPathLen;
        
        metadata.folderMappings.push_back(mapping);
    }
    

    metadata.totalCompressedSize = 0;
    for (const auto& mapping : metadata.folderMappings) {
        metadata.totalCompressedSize += mapping.compressedSize;
    }
    
    return metadata;
}

ExtendedInstallationMetadata MetadataParser::deserializeExtendedMetadata(const std::vector<uint8_t>& data) {
    ExtendedInstallationMetadata metadata;
    
    if (data.size() < sizeof(BinaryMetadata)) {
        META_LOG();
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
        META_LOG();
        return metadata;
    }
    
    uint32_t extendedMarker = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    if (extendedMarker != 0x45585444) {
        META_LOG();
        return metadata;
    }
    offset += sizeof(uint32_t);
    
    if (!ReadString(data, offset, metadata.applicationName, "applicationName")) {
        return metadata;
    }
    if (header->version >= 15) {
        if (!ReadString(data, offset, metadata.appId, "appId") ||
            (header->version >= 16 &&
             !ReadString(data, offset, metadata.directoryName, "directoryName")) ||
            !ReadStringList(data, offset, metadata.legacyAppIds, "legacyAppIds")) {
            return metadata;
        }
        if (header->version >= 19) {
            if (!ReadString(data, offset, metadata.desktopShortcutName, "desktopShortcutName") ||
                !ReadStringMap(data, offset, metadata.desktopShortcutNameI18n, "desktopShortcutNameI18n") ||
                !ReadStringList(data, offset, metadata.legacyDesktopShortcutNames, "legacyDesktopShortcutNames")) {
                return metadata;
            }
        } else {
            metadata.desktopShortcutName.clear();
            metadata.desktopShortcutNameI18n.clear();
            metadata.legacyDesktopShortcutNames.clear();
        }
    } else {
        metadata.appId.clear();
        metadata.directoryName.clear();
        metadata.legacyAppIds.clear();
        metadata.desktopShortcutName.clear();
        metadata.desktopShortcutNameI18n.clear();
        metadata.legacyDesktopShortcutNames.clear();
    }
    if (!ReadString(data, offset, metadata.defaultInstallDir, "defaultInstallDir") ||
        !ReadString(data, offset, metadata.configVersion, "configVersion")) {
        return metadata;
    }

    if (header->version >= 10) {
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t webUrlLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + webUrlLen > data.size()) {
            META_LOG();
            return metadata;
        }
        metadata.webPageUrl = std::string(reinterpret_cast<const char*>(data.data() + offset), webUrlLen);
        offset += webUrlLen;
    } else {
        metadata.webPageUrl.clear();
    }

    if (metadata.appId.empty()) {
        metadata.appId = metadata.applicationName;
    }
    if (metadata.directoryName.empty()) {
        metadata.directoryName = metadata.applicationName;
    }
    if (metadata.desktopShortcutName.empty()) {
        metadata.desktopShortcutName = metadata.applicationName;
    }

    if (header->version >= 7) {
        size_t flagCount = header->version >= 9 ? 4 : 3;
        if (offset + sizeof(uint8_t) * flagCount > data.size()) {
            META_LOG();
            return metadata;
        }
        metadata.autoStartup = data[offset] != 0;
        metadata.desktopIcons = data[offset + 1] != 0;
        metadata.requireAdmin = data[offset + 2] != 0;
        metadata.autoCleanOldInstall = header->version >= 9 ? (data[offset + 3] != 0) : false;
        offset += sizeof(uint8_t) * flagCount;
    } else {
        if (offset + sizeof(uint8_t) * 2 > data.size()) {
            META_LOG();
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
            META_LOG();
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
            META_LOG();
            return metadata;
        }
        metadata.sparseFileThresholdBytes = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        offset += sizeof(uint64_t);
    } else {
        metadata.sparseFileThresholdBytes = 4 * 1024 * 1024;
    }

    if (offset + sizeof(uint8_t) * 2 > data.size()) {
        META_LOG();
        return metadata;
    }
    metadata.installState.mode = static_cast<InstallStateMode>(data[offset]);
    metadata.installState.useMutex = data[offset + 1] != 0;
    offset += sizeof(uint8_t) * 2;

    if (offset + sizeof(uint32_t) > data.size()) {
        META_LOG();
        return metadata;
    }
    uint32_t regPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + regPathLen > data.size()) {
        META_LOG();
        return metadata;
    }
    metadata.installState.registryPath = std::string(reinterpret_cast<const char*>(data.data() + offset), regPathLen);
    offset += regPathLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        META_LOG();
        return metadata;
    }
    uint32_t regKeyLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + regKeyLen > data.size()) {
        META_LOG();
        return metadata;
    }
    metadata.installState.registryKey = std::string(reinterpret_cast<const char*>(data.data() + offset), regKeyLen);
    offset += regKeyLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        META_LOG();
        return metadata;
    }
    uint32_t filePathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + filePathLen > data.size()) {
        META_LOG();
        return metadata;
    }
    metadata.installState.filePath = std::string(reinterpret_cast<const char*>(data.data() + offset), filePathLen);
    offset += filePathLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        META_LOG();
        return metadata;
    }
    uint32_t mutexNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + mutexNameLen > data.size()) {
        META_LOG();
        return metadata;
    }
    metadata.installState.mutexName = std::string(reinterpret_cast<const char*>(data.data() + offset), mutexNameLen);
    offset += mutexNameLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        META_LOG();
        return metadata;
    }
    uint32_t registryCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    metadata.registry.reserve(registryCount);
    for (uint32_t r = 0; r < registryCount; ++r) {
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t pathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + pathLen > data.size()) {
            META_LOG();
            return metadata;
        }
        RegistryEntry reg;
        reg.path = std::string(reinterpret_cast<const char*>(data.data() + offset), pathLen);
        offset += pathLen;
        
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t keyLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + keyLen > data.size()) {
            META_LOG();
            return metadata;
        }
        reg.key = std::string(reinterpret_cast<const char*>(data.data() + offset), keyLen);
        offset += keyLen;
        
        if (offset + sizeof(uint8_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        reg.type = static_cast<RegistryValueType>(data[offset]);
        offset += sizeof(uint8_t);
        
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t valueLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + valueLen > data.size()) {
            META_LOG();
            return metadata;
        }
        reg.value = std::string(reinterpret_cast<const char*>(data.data() + offset), valueLen);
        offset += valueLen;
        
        metadata.registry.push_back(std::move(reg));
    }
    
    if (header->version >= 12) {
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t killCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        metadata.installKillProcesses.reserve(killCount);
        for (uint32_t k = 0; k < killCount; ++k) {
            if (offset + sizeof(uint32_t) > data.size()) {
                META_LOG();
                return metadata;
            }
            uint32_t nameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            if (offset + nameLen > data.size()) {
                META_LOG();
                return metadata;
            }
            metadata.installKillProcesses.emplace_back(reinterpret_cast<const char*>(data.data() + offset), nameLen);
            offset += nameLen;
        }
    } else {
        metadata.installKillProcesses.clear();
    }

    if (header->version >= 13) {
        if (!ReadComponentList(data, offset, metadata.components)) {
            return metadata;
        }
        if (!ReadComponentUiConfig(data, offset, metadata.componentUi)) {
            return metadata;
        }
        if (header->version >= 14) {
            if (!ReadUiLinks(data, offset, metadata.uiLinks)) {
                return metadata;
            }
        } else {
            metadata.uiLinks.clear();
        }
        if (header->version >= 18) {
            if (!ReadCleanupRules(data, offset, metadata.uninstallCleanupRules)) {
                return metadata;
            }
        } else {
            metadata.uninstallCleanupRules.clear();
        }
        if (header->version >= 21) {
            uint8_t deleteFromManifest = 0;
            if (!ReadPod<uint8_t>(data, offset, deleteFromManifest)) {
                META_LOG();
                return metadata;
            }
            metadata.upgradeCleanup.registry.deleteFromManifest = deleteFromManifest != 0;
            if (!ReadRegistryList(data,
                                  offset,
                                  metadata.upgradeCleanup.registry.legacyKeys,
                                  "upgradeCleanup.registry.legacyKeys") ||
                !ReadCleanupRules(data, offset, metadata.upgradeCleanup.extraPaths)) {
                return metadata;
            }
        } else {
            metadata.upgradeCleanup = UpgradeCleanupConfig{};
        }
    } else {
        metadata.components.clear();
        metadata.componentUi = UiComponentSelectionConfig();
        metadata.uiLinks.clear();
        metadata.uninstallCleanupRules.clear();
        metadata.upgradeCleanup = UpgradeCleanupConfig{};
    }

    for (uint32_t i = 0; i < header->folderCount; ++i) {
        ExtendedFolderMapping mapping;
        
        if (offset + sizeof(uint64_t) * 3 + sizeof(uint32_t) > data.size()) {
            META_LOG();
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
        if (!IsSupportedPayloadAlgorithm(mapping.algorithm)) {
            META_LOG();
            return metadata;
        }
        
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t folderNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + folderNameLen > data.size()) {
            META_LOG();
            return metadata;
        }
        mapping.folderName = std::string(reinterpret_cast<const char*>(data.data() + offset), folderNameLen);
        offset += folderNameLen;
        
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t targetPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + targetPathLen > data.size()) {
            META_LOG();
            return metadata;
        }
        mapping.targetPath = std::string(reinterpret_cast<const char*>(data.data() + offset), targetPathLen);
        offset += targetPathLen;
        
        if (offset + sizeof(SpecialDirectoryType) > data.size()) {
            META_LOG();
            return metadata;
        }
        mapping.targetDirType = *reinterpret_cast<const SpecialDirectoryType*>(data.data() + offset);
        offset += sizeof(SpecialDirectoryType);

        if (header->version >= 17) {
            if (offset + sizeof(uint8_t) > data.size()) {
                META_LOG();
                return metadata;
            }
            mapping.appendDirectoryName = data[offset] != 0;
            offset += sizeof(uint8_t);
        } else {
            mapping.appendDirectoryName = true;
        }
        
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t customPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + customPathLen > data.size()) {
            META_LOG();
            return metadata;
        }
        mapping.customTargetPath = std::string(reinterpret_cast<const char*>(data.data() + offset), customPathLen);
        offset += customPathLen;
        
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t fileCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        mapping.fileIndex.reserve(fileCount);
        for (uint32_t f = 0; f < fileCount; ++f) {
            if (offset + sizeof(uint32_t) > data.size()) {
                META_LOG();
                return metadata;
            }
            uint32_t pathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            if (offset + pathLen > data.size()) {
                META_LOG();
                return metadata;
            }
            FileIndexEntry fileEntry;
            fileEntry.relativePath = std::string(reinterpret_cast<const char*>(data.data() + offset), pathLen);
            offset += pathLen;
            if (offset + sizeof(uint64_t) * 2 > data.size()) {
                META_LOG();
                return metadata;
            }
            fileEntry.offset = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            fileEntry.size = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            mapping.fileIndex.push_back(std::move(fileEntry));
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

std::vector<uint8_t> MetadataParser::readExternalMetadata() {
    std::ifstream file(PathFromUtf8(dataPackagePath_), std::ios::binary);
    if (!file) {
        META_LOG();
        return {};
    }
    
    DataPackageHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(DataPackageHeader));
    if (!file || header.magic != Constants::DATA_MAGIC_NUMBER) {
        META_LOG();
        return {};
    }
    
    file.seekg(static_cast<std::streamoff>(header.metadataOffset));
    std::vector<uint8_t> metadata(header.metadataSize);
    file.read(reinterpret_cast<char*>(metadata.data()), header.metadataSize);
    
    if (file.gcount() != static_cast<std::streamsize>(header.metadataSize)) {
        META_LOG();
        return {};
    }
    
    return metadata;
}

bool MetadataParser::validateHeader(const BinaryMetadata& header) {
    if (header.magic != Constants::MAGIC_NUMBER) {
        META_LOG();
        return false;
    }
    
    if (!IsSupportedMetadataVersion(header.version)) {
        META_LOG();
        return false;
    }
    
    return true;
}

bool MetadataParser::readEmbeddedLocator(std::ifstream& file,
                                         uint64_t fileSize,
                                         uint64_t& logicalEnd,
                                         DataLocator& locator) {
    EmbeddedDataLocatorRecord resolvedLocator;
    if (!findEmbeddedDataLocator(file, fileSize, logicalEnd, resolvedLocator)) {
        META_LOG();
        return false;
    }

    locator.magic = resolvedLocator.magic;
    locator.metadataOffset = resolvedLocator.metadataOffset;
    locator.metadataSize = resolvedLocator.metadataSize;
    locator.dataOffset = resolvedLocator.dataOffset;
    locator.dataSize = resolvedLocator.dataSize;

    if (locator.metadataOffset >= logicalEnd ||
        locator.metadataOffset + locator.metadataSize > logicalEnd) {
        META_LOG();
        return false;
    }

    if (locator.dataOffset >= logicalEnd ||
        locator.dataOffset + locator.dataSize > logicalEnd) {
        META_LOG();
        return false;
    }

    return true;
}

} // namespace MultiThreadedInstaller

