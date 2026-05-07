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


