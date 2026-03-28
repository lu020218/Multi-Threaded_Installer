#include "packager/metadata_generator.h"
#include "common/utf8_utils.h"
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace MultiThreadedInstaller {
namespace {

template <typename T>
void AppendPod(std::vector<uint8_t>& out, const T& value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

void AppendString(std::vector<uint8_t>& out, const std::string& value) {
    uint32_t len = static_cast<uint32_t>(value.size());
    AppendPod(out, len);
    out.insert(out.end(), value.begin(), value.end());
}

void AppendStringList(std::vector<uint8_t>& out, const std::vector<std::string>& values) {
    uint32_t count = static_cast<uint32_t>(values.size());
    AppendPod(out, count);
    for (const auto& value : values) {
        AppendString(out, value);
    }
}

void AppendStringMap(std::vector<uint8_t>& out,
                     const std::unordered_map<std::string, std::string>& values) {
    uint32_t count = static_cast<uint32_t>(values.size());
    AppendPod(out, count);
    std::vector<std::pair<std::string, std::string>> ordered(values.begin(), values.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& item : ordered) {
        AppendString(out, item.first);
        AppendString(out, item.second);
    }
}

void AppendRegistryList(std::vector<uint8_t>& out, const std::vector<RegistryEntry>& values) {
    uint32_t count = static_cast<uint32_t>(values.size());
    AppendPod(out, count);
    for (const auto& reg : values) {
        AppendString(out, reg.path);
        AppendString(out, reg.key);
        uint8_t valueType = static_cast<uint8_t>(reg.type);
        out.push_back(valueType);
        AppendString(out, reg.value);
    }
}

void AppendUiLinks(std::vector<uint8_t>& out, const std::vector<UiLinkBinding>& values) {
    uint32_t count = static_cast<uint32_t>(values.size());
    AppendPod(out, count);
    for (const auto& link : values) {
        AppendString(out, link.control);
        AppendString(out, link.url);
    }
}

void AppendCleanupRules(std::vector<uint8_t>& out, const std::vector<UninstallCleanupRule>& values) {
    uint32_t count = static_cast<uint32_t>(values.size());
    AppendPod(out, count);
    for (const auto& rule : values) {
        AppendString(out, rule.path);
        out.push_back(rule.recursive ? 1 : 0);
        out.push_back(rule.onlyIfEmpty ? 1 : 0);
    }
}

std::string ResolveEffectiveAppIdLocal(const std::string& appId, const std::string& applicationName) {
    return appId.empty() ? applicationName : appId;
}

std::string ResolveEffectiveDirectoryNameLocal(const std::string& directoryName,
                                               const std::string& applicationName) {
    return directoryName.empty() ? applicationName : directoryName;
}

} // namespace

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
    

    metadata.applicationName = config.applicationName;
    metadata.appId = ResolveEffectiveAppIdLocal(config.appId, config.applicationName);
    metadata.directoryName =
        ResolveEffectiveDirectoryNameLocal(config.directoryName, config.applicationName);
    metadata.legacyAppIds = config.legacyAppIds;
    metadata.desktopShortcutName = config.desktopShortcutName.empty()
                                       ? config.applicationName
                                       : config.desktopShortcutName;
    metadata.desktopShortcutNameI18n = config.desktopShortcutNameI18n;
    metadata.legacyDesktopShortcutNames = config.legacyDesktopShortcutNames;
    metadata.configVersion = config.version;
    metadata.defaultInstallDir = config.defaultInstallDir;
    metadata.webPageUrl = config.webPageUrl;
    metadata.autoStartup = config.autoStartup;
    metadata.desktopIcons = config.desktopIcons;
    metadata.autoCleanOldInstall = config.autoCleanOldInstall;
    metadata.requireAdmin = config.requireAdmin;
    metadata.minWindowsMajor = config.minWindowsMajor;
    metadata.minWindowsMinor = config.minWindowsMinor;
    metadata.minWindowsBuild = config.minWindowsBuild;
    metadata.sparseFileThresholdBytes = config.sparseFileThresholdBytes;
    metadata.installState = config.installState;
    metadata.registry = config.registry;
    metadata.installKillProcesses = config.installKillProcesses;
    metadata.components = config.components;
    metadata.componentUi = config.componentUi;
    metadata.uiLinks = config.uiLinks;
    metadata.uninstallCleanupRules = config.uninstallCleanupRules;
    metadata.upgradeCleanup = config.upgradeCleanup;
    
    uint64_t currentOffset = 0;
    for (size_t i = 0; i < results.size() && i < folderInfos.size(); ++i) {

        ExtendedFolderMapping extMapping = createExtendedFolderMapping(results[i], folderInfos[i], currentOffset, config);
        metadata.extendedMappings.push_back(extMapping);
        

        FolderMapping baseMapping = createFolderMapping(results[i], folderInfos[i], currentOffset);
        metadata.folderMappings.push_back(baseMapping);
        
        currentOffset += results[i].compressedSize;
    }
    
    return metadata;
}

std::vector<uint8_t> MetadataGenerator::serializeMetadata(const InstallationMetadata& metadata) {
    std::vector<uint8_t> serialized;
    

    size_t stringDataSize = 0;
    for (const auto& mapping : metadata.folderMappings) {
        stringDataSize += mapping.folderName.length() + 1; // +1 for null terminator
        stringDataSize += mapping.targetPath.length() + 1; // +1 for null terminator
    }
    

    BinaryMetadata header;
    header.magic = Constants::MAGIC_NUMBER;
    header.version = metadata.version;
    header.folderCount = metadata.folderCount;
    header.metadataSize = sizeof(BinaryMetadata) + 
                         metadata.folderMappings.size() * (sizeof(uint64_t) * 4 + sizeof(uint32_t) * 2) + 
                         stringDataSize;
    header.dataOffset = header.metadataSize;
    

    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    serialized.insert(serialized.end(), headerBytes, headerBytes + sizeof(BinaryMetadata));
    

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
    AppendPod(serialized, extendedMarker);
    
    AppendString(serialized, metadata.applicationName);
    if (header.version >= 15) {
        AppendString(serialized, metadata.appId);
        if (header.version >= 16) {
            AppendString(serialized, metadata.directoryName);
        }
        AppendStringList(serialized, metadata.legacyAppIds);
        if (header.version >= 19) {
            AppendString(serialized, metadata.desktopShortcutName);
            AppendStringMap(serialized, metadata.desktopShortcutNameI18n);
            AppendStringList(serialized, metadata.legacyDesktopShortcutNames);
        }
    }
    
    AppendString(serialized, metadata.defaultInstallDir);

    AppendString(serialized, metadata.configVersion);

    AppendString(serialized, metadata.webPageUrl);

    uint8_t autoStartupFlag = metadata.autoStartup ? 1 : 0;
    uint8_t desktopIconsFlag = metadata.desktopIcons ? 1 : 0;
    uint8_t requireAdminFlag = metadata.requireAdmin ? 1 : 0;
    uint8_t autoCleanFlag = metadata.autoCleanOldInstall ? 1 : 0;
    serialized.push_back(autoStartupFlag);
    serialized.push_back(desktopIconsFlag);
    serialized.push_back(requireAdminFlag);
    serialized.push_back(autoCleanFlag);

    AppendPod(serialized, metadata.minWindowsMajor);
    AppendPod(serialized, metadata.minWindowsMinor);
    AppendPod(serialized, metadata.minWindowsBuild);

    AppendPod(serialized, metadata.sparseFileThresholdBytes);

    uint8_t installMode = static_cast<uint8_t>(metadata.installState.mode);
    uint8_t installMutex = metadata.installState.useMutex ? 1 : 0;
    serialized.push_back(installMode);
    serialized.push_back(installMutex);
    
    AppendString(serialized, metadata.installState.registryPath);
    AppendString(serialized, metadata.installState.registryKey);
    AppendString(serialized, metadata.installState.filePath);
    AppendString(serialized, metadata.installState.mutexName);

    AppendRegistryList(serialized, metadata.registry);
    AppendStringList(serialized, metadata.installKillProcesses);

    if (header.version >= 13) {
        uint32_t componentCount = static_cast<uint32_t>(metadata.components.size());
        AppendPod(serialized, componentCount);
        for (const auto& component : metadata.components) {
            AppendString(serialized, component.id);
            AppendString(serialized, component.name);
            AppendString(serialized, component.description);

            serialized.push_back(component.required ? 1 : 0);
            serialized.push_back(component.defaultSelected ? 1 : 0);
            AppendPod(serialized, component.sizeHintMB);

            AppendStringList(serialized, component.dependsOn);
            AppendStringList(serialized, component.folders);

            serialized.push_back(static_cast<uint8_t>(component.source.type));

            AppendString(serialized, component.source.local.base);
            AppendString(serialized, component.source.local.installer);
            AppendString(serialized, component.source.local.args);
            serialized.push_back(component.source.local.wait ? 1 : 0);
            AppendPod(serialized, component.source.local.timeoutSec);
            AppendString(serialized, component.source.local.uninstall);

            AppendString(serialized, component.source.download.url);
            AppendString(serialized, component.source.download.sha256);
            AppendString(serialized, component.source.download.saveAs);
            AppendString(serialized, component.source.download.args);
            serialized.push_back(component.source.download.wait ? 1 : 0);
            AppendPod(serialized, component.source.download.timeoutSec);
            AppendString(serialized, component.source.download.uninstall);

            AppendRegistryList(serialized, component.registry);
            AppendStringList(serialized, component.killProcesses);

            serialized.push_back(component.createDesktopShortcut ? 1 : 0);
            serialized.push_back(component.autoStartup ? 1 : 0);
        }

        AppendString(serialized, metadata.componentUi.mode);
        AppendString(serialized, metadata.componentUi.strategy);
        AppendString(serialized, metadata.componentUi.tokenPrefix);
        uint32_t pageCount = static_cast<uint32_t>(metadata.componentUi.pages.size());
        AppendPod(serialized, pageCount);
        for (const auto& page : metadata.componentUi.pages) {
            AppendString(serialized, page.skin);
            AppendStringList(serialized, page.controls);
        }

        if (header.version >= 14) {
            AppendUiLinks(serialized, metadata.uiLinks);
        }
        if (header.version >= 18) {
            AppendCleanupRules(serialized, metadata.uninstallCleanupRules);
        }
        if (header.version >= 20) {
            serialized.push_back(metadata.upgradeCleanup.registry.deleteFromManifest ? 1 : 0);
            AppendRegistryList(serialized, metadata.upgradeCleanup.registry.legacyKeys);
            AppendCleanupRules(serialized, metadata.upgradeCleanup.extraPaths);
        }
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

        if (header.version >= 17) {
            serialized.push_back(extMapping.appendDirectoryName ? 1 : 0);
        }
        
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
    

    std::filesystem::path sourcePath = PathFromUtf8(folderInfo.sourcePath);
    std::string folderName = Utf8FromPath(sourcePath.filename());
    
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
    

    std::filesystem::path sourcePath = PathFromUtf8(folderInfo.sourcePath);
    std::string folderName = Utf8FromPath(sourcePath.filename());
    

    mapping.folderName = folderName;
    mapping.targetPath = folderInfo.targetPath;
    mapping.offset = offset;
    mapping.compressedSize = result.compressedSize;
    mapping.originalSize = result.originalSize;
    mapping.checksum = result.checksum;
    mapping.algorithm = result.algorithm;
    mapping.fileIndex = result.fileIndex;
    mapping.blockIndex = result.blockIndex;
    

    mapping.targetDirType = SpecialDirectoryType::INSTALL_DIRECTORY;
    mapping.customTargetPath = "";
    mapping.appendDirectoryName = true;
    
    for (const auto& folderTarget : config.folderTargets) {
        if (folderTarget.folderName == folderName) {
            mapping.targetDirType = folderTarget.dirType;
            mapping.customTargetPath = folderTarget.targetDirectory;
            mapping.appendDirectoryName = folderTarget.appendDirectoryName;
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


