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

std::string ResolveEffectiveAppIdLocal(const std::string& appId, const std::string& appName) {
    return appId.empty() ? appName : appId;
}

std::string ResolveEffectiveDirectoryNameLocal(const std::string& appDirectoryName,
                                               const std::string& appName) {
    return appDirectoryName.empty() ? appName : appDirectoryName;
}

std::string FolderSourceName(const std::string& source) {
    return Utf8FromPath(PathFromUtf8(source).filename());
}

void AppendRegistryLookupList(std::vector<uint8_t>& out,
                              const std::vector<RegistryLookupEntry>& values) {
    uint32_t count = static_cast<uint32_t>(values.size());
    AppendPod(out, count);
    for (const auto& entry : values) {
        AppendString(out, entry.path);
        AppendString(out, entry.key);
    }
}

void AppendNamedCleanupList(std::vector<uint8_t>& out,
                            const std::vector<NamedCleanupEntry>& values) {
    uint32_t count = static_cast<uint32_t>(values.size());
    AppendPod(out, count);
    for (const auto& entry : values) {
        AppendString(out, entry.name);
    }
}

void AppendUninstallEntryCleanupList(std::vector<uint8_t>& out,
                                     const std::vector<UninstallEntryCleanup>& values) {
    uint32_t count = static_cast<uint32_t>(values.size());
    AppendPod(out, count);
    for (const auto& entry : values) {
        AppendString(out, entry.name);
        out.push_back(static_cast<uint8_t>(entry.scope));
    }
}

void AppendInstallInfoConfig(std::vector<uint8_t>& out, const InstallInfoConfig& installInfo) {
    out.push_back(static_cast<uint8_t>(installInfo.mode));
    AppendString(out, installInfo.path);
    std::vector<std::pair<std::string, InstallInfoValueConfig>> ordered(installInfo.values.begin(),
                                                                        installInfo.values.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    uint32_t count = static_cast<uint32_t>(ordered.size());
    AppendPod(out, count);
    for (const auto& item : ordered) {
        AppendString(out, item.first);
        AppendString(out, item.second.key);
        out.push_back(static_cast<uint8_t>(item.second.type));
        AppendString(out, item.second.value);
    }
}

} // namespace

ExtendedInstallationMetadata MetadataGenerator::generateExtendedMetadata(const std::vector<CompressionResult>& results,
                                                                        const std::vector<FolderInfo>& folderInfos,
                                                                        const PackagerConfiguration& config) {
    ExtendedInstallationMetadata metadata;
    metadata.version = Constants::VERSION;
    metadata.folderCount = static_cast<uint32_t>(results.size());
    metadata.totalPayloadCompressedSize = calculateTotalPayloadCompressedSize(results);
    

    metadata.appName = config.app.name;
    metadata.appId = ResolveEffectiveAppIdLocal(config.app.id, config.app.name);
    metadata.appDirectoryName =
        ResolveEffectiveDirectoryNameLocal(config.app.directoryName, config.app.name);
    metadata.desktopShortcutDefaultName = config.ui.desktopShortcut.defaultName.empty()
                                       ? config.app.name
                                       : config.ui.desktopShortcut.defaultName;
    metadata.desktopShortcutLocalizedNames = config.ui.desktopShortcut.i18n;
    metadata.appVersion = config.app.version;
    metadata.installDefaultDir = config.install.defaultDir;
    metadata.appWebsite = config.app.website;
    metadata.installAutoStartup = config.install.autoStartup;
    metadata.installDesktopIcon = config.install.desktopIcon;
    metadata.installAutoCleanOldInstall = config.install.autoCleanOldInstall;
    metadata.installRequireAdmin = config.install.requireAdmin;
    metadata.installMinWindowsMajor = config.install.minWindows.major;
    metadata.installMinWindowsMinor = config.install.minWindows.minor;
    metadata.installMinWindowsBuild = config.install.minWindows.build;
    metadata.installSparseFileThresholdBytes = config.install.sparseFileThresholdBytes;
    metadata.installUseMutex = config.install.useMutex;
    metadata.installMutexName = config.install.mutexName;
    metadata.installInfo = config.install.installInfo;
    metadata.lifecycleInstallRegistry = config.lifecycle.registry.onInstall;
    metadata.installKillProcesses = config.install.killProcesses;
    metadata.layoutComponents = config.layout.components;
    metadata.uiComponentSelection = config.ui.componentSelection;
    metadata.uiLinkBindings = config.ui.links;
    metadata.lifecycleUninstallCleanup = config.lifecycle.cleanup.onUninstall;
    metadata.lifecycleUpgradeCleanup = config.lifecycle.cleanup.onUpgrade;
    
    uint64_t currentOffset = 0;
    for (size_t i = 0; i < results.size() && i < folderInfos.size(); ++i) {

        ExtendedFolderMapping extMapping = createExtendedFolderMapping(results[i], folderInfos[i], currentOffset, config);
        metadata.extendedPayloadMappings.push_back(extMapping);
        

        FolderMapping baseMapping = createFolderMapping(results[i], folderInfos[i], currentOffset);
        metadata.payloadMappings.push_back(baseMapping);
        
        currentOffset += results[i].compressedSize;
    }
    
    return metadata;
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
    
    AppendString(serialized, metadata.appName);
    AppendString(serialized, metadata.appId);
    AppendString(serialized, metadata.appDirectoryName);
    AppendString(serialized, metadata.installDefaultDir);
    AppendString(serialized, metadata.appVersion);
    AppendString(serialized, metadata.appWebsite);
    AppendString(serialized, metadata.desktopShortcutDefaultName);
    AppendStringMap(serialized, metadata.desktopShortcutLocalizedNames);
    uint8_t installAutoStartupFlag = metadata.installAutoStartup ? 1 : 0;
    uint8_t installDesktopIconFlag = metadata.installDesktopIcon ? 1 : 0;
    uint8_t installRequireAdminFlag = metadata.installRequireAdmin ? 1 : 0;
    uint8_t autoCleanFlag = metadata.installAutoCleanOldInstall ? 1 : 0;
    serialized.push_back(installAutoStartupFlag);
    serialized.push_back(installDesktopIconFlag);
    serialized.push_back(installRequireAdminFlag);
    serialized.push_back(autoCleanFlag);

    AppendPod(serialized, metadata.installMinWindowsMajor);
    AppendPod(serialized, metadata.installMinWindowsMinor);
    AppendPod(serialized, metadata.installMinWindowsBuild);

    AppendPod(serialized, metadata.installSparseFileThresholdBytes);
    serialized.push_back(metadata.installUseMutex ? 1 : 0);
    AppendString(serialized, metadata.installMutexName);
    AppendInstallInfoConfig(serialized, metadata.installInfo);
    AppendRegistryList(serialized, metadata.lifecycleInstallRegistry);
    AppendStringList(serialized, metadata.installKillProcesses);
    uint32_t componentCount = static_cast<uint32_t>(metadata.layoutComponents.size());
    AppendPod(serialized, componentCount);
    for (const auto& component : metadata.layoutComponents) {
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

    AppendString(serialized, metadata.uiComponentSelection.mode);
    AppendString(serialized, metadata.uiComponentSelection.strategy);
    AppendString(serialized, metadata.uiComponentSelection.tokenPrefix);
    uint32_t pageCount = static_cast<uint32_t>(metadata.uiComponentSelection.pages.size());
    AppendPod(serialized, pageCount);
    for (const auto& page : metadata.uiComponentSelection.pages) {
        AppendString(serialized, page.skin);
        AppendStringList(serialized, page.controls);
    }

    AppendUiLinks(serialized, metadata.uiLinkBindings);
    AppendNamedCleanupList(serialized, metadata.lifecycleUninstallCleanup.processes);
    AppendRegistryList(serialized, metadata.lifecycleUninstallCleanup.registry.legacyKeys);
    AppendUninstallEntryCleanupList(serialized, metadata.lifecycleUninstallCleanup.uninstallEntries);
    AppendNamedCleanupList(serialized, metadata.lifecycleUninstallCleanup.shortcuts);
    AppendNamedCleanupList(serialized, metadata.lifecycleUninstallCleanup.startup);
    AppendCleanupRules(serialized, metadata.lifecycleUninstallCleanup.paths);
    AppendRegistryLookupList(serialized, metadata.lifecycleUpgradeCleanup.installRoots);
    AppendRegistryList(serialized, metadata.lifecycleUpgradeCleanup.registry.legacyKeys);
    AppendUninstallEntryCleanupList(serialized, metadata.lifecycleUpgradeCleanup.uninstallEntries);
    AppendNamedCleanupList(serialized, metadata.lifecycleUpgradeCleanup.shortcuts);
    AppendNamedCleanupList(serialized, metadata.lifecycleUpgradeCleanup.startup);
    AppendCleanupRules(serialized, metadata.lifecycleUpgradeCleanup.extraPaths);

    for (size_t i = 0; i < metadata.extendedPayloadMappings.size(); ++i) {
        const auto& extMapping = metadata.extendedPayloadMappings[i];
        
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
        
        uint32_t folderIdLen = static_cast<uint32_t>(extMapping.folderId.length());
        const uint8_t* folderIdLenBytes = reinterpret_cast<const uint8_t*>(&folderIdLen);
        serialized.insert(serialized.end(), folderIdLenBytes, folderIdLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), extMapping.folderId.begin(), extMapping.folderId.end());

        uint32_t folderNameLen = static_cast<uint32_t>(extMapping.folderName.length());
        const uint8_t* folderNameLenBytes = reinterpret_cast<const uint8_t*>(&folderNameLen);
        serialized.insert(serialized.end(), folderNameLenBytes, folderNameLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), extMapping.folderName.begin(), extMapping.folderName.end());
        
        uint32_t targetPathLen = static_cast<uint32_t>(extMapping.targetPath.length());
        const uint8_t* targetPathLenBytes = reinterpret_cast<const uint8_t*>(&targetPathLen);
        serialized.insert(serialized.end(), targetPathLenBytes, targetPathLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), extMapping.targetPath.begin(), extMapping.targetPath.end());
        
        uint32_t targetLen = static_cast<uint32_t>(extMapping.target.length());
        const uint8_t* targetLenBytes = reinterpret_cast<const uint8_t*>(&targetLen);
        serialized.insert(serialized.end(), targetLenBytes, targetLenBytes + sizeof(uint32_t));
        serialized.insert(serialized.end(), extMapping.target.begin(), extMapping.target.end());
        
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
    mapping.folderId = folderInfo.id.empty() ? folderName : folderInfo.id;
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
    
    mapping.folderId = folderInfo.id.empty() ? folderName : folderInfo.id;
    mapping.folderName = folderName;
    mapping.targetPath = folderInfo.targetPath;
    mapping.offset = offset;
    mapping.compressedSize = result.compressedSize;
    mapping.originalSize = result.originalSize;
    mapping.checksum = result.checksum;
    mapping.algorithm = result.algorithm;
    mapping.fileIndex = result.fileIndex;
    

    const std::string folderId = mapping.folderId;
    for (const auto& folderTarget : config.layout.folders) {
        if ((!folderId.empty() && folderTarget.id == folderId) ||
            FolderSourceName(folderTarget.source) == folderName) {
            mapping.target = folderTarget.target;
            mapping.targetPath = folderTarget.target;
            break;
        }
    }
    
    return mapping;
}

uint64_t MetadataGenerator::calculateTotalPayloadCompressedSize(const std::vector<CompressionResult>& results) {
    uint64_t total = 0;
    for (const auto& result : results) {
        total += result.compressedSize;
    }
    return total;
}

} // namespace MultiThreadedInstaller


