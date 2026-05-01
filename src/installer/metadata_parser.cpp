#include "installer/metadata_parser.h"
#include "installer/metadata_binary_reader.h"
#include "common/package_manifest_codec.h"
#include "installer/package_manifest_validator.h"
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
        !ReadString(data, offset, mapping.target, "target")) {
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

ExtendedInstallationMetadata MetadataParser::deserializeExtendedMetadata(const std::vector<uint8_t>& data) {
    PackageManifest manifest;
    std::string manifestError;
    if (DeserializePackageManifest(data, manifest, manifestError)) {
        std::string validationError;
        if (!ValidatePackageManifest(manifest, validationError)) {
            META_LOG();
            return ExtendedInstallationMetadata{};
        }
        return PackageManifestToExtendedMetadata(manifest);
    }

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
    if (!ReadString(data, offset, metadata.appId, "appId") ||
        !ReadString(data, offset, metadata.appDirectoryName, "appDirectoryName")) {
        return metadata;
    }
    if (!ReadString(data, offset, metadata.installDefaultDir, "installDefaultDir") ||
        !ReadString(data, offset, metadata.appVersion, "appVersion")) {
        return metadata;
    }

    if (!ReadString(data, offset, metadata.appWebsite, "appWebsite") ||
        !ReadString(data, offset, metadata.desktopShortcutDefaultName, "desktopShortcutDefaultName") ||
        !ReadStringMap(data, offset, metadata.desktopShortcutLocalizedNames, "desktopShortcutLocalizedNames")) {
        return metadata;
    }

    if (metadata.appId.empty()) {
        metadata.appId = metadata.appName;
    }
    if (metadata.appDirectoryName.empty()) {
        metadata.appDirectoryName = metadata.appName;
    }

    if (offset + sizeof(uint8_t) * 4 > data.size()) {
        META_LOG();
        return metadata;
    }
    metadata.installAutoStartup = data[offset] != 0;
    metadata.installDesktopIcon = data[offset + 1] != 0;
    metadata.installRequireAdmin = data[offset + 2] != 0;
    metadata.installAutoCleanOldInstall = data[offset + 3] != 0;
    offset += sizeof(uint8_t) * 4;

    if (!ReadPod<uint16_t>(data, offset, metadata.installMinWindowsMajor) ||
        !ReadPod<uint16_t>(data, offset, metadata.installMinWindowsMinor) ||
        !ReadPod<uint32_t>(data, offset, metadata.installMinWindowsBuild) ||
        !ReadPod<uint64_t>(data, offset, metadata.installSparseFileThresholdBytes)) {
        META_LOG();
        return metadata;
    }

    if (offset + sizeof(uint8_t) > data.size()) {
        META_LOG();
        return metadata;
    }
    metadata.installUseMutex = data[offset] != 0;
    offset += sizeof(uint8_t);

    if (!ReadString(data, offset, metadata.installMutexName, "installMutexName") ||
        !ReadInstallInfoConfig(data, offset, metadata.installInfo) ||
        !ReadRegistryList(data, offset, metadata.lifecycleInstallRegistry, "lifecycleInstallRegistry")) {
        return metadata;
    }

    if (!ReadStringList(data, offset, metadata.installKillProcesses, "installKillProcesses")) {
        return metadata;
    }

    if (!ReadComponentList(data, offset, metadata.layoutComponents)) {
        return metadata;
    }
    if (!ReadComponentUiConfig(data, offset, metadata.uiComponentSelection)) {
        return metadata;
    }
    if (!ReadUiLinks(data, offset, metadata.uiLinkBindings) ||
        !ReadNamedCleanupList(data, offset, metadata.lifecycleUninstallCleanup.processes) ||
        !ReadRegistryList(data, offset, metadata.lifecycleUninstallCleanup.registry.legacyKeys, "onUninstall.registry.legacyKeys") ||
        !ReadUninstallEntryCleanupList(data, offset, metadata.lifecycleUninstallCleanup.uninstallEntries) ||
        !ReadNamedCleanupList(data, offset, metadata.lifecycleUninstallCleanup.shortcuts) ||
        !ReadNamedCleanupList(data, offset, metadata.lifecycleUninstallCleanup.startup) ||
        !ReadCleanupRules(data, offset, metadata.lifecycleUninstallCleanup.paths) ||
        !ReadRegistryLookupList(data, offset, metadata.lifecycleUpgradeCleanup.installRoots) ||
        !ReadRegistryList(data, offset, metadata.lifecycleUpgradeCleanup.registry.legacyKeys, "onUpgrade.registry.legacyKeys") ||
        !ReadUninstallEntryCleanupList(data, offset, metadata.lifecycleUpgradeCleanup.uninstallEntries) ||
        !ReadNamedCleanupList(data, offset, metadata.lifecycleUpgradeCleanup.shortcuts) ||
        !ReadNamedCleanupList(data, offset, metadata.lifecycleUpgradeCleanup.startup) ||
        !ReadCleanupRules(data, offset, metadata.lifecycleUpgradeCleanup.extraPaths)) {
        return metadata;
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

bool MetadataParser::validateMetadata(const ExtendedInstallationMetadata& metadata) {
    std::string error;
    if (!ValidateExtendedInstallationMetadata(metadata, error)) {
        META_LOG();
        if (!error.empty()) {
            logInstallerError("[Metadata] Validation failed: " + error);
        }
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

