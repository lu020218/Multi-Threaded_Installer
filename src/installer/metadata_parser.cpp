#include "installer/metadata_parser.h"
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

bool IsSupportedMetadataVersion(uint32_t version) {
    return version >= 5 && version <= Constants::VERSION;
}

template <typename T>
bool ReadPod(const std::vector<uint8_t>& data, size_t& offset, T& out) {
    if (offset + sizeof(T) > data.size()) {
        return false;
    }
    out = *reinterpret_cast<const T*>(data.data() + offset);
    offset += sizeof(T);
    return true;
}

bool ReadString(const std::vector<uint8_t>& data,
                size_t& offset,
                std::string& out,
                const char* label) {
    uint32_t len = 0;
    if (!ReadPod<uint32_t>(data, offset, len)) {
        META_LOG();
        return false;
    }
    if (offset + len > data.size()) {
        META_LOG();
        return false;
    }
    out.assign(reinterpret_cast<const char*>(data.data() + offset), len);
    offset += len;
    return true;
}

bool ReadStringList(const std::vector<uint8_t>& data,
                    size_t& offset,
                    std::vector<std::string>& out,
                    const char* label) {
    uint32_t count = 0;
    if (!ReadPod<uint32_t>(data, offset, count)) {
        META_LOG();
        return false;
    }
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::string value;
        if (!ReadString(data, offset, value, label)) {
            return false;
        }
        out.push_back(std::move(value));
    }
    return true;
}

bool ReadStringMap(const std::vector<uint8_t>& data,
                   size_t& offset,
                   std::unordered_map<std::string, std::string>& out,
                   const char* label) {
    uint32_t count = 0;
    if (!ReadPod<uint32_t>(data, offset, count)) {
        META_LOG();
        return false;
    }
    out.clear();
    for (uint32_t i = 0; i < count; ++i) {
        std::string key;
        std::string value;
        if (!ReadString(data, offset, key, label) ||
            !ReadString(data, offset, value, label)) {
            return false;
        }
        out.emplace(std::move(key), std::move(value));
    }
    return true;
}

bool ReadRegistryList(const std::vector<uint8_t>& data,
                      size_t& offset,
                      std::vector<RegistryEntry>& out,
                      const char* label) {
    uint32_t count = 0;
    if (!ReadPod<uint32_t>(data, offset, count)) {
        META_LOG();
        return false;
    }
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        RegistryEntry reg;
        if (!ReadString(data, offset, reg.path, "registry path") ||
            !ReadString(data, offset, reg.key, "registry key")) {
            return false;
        }
        uint8_t valueType = 0;
        if (!ReadPod<uint8_t>(data, offset, valueType)) {
            META_LOG();
            return false;
        }
        reg.type = static_cast<RegistryValueType>(valueType);
        if (!ReadString(data, offset, reg.value, "registry value")) {
            return false;
        }
        out.push_back(std::move(reg));
    }
    return true;
}

bool ReadComponentList(const std::vector<uint8_t>& data,
                       size_t& offset,
                       std::vector<ComponentConfig>& out) {
    uint32_t componentCount = 0;
    if (!ReadPod<uint32_t>(data, offset, componentCount)) {
        META_LOG();
        return false;
    }
    out.clear();
    out.reserve(componentCount);
    for (uint32_t i = 0; i < componentCount; ++i) {
        ComponentConfig component;
        if (!ReadString(data, offset, component.id, "component.id") ||
            !ReadString(data, offset, component.name, "component.name") ||
            !ReadString(data, offset, component.description, "component.description")) {
            return false;
        }

        uint8_t required = 0;
        uint8_t defaultSelected = 0;
        if (!ReadPod<uint8_t>(data, offset, required) ||
            !ReadPod<uint8_t>(data, offset, defaultSelected)) {
            META_LOG();
            return false;
        }
        component.required = required != 0;
        component.defaultSelected = defaultSelected != 0;

        if (!ReadPod<uint32_t>(data, offset, component.sizeHintMB)) {
            META_LOG();
            return false;
        }

        if (!ReadStringList(data, offset, component.dependsOn, "component.dependsOn") ||
            !ReadStringList(data, offset, component.folders, "component.folders")) {
            return false;
        }

        uint8_t sourceType = 0;
        if (!ReadPod<uint8_t>(data, offset, sourceType)) {
            META_LOG();
            return false;
        }
        component.source.type = static_cast<ComponentSourceType>(sourceType);

        if (!ReadString(data, offset, component.source.local.base, "component.source.local.base") ||
            !ReadString(data, offset, component.source.local.installer, "component.source.local.installer") ||
            !ReadString(data, offset, component.source.local.args, "component.source.local.args")) {
            return false;
        }
        uint8_t localWait = 0;
        if (!ReadPod<uint8_t>(data, offset, localWait) ||
            !ReadPod<uint32_t>(data, offset, component.source.local.timeoutSec) ||
            !ReadString(data, offset, component.source.local.uninstall, "component.source.local.uninstall")) {
            return false;
        }
        component.source.local.wait = localWait != 0;

        if (!ReadString(data, offset, component.source.download.url, "component.source.download.url") ||
            !ReadString(data, offset, component.source.download.sha256, "component.source.download.sha256") ||
            !ReadString(data, offset, component.source.download.saveAs, "component.source.download.saveAs") ||
            !ReadString(data, offset, component.source.download.args, "component.source.download.args")) {
            return false;
        }
        uint8_t downloadWait = 0;
        if (!ReadPod<uint8_t>(data, offset, downloadWait) ||
            !ReadPod<uint32_t>(data, offset, component.source.download.timeoutSec) ||
            !ReadString(data, offset, component.source.download.uninstall, "component.source.download.uninstall")) {
            return false;
        }
        component.source.download.wait = downloadWait != 0;

        if (!ReadRegistryList(data, offset, component.registry, "component.registry") ||
            !ReadStringList(data, offset, component.killProcesses, "component.killProcesses")) {
            return false;
        }

        uint8_t createDesktopShortcut = 0;
        uint8_t autoStartup = 0;
        if (!ReadPod<uint8_t>(data, offset, createDesktopShortcut) ||
            !ReadPod<uint8_t>(data, offset, autoStartup)) {
            META_LOG();
            return false;
        }
        component.createDesktopShortcut = createDesktopShortcut != 0;
        component.autoStartup = autoStartup != 0;

        out.push_back(std::move(component));
    }
    return true;
}

bool ReadComponentUiConfig(const std::vector<uint8_t>& data,
                           size_t& offset,
                           UiComponentSelectionConfig& out) {
    if (!ReadString(data, offset, out.mode, "componentUi.mode") ||
        !ReadString(data, offset, out.strategy, "componentUi.strategy") ||
        !ReadString(data, offset, out.tokenPrefix, "componentUi.tokenPrefix")) {
        return false;
    }

    uint32_t pageCount = 0;
    if (!ReadPod<uint32_t>(data, offset, pageCount)) {
        META_LOG();
        return false;
    }
    out.pages.clear();
    out.pages.reserve(pageCount);
    for (uint32_t i = 0; i < pageCount; ++i) {
        UiComponentBindingPage page;
        if (!ReadString(data, offset, page.skin, "componentUi.pages.skin") ||
            !ReadStringList(data, offset, page.controls, "componentUi.pages.controls")) {
            return false;
        }
        out.pages.push_back(std::move(page));
    }
    return true;
}

bool ReadUiLinks(const std::vector<uint8_t>& data,
                 size_t& offset,
                 std::vector<UiLinkBinding>& out) {
    uint32_t count = 0;
    if (!ReadPod<uint32_t>(data, offset, count)) {
        META_LOG();
        return false;
    }
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        UiLinkBinding link;
        if (!ReadString(data, offset, link.control, "ui.links.control") ||
            !ReadString(data, offset, link.url, "ui.links.url")) {
            return false;
        }
        out.push_back(std::move(link));
    }
    return true;
}

bool ReadCleanupRules(const std::vector<uint8_t>& data,
                      size_t& offset,
                      std::vector<UninstallCleanupRule>& out) {
    uint32_t count = 0;
    if (!ReadPod<uint32_t>(data, offset, count)) {
        META_LOG();
        return false;
    }
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        UninstallCleanupRule rule;
        if (!ReadString(data, offset, rule.path, "cleanup.path")) {
            return false;
        }
        uint8_t recursive = 0;
        uint8_t onlyIfEmpty = 0;
        if (!ReadPod<uint8_t>(data, offset, recursive) ||
            !ReadPod<uint8_t>(data, offset, onlyIfEmpty)) {
            META_LOG();
            return false;
        }
        rule.recursive = recursive != 0;
        rule.onlyIfEmpty = onlyIfEmpty != 0;
        out.push_back(std::move(rule));
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
        if (header->version >= 20) {
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
        
        if (offset + sizeof(uint32_t) > data.size()) {
            META_LOG();
            return metadata;
        }
        uint32_t blockCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        mapping.blockIndex.reserve(blockCount);
        for (uint32_t b = 0; b < blockCount; ++b) {
            if (offset + sizeof(uint32_t) + sizeof(uint64_t) * 3 + sizeof(uint32_t) > data.size()) {
                META_LOG();
                return metadata;
            }
            BlockIndexEntry blockEntry;
            blockEntry.blockId = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            blockEntry.offset = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            blockEntry.compressedSize = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            blockEntry.originalSize = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            blockEntry.checksum = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            mapping.blockIndex.push_back(std::move(blockEntry));
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

std::vector<uint8_t> MetadataParser::readCompressedData(uint64_t offset, uint64_t size) {
    if (!dataPackagePath_.empty()) {
        return readExternalCompressedData(offset, size);
    }
    
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
    

    uint64_t absoluteOffset = locator.dataOffset + offset;
    

    if (absoluteOffset >= logicalEnd ||
        absoluteOffset + size > logicalEnd) {
        META_LOG();
        return {};
    }
    

    file.seekg(absoluteOffset);
    std::vector<uint8_t> compressedData(size);
    file.read(reinterpret_cast<char*>(compressedData.data()), size);
    
    if (file.gcount() != static_cast<std::streamsize>(size)) {
        META_LOG();
        return {};
    }
    
    return compressedData;
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

std::vector<uint8_t> MetadataParser::readExternalCompressedData(uint64_t offset, uint64_t size) {
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
    
    uint64_t absoluteOffset = header.dataOffset + offset;
    if (absoluteOffset + size > header.dataOffset + header.dataSize) {
        META_LOG();
        return {};
    }
    
    file.seekg(static_cast<std::streamoff>(absoluteOffset));
    std::vector<uint8_t> compressedData(size);
    file.read(reinterpret_cast<char*>(compressedData.data()), size);
    
    if (file.gcount() != static_cast<std::streamsize>(size)) {
        META_LOG();
        return {};
    }
    
    return compressedData;
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

