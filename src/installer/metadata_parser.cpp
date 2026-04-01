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

bool ReadPayloadMapping(const std::vector<uint8_t>& data,
                        size_t& offset,
                        FolderMapping& mapping) {
    return ReadPod<uint64_t>(data, offset, mapping.offset) &&
           ReadPod<uint64_t>(data, offset, mapping.compressedSize) &&
           ReadPod<uint64_t>(data, offset, mapping.originalSize) &&
           ReadPod<uint32_t>(data, offset, mapping.checksum) &&
           ReadPod<CompressionAlgorithm>(data, offset, mapping.algorithm) &&
           IsSupportedPayloadAlgorithm(mapping.algorithm) &&
           ReadString(data, offset, mapping.folderId, "folderId") &&
           ReadString(data, offset, mapping.folderName, "folderName") &&
           ReadString(data, offset, mapping.targetPath, "targetPath");
}

bool ReadExtendedPayloadMapping(const std::vector<uint8_t>& data,
                                size_t& offset,
                                ExtendedFolderMapping& mapping) {
    if (!ReadPod<uint64_t>(data, offset, mapping.offset) ||
        !ReadPod<uint64_t>(data, offset, mapping.compressedSize) ||
        !ReadPod<uint64_t>(data, offset, mapping.originalSize) ||
        !ReadPod<uint32_t>(data, offset, mapping.checksum) ||
        !ReadPod<CompressionAlgorithm>(data, offset, mapping.algorithm) ||
        !IsSupportedPayloadAlgorithm(mapping.algorithm) ||
        !ReadString(data, offset, mapping.folderId, "folderId") ||
        !ReadString(data, offset, mapping.folderName, "folderName") ||
        !ReadString(data, offset, mapping.targetPath, "targetPath") ||
        !ReadPod<SpecialDirectoryType>(data, offset, mapping.targetDirType)) {
        return false;
    }

    uint8_t appendDirectoryName = 0;
    if (!ReadPod<uint8_t>(data, offset, appendDirectoryName)) {
        return false;
    }
    mapping.appendDirectoryName = appendDirectoryName != 0;

    if (!ReadString(data, offset, mapping.customTargetPath, "customTargetPath")) {
        return false;
    }

    uint32_t fileCount = 0;
    if (!ReadPod<uint32_t>(data, offset, fileCount)) {
        return false;
    }
    mapping.fileIndex.clear();
    mapping.fileIndex.reserve(fileCount);
    for (uint32_t f = 0; f < fileCount; ++f) {
        FileIndexEntry fileEntry;
        if (!ReadString(data, offset, fileEntry.relativePath, "fileIndex.relativePath") ||
            !ReadPod<uint64_t>(data, offset, fileEntry.offset) ||
            !ReadPod<uint64_t>(data, offset, fileEntry.size)) {
            return false;
        }
        mapping.fileIndex.push_back(std::move(fileEntry));
    }

    return true;
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
    
    if (metadata.payloadMappings.size() != metadata.folderCount) {
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
    

    BinaryMetadata header{};
    size_t offset = 0;
    if (!ReadPod<BinaryMetadata>(data, offset, header) || !validateHeader(header)) {
        return metadata;
    }
    metadata.version = header.version;
    metadata.folderCount = header.folderCount;

    for (uint32_t i = 0; i < header.folderCount; ++i) {
        FolderMapping mapping;
        if (!ReadPayloadMapping(data, offset, mapping)) {
            META_LOG();
            break;
        }
        metadata.payloadMappings.push_back(mapping);
    }
    

    metadata.totalPayloadCompressedSize = 0;
    for (const auto& mapping : metadata.payloadMappings) {
        metadata.totalPayloadCompressedSize += mapping.compressedSize;
    }
    
    return metadata;
}

ExtendedInstallationMetadata MetadataParser::deserializeExtendedMetadata(const std::vector<uint8_t>& data) {
    ExtendedInstallationMetadata metadata;
    
    if (data.size() < sizeof(BinaryMetadata)) {
        META_LOG();
        return metadata;
    }
    
    BinaryMetadata header{};
    size_t offset = 0;
    if (!ReadPod<BinaryMetadata>(data, offset, header) || !validateHeader(header)) {
        return metadata;
    }
    metadata.version = header.version;
    metadata.folderCount = header.folderCount;
    
    uint32_t extendedMarker = 0;
    if (!ReadPod<uint32_t>(data, offset, extendedMarker)) {
        META_LOG();
        return metadata;
    }
    if (extendedMarker != 0x45585444) {
        META_LOG();
        return metadata;
    }
    
    if (!ReadString(data, offset, metadata.appName, "appName")) {
        return metadata;
    }
    if (header.version >= 15) {
        if (!ReadString(data, offset, metadata.appId, "appId") ||
            (header.version >= 16 &&
             !ReadString(data, offset, metadata.appDirectoryName, "appDirectoryName")) ||
            !ReadStringList(data, offset, metadata.compatibilityLegacyAppIds, "compatibilityLegacyAppIds")) {
            return metadata;
        }
        if (header.version >= 19) {
            if (!ReadString(data, offset, metadata.desktopShortcutDefaultName, "desktopShortcutDefaultName") ||
                !ReadStringMap(data, offset, metadata.desktopShortcutLocalizedNames, "desktopShortcutLocalizedNames") ||
                !ReadStringList(data,
                                offset,
                                metadata.compatibilityLegacyDesktopShortcutNames,
                                "compatibilityLegacyDesktopShortcutNames")) {
                return metadata;
            }
        } else {
            metadata.desktopShortcutDefaultName.clear();
            metadata.desktopShortcutLocalizedNames.clear();
            metadata.compatibilityLegacyDesktopShortcutNames.clear();
        }
    } else {
        metadata.appId.clear();
        metadata.appDirectoryName.clear();
        metadata.compatibilityLegacyAppIds.clear();
        metadata.desktopShortcutDefaultName.clear();
        metadata.desktopShortcutLocalizedNames.clear();
        metadata.compatibilityLegacyDesktopShortcutNames.clear();
    }
    if (!ReadString(data, offset, metadata.installDefaultDir, "installDefaultDir") ||
        !ReadString(data, offset, metadata.appVersion, "appVersion")) {
        return metadata;
    }

    if (header.version >= 10) {
        if (!ReadString(data, offset, metadata.appWebsite, "appWebsite")) {
            return metadata;
        }
    } else {
        metadata.appWebsite.clear();
    }

    if (metadata.appId.empty()) {
        metadata.appId = metadata.appName;
    }
    if (metadata.appDirectoryName.empty()) {
        metadata.appDirectoryName = metadata.appName;
    }
    if (metadata.desktopShortcutDefaultName.empty()) {
        metadata.desktopShortcutDefaultName = metadata.appName;
    }

    if (header.version >= 7) {
        size_t flagCount = header.version >= 9 ? 4 : 3;
        if (offset + sizeof(uint8_t) * flagCount > data.size()) {
            META_LOG();
            return metadata;
        }
        metadata.installAutoStartup = data[offset] != 0;
        metadata.installDesktopIcon = data[offset + 1] != 0;
        metadata.installRequireAdmin = data[offset + 2] != 0;
        metadata.installAutoCleanOldInstall = header.version >= 9 ? (data[offset + 3] != 0) : false;
        offset += sizeof(uint8_t) * flagCount;
    } else {
        if (offset + sizeof(uint8_t) * 2 > data.size()) {
            META_LOG();
            return metadata;
        }
        metadata.installAutoStartup = data[offset] != 0;
        metadata.installDesktopIcon = data[offset + 1] != 0;
        metadata.installRequireAdmin = false;
        metadata.installAutoCleanOldInstall = false;
        offset += sizeof(uint8_t) * 2;
    }

    if (header.version >= 8) {
        if (!ReadPod<uint16_t>(data, offset, metadata.installMinWindowsMajor) ||
            !ReadPod<uint16_t>(data, offset, metadata.installMinWindowsMinor) ||
            !ReadPod<uint32_t>(data, offset, metadata.installMinWindowsBuild)) {
            META_LOG();
            return metadata;
        }
    } else {
        metadata.installMinWindowsMajor = 0;
        metadata.installMinWindowsMinor = 0;
        metadata.installMinWindowsBuild = 0;
    }

    if (header.version >= 6) {
        if (!ReadPod<uint64_t>(data, offset, metadata.installSparseFileThresholdBytes)) {
            META_LOG();
            return metadata;
        }
    } else {
        metadata.installSparseFileThresholdBytes = 4 * 1024 * 1024;
    }

    if (offset + sizeof(uint8_t) * 2 > data.size()) {
        META_LOG();
        return metadata;
    }
    metadata.installStateConfig.mode = static_cast<InstallStateMode>(data[offset]);
    metadata.installStateConfig.useMutex = data[offset + 1] != 0;
    offset += sizeof(uint8_t) * 2;

    if (!ReadString(data, offset, metadata.installStateConfig.registryPath, "installState.registryPath") ||
        !ReadString(data, offset, metadata.installStateConfig.registryKey, "installState.registryKey") ||
        !ReadString(data, offset, metadata.installStateConfig.filePath, "installState.filePath") ||
        !ReadString(data, offset, metadata.installStateConfig.mutexName, "installState.mutexName") ||
        !ReadRegistryList(data, offset, metadata.lifecycleInstallRegistry, "lifecycleInstallRegistry")) {
        return metadata;
    }
    
    if (header.version >= 12) {
        if (!ReadStringList(data, offset, metadata.installKillProcesses, "installKillProcesses")) {
            return metadata;
        }
    } else {
        metadata.installKillProcesses.clear();
    }

    if (header.version >= 13) {
        if (!ReadComponentList(data, offset, metadata.layoutComponents)) {
            return metadata;
        }
        if (!ReadComponentUiConfig(data, offset, metadata.uiComponentSelection)) {
            return metadata;
        }
        if (header.version >= 14) {
            if (!ReadUiLinks(data, offset, metadata.uiLinkBindings)) {
                return metadata;
            }
        } else {
            metadata.uiLinkBindings.clear();
        }
        if (header.version >= 18) {
            if (!ReadCleanupRules(data, offset, metadata.lifecycleUninstallCleanupRules)) {
                return metadata;
            }
        } else {
            metadata.lifecycleUninstallCleanupRules.clear();
        }
        if (header.version >= 21) {
            uint8_t deleteFromManifest = 0;
            if (!ReadPod<uint8_t>(data, offset, deleteFromManifest)) {
                META_LOG();
                return metadata;
            }
            metadata.lifecycleUpgradeCleanup.registry.deleteFromManifest = deleteFromManifest != 0;
            if (!ReadRegistryList(data,
                                  offset,
                                  metadata.lifecycleUpgradeCleanup.registry.legacyKeys,
                                  "lifecycleUpgradeCleanup.registry.legacyKeys") ||
                !ReadCleanupRules(data, offset, metadata.lifecycleUpgradeCleanup.extraPaths)) {
                return metadata;
            }
        } else {
            metadata.lifecycleUpgradeCleanup = UpgradeCleanupConfig{};
        }
    } else {
        metadata.layoutComponents.clear();
        metadata.uiComponentSelection = UiComponentSelectionConfig();
        metadata.uiLinkBindings.clear();
        metadata.lifecycleUninstallCleanupRules.clear();
        metadata.lifecycleUpgradeCleanup = UpgradeCleanupConfig{};
    }

    for (uint32_t i = 0; i < header.folderCount; ++i) {
        ExtendedFolderMapping mapping;
        if (!ReadExtendedPayloadMapping(data, offset, mapping)) {
            META_LOG();
            return metadata;
        }
        
        metadata.extendedPayloadMappings.push_back(mapping);
        
        FolderMapping baseMapping;
        baseMapping.folderId = mapping.folderId;
        baseMapping.folderName = mapping.folderName;
        baseMapping.targetPath = mapping.targetPath;
        baseMapping.offset = mapping.offset;
        baseMapping.compressedSize = mapping.compressedSize;
        baseMapping.originalSize = mapping.originalSize;
        baseMapping.checksum = mapping.checksum;
        baseMapping.algorithm = mapping.algorithm;
        metadata.payloadMappings.push_back(baseMapping);
    }
    
    metadata.totalPayloadCompressedSize = 0;
    for (const auto& mapping : metadata.payloadMappings) {
        metadata.totalPayloadCompressedSize += mapping.compressedSize;
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

