#include "installer/metadata_parser.h"
#include "installer/installer_helpers.h"
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
        std::cerr << "Missing " << label << " length" << std::endl;
        return false;
    }
    if (offset + len > data.size()) {
        std::cerr << "Insufficient data for " << label << std::endl;
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
        std::cerr << "Missing " << label << " count" << std::endl;
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

bool ReadRegistryList(const std::vector<uint8_t>& data,
                      size_t& offset,
                      std::vector<RegistryEntry>& out,
                      const char* label) {
    uint32_t count = 0;
    if (!ReadPod<uint32_t>(data, offset, count)) {
        std::cerr << "Missing " << label << " count" << std::endl;
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
            std::cerr << "Missing registry value type" << std::endl;
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
        std::cerr << "Missing component count" << std::endl;
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
            std::cerr << "Missing component boolean flags" << std::endl;
            return false;
        }
        component.required = required != 0;
        component.defaultSelected = defaultSelected != 0;

        if (!ReadPod<uint32_t>(data, offset, component.sizeHintMB)) {
            std::cerr << "Missing component sizeHintMB" << std::endl;
            return false;
        }

        if (!ReadStringList(data, offset, component.dependsOn, "component.dependsOn") ||
            !ReadStringList(data, offset, component.folders, "component.folders")) {
            return false;
        }

        uint8_t sourceType = 0;
        if (!ReadPod<uint8_t>(data, offset, sourceType)) {
            std::cerr << "Missing component source type" << std::endl;
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
            std::cerr << "Missing component trailing boolean flags" << std::endl;
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
        std::cerr << "Missing componentUi.pages count" << std::endl;
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
        std::cerr << "Missing ui.links count" << std::endl;
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

} // namespace

InstallationMetadata MetadataParser::parseEmbeddedMetadata() {
    std::vector<uint8_t> embeddedData = dataPackagePath_.empty() ? readEmbeddedData() : readExternalMetadata();
    if (embeddedData.empty()) {
        std::cerr << "No embedded data found" << std::endl;
        return InstallationMetadata{};
    }
    
    return deserializeMetadata(embeddedData);
}

ExtendedInstallationMetadata MetadataParser::parseExtendedEmbeddedMetadata() {
    std::vector<uint8_t> embeddedData = dataPackagePath_.empty() ? readEmbeddedData() : readExternalMetadata();
    if (embeddedData.empty()) {
        std::cerr << "No embedded data found" << std::endl;
        return ExtendedInstallationMetadata{};
    }
    
    return deserializeExtendedMetadata(embeddedData);
}

bool MetadataParser::validateMetadata(const InstallationMetadata& metadata) {
    if (!IsSupportedMetadataVersion(metadata.version)) {
        std::cerr << "Unsupported metadata version: " << metadata.version << std::endl;
        return false;
    }
    
    if (metadata.folderCount == 0) {
        std::cerr << "No folders found in metadata" << std::endl;
        return false;
    }
    
    if (metadata.folderMappings.size() != metadata.folderCount) {
        std::cerr << "Folder mapping count mismatch" << std::endl;
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
        std::cerr << "Failed to open executable file: " << executablePath << std::endl;
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
        std::cerr << "Failed to read complete metadata" << std::endl;
        return {};
    }
    
    return metadata;
}

InstallationMetadata MetadataParser::deserializeMetadata(const std::vector<uint8_t>& data) {
    InstallationMetadata metadata;
    
    if (data.size() < sizeof(BinaryMetadata)) {
        std::cerr << "Insufficient data for metadata header" << std::endl;
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
            std::cerr << "Insufficient data for folder mapping " << i << " numeric fields" << std::endl;
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
            std::cerr << "Insufficient data for folder name length" << std::endl;
            break;
        }
        
        uint32_t folderNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        
        if (offset + folderNameLen > data.size()) {
            std::cerr << "Insufficient data for folder name" << std::endl;
            break;
        }
        
        mapping.folderName = std::string(reinterpret_cast<const char*>(data.data() + offset), folderNameLen);
        offset += folderNameLen;
        

        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for target path length" << std::endl;
            break;
        }
        
        uint32_t targetPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        
        if (offset + targetPathLen > data.size()) {
            std::cerr << "Insufficient data for target path" << std::endl;
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
        std::cerr << "Insufficient data for metadata header" << std::endl;
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
        std::cerr << "Missing extended marker" << std::endl;
        return metadata;
    }
    
    uint32_t extendedMarker = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    if (extendedMarker != 0x45585444) {
        std::cerr << "Invalid extended marker" << std::endl;
        return metadata;
    }
    offset += sizeof(uint32_t);
    
    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing application name length" << std::endl;
        return metadata;
    }
    uint32_t appNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + appNameLen > data.size()) {
        std::cerr << "Insufficient data for application name" << std::endl;
        return metadata;
    }
    metadata.applicationName = std::string(reinterpret_cast<const char*>(data.data() + offset), appNameLen);
    offset += appNameLen;
    
    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install dir length" << std::endl;
        return metadata;
    }
    uint32_t installDirLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + installDirLen > data.size()) {
        std::cerr << "Insufficient data for install dir" << std::endl;
        return metadata;
    }
    metadata.defaultInstallDir = std::string(reinterpret_cast<const char*>(data.data() + offset), installDirLen);
    offset += installDirLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing config version length" << std::endl;
        return metadata;
    }
    uint32_t configVersionLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + configVersionLen > data.size()) {
        std::cerr << "Insufficient data for config version" << std::endl;
        return metadata;
    }
    metadata.configVersion = std::string(reinterpret_cast<const char*>(data.data() + offset), configVersionLen);
    offset += configVersionLen;

    if (header->version >= 10) {
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing web page URL length" << std::endl;
            return metadata;
        }
        uint32_t webUrlLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + webUrlLen > data.size()) {
            std::cerr << "Insufficient data for web page URL" << std::endl;
            return metadata;
        }
        metadata.webPageUrl = std::string(reinterpret_cast<const char*>(data.data() + offset), webUrlLen);
        offset += webUrlLen;
    } else {
        metadata.webPageUrl.clear();
    }

    if (header->version >= 7) {
        size_t flagCount = header->version >= 9 ? 4 : 3;
        if (offset + sizeof(uint8_t) * flagCount > data.size()) {
            std::cerr << "Missing startup/desktop/admin flags" << std::endl;
            return metadata;
        }
        metadata.autoStartup = data[offset] != 0;
        metadata.desktopIcons = data[offset + 1] != 0;
        metadata.requireAdmin = data[offset + 2] != 0;
        metadata.autoCleanOldInstall = header->version >= 9 ? (data[offset + 3] != 0) : false;
        offset += sizeof(uint8_t) * flagCount;
    } else {
        if (offset + sizeof(uint8_t) * 2 > data.size()) {
            std::cerr << "Missing startup/desktop flags" << std::endl;
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
            std::cerr << "Missing minimum Windows version" << std::endl;
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
            std::cerr << "Missing sparse file threshold" << std::endl;
            return metadata;
        }
        metadata.sparseFileThresholdBytes = *reinterpret_cast<const uint64_t*>(data.data() + offset);
        offset += sizeof(uint64_t);
    } else {
        metadata.sparseFileThresholdBytes = 4 * 1024 * 1024;
    }

    if (offset + sizeof(uint8_t) * 2 > data.size()) {
        std::cerr << "Missing install state flags" << std::endl;
        return metadata;
    }
    metadata.installState.mode = static_cast<InstallStateMode>(data[offset]);
    metadata.installState.useMutex = data[offset + 1] != 0;
    offset += sizeof(uint8_t) * 2;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install state registry path length" << std::endl;
        return metadata;
    }
    uint32_t regPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + regPathLen > data.size()) {
        std::cerr << "Insufficient data for install state registry path" << std::endl;
        return metadata;
    }
    metadata.installState.registryPath = std::string(reinterpret_cast<const char*>(data.data() + offset), regPathLen);
    offset += regPathLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install state registry key length" << std::endl;
        return metadata;
    }
    uint32_t regKeyLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + regKeyLen > data.size()) {
        std::cerr << "Insufficient data for install state registry key" << std::endl;
        return metadata;
    }
    metadata.installState.registryKey = std::string(reinterpret_cast<const char*>(data.data() + offset), regKeyLen);
    offset += regKeyLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install state file path length" << std::endl;
        return metadata;
    }
    uint32_t filePathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + filePathLen > data.size()) {
        std::cerr << "Insufficient data for install state file path" << std::endl;
        return metadata;
    }
    metadata.installState.filePath = std::string(reinterpret_cast<const char*>(data.data() + offset), filePathLen);
    offset += filePathLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing install state mutex name length" << std::endl;
        return metadata;
    }
    uint32_t mutexNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    if (offset + mutexNameLen > data.size()) {
        std::cerr << "Insufficient data for install state mutex name" << std::endl;
        return metadata;
    }
    metadata.installState.mutexName = std::string(reinterpret_cast<const char*>(data.data() + offset), mutexNameLen);
    offset += mutexNameLen;

    if (offset + sizeof(uint32_t) > data.size()) {
        std::cerr << "Missing registry entry count" << std::endl;
        return metadata;
    }
    uint32_t registryCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
    offset += sizeof(uint32_t);
    metadata.registry.reserve(registryCount);
    for (uint32_t r = 0; r < registryCount; ++r) {
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing registry path length" << std::endl;
            return metadata;
        }
        uint32_t pathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + pathLen > data.size()) {
            std::cerr << "Insufficient data for registry path" << std::endl;
            return metadata;
        }
        RegistryEntry reg;
        reg.path = std::string(reinterpret_cast<const char*>(data.data() + offset), pathLen);
        offset += pathLen;
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing registry key length" << std::endl;
            return metadata;
        }
        uint32_t keyLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + keyLen > data.size()) {
            std::cerr << "Insufficient data for registry key" << std::endl;
            return metadata;
        }
        reg.key = std::string(reinterpret_cast<const char*>(data.data() + offset), keyLen);
        offset += keyLen;
        
        if (offset + sizeof(uint8_t) > data.size()) {
            std::cerr << "Missing registry value type" << std::endl;
            return metadata;
        }
        reg.type = static_cast<RegistryValueType>(data[offset]);
        offset += sizeof(uint8_t);
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing registry value length" << std::endl;
            return metadata;
        }
        uint32_t valueLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + valueLen > data.size()) {
            std::cerr << "Insufficient data for registry value" << std::endl;
            return metadata;
        }
        reg.value = std::string(reinterpret_cast<const char*>(data.data() + offset), valueLen);
        offset += valueLen;
        
        metadata.registry.push_back(std::move(reg));
    }
    
    if (header->version >= 12) {
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Missing kill process count" << std::endl;
            return metadata;
        }
        uint32_t killCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        metadata.installKillProcesses.reserve(killCount);
        for (uint32_t k = 0; k < killCount; ++k) {
            if (offset + sizeof(uint32_t) > data.size()) {
                std::cerr << "Missing kill process name length" << std::endl;
                return metadata;
            }
            uint32_t nameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            if (offset + nameLen > data.size()) {
                std::cerr << "Insufficient data for kill process name" << std::endl;
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
    } else {
        metadata.components.clear();
        metadata.componentUi = UiComponentSelectionConfig();
        metadata.uiLinks.clear();
    }

    for (uint32_t i = 0; i < header->folderCount; ++i) {
        ExtendedFolderMapping mapping;
        
        if (offset + sizeof(uint64_t) * 3 + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for folder mapping numeric fields" << std::endl;
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
            std::cerr << "Insufficient data for folder name length" << std::endl;
            return metadata;
        }
        uint32_t folderNameLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + folderNameLen > data.size()) {
            std::cerr << "Insufficient data for folder name" << std::endl;
            return metadata;
        }
        mapping.folderName = std::string(reinterpret_cast<const char*>(data.data() + offset), folderNameLen);
        offset += folderNameLen;
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for target path length" << std::endl;
            return metadata;
        }
        uint32_t targetPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + targetPathLen > data.size()) {
            std::cerr << "Insufficient data for target path" << std::endl;
            return metadata;
        }
        mapping.targetPath = std::string(reinterpret_cast<const char*>(data.data() + offset), targetPathLen);
        offset += targetPathLen;
        
        if (offset + sizeof(SpecialDirectoryType) > data.size()) {
            std::cerr << "Insufficient data for target dir type" << std::endl;
            return metadata;
        }
        mapping.targetDirType = *reinterpret_cast<const SpecialDirectoryType*>(data.data() + offset);
        offset += sizeof(SpecialDirectoryType);
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for custom path length" << std::endl;
            return metadata;
        }
        uint32_t customPathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        if (offset + customPathLen > data.size()) {
            std::cerr << "Insufficient data for custom path" << std::endl;
            return metadata;
        }
        mapping.customTargetPath = std::string(reinterpret_cast<const char*>(data.data() + offset), customPathLen);
        offset += customPathLen;
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for file count" << std::endl;
            return metadata;
        }
        uint32_t fileCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        mapping.fileIndex.reserve(fileCount);
        for (uint32_t f = 0; f < fileCount; ++f) {
            if (offset + sizeof(uint32_t) > data.size()) {
                std::cerr << "Insufficient data for file path length" << std::endl;
                return metadata;
            }
            uint32_t pathLen = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(uint32_t);
            if (offset + pathLen > data.size()) {
                std::cerr << "Insufficient data for file path" << std::endl;
                return metadata;
            }
            FileIndexEntry fileEntry;
            fileEntry.relativePath = std::string(reinterpret_cast<const char*>(data.data() + offset), pathLen);
            offset += pathLen;
            if (offset + sizeof(uint64_t) * 2 > data.size()) {
                std::cerr << "Insufficient data for file entry" << std::endl;
                return metadata;
            }
            fileEntry.offset = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            fileEntry.size = *reinterpret_cast<const uint64_t*>(data.data() + offset);
            offset += sizeof(uint64_t);
            mapping.fileIndex.push_back(std::move(fileEntry));
        }
        
        if (offset + sizeof(uint32_t) > data.size()) {
            std::cerr << "Insufficient data for block count" << std::endl;
            return metadata;
        }
        uint32_t blockCount = *reinterpret_cast<const uint32_t*>(data.data() + offset);
        offset += sizeof(uint32_t);
        mapping.blockIndex.reserve(blockCount);
        for (uint32_t b = 0; b < blockCount; ++b) {
            if (offset + sizeof(uint32_t) + sizeof(uint64_t) * 3 + sizeof(uint32_t) > data.size()) {
                std::cerr << "Insufficient data for block entry" << std::endl;
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
        std::cerr << "Failed to open executable file: " << executablePath << std::endl;
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
        std::cerr << "Invalid data offset or size" << std::endl;
        return {};
    }
    

    file.seekg(absoluteOffset);
    std::vector<uint8_t> compressedData(size);
    file.read(reinterpret_cast<char*>(compressedData.data()), size);
    
    if (file.gcount() != static_cast<std::streamsize>(size)) {
        std::cerr << "Failed to read complete compressed data" << std::endl;
        return {};
    }
    
    return compressedData;
}

std::vector<uint8_t> MetadataParser::readExternalMetadata() {
    std::ifstream file(PathFromUtf8(dataPackagePath_), std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open data package: " << dataPackagePath_ << std::endl;
        return {};
    }
    
    DataPackageHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(DataPackageHeader));
    if (!file || header.magic != Constants::DATA_MAGIC_NUMBER) {
        std::cerr << "Invalid data package header" << std::endl;
        return {};
    }
    
    file.seekg(static_cast<std::streamoff>(header.metadataOffset));
    std::vector<uint8_t> metadata(header.metadataSize);
    file.read(reinterpret_cast<char*>(metadata.data()), header.metadataSize);
    
    if (file.gcount() != static_cast<std::streamsize>(header.metadataSize)) {
        std::cerr << "Failed to read complete metadata from data package" << std::endl;
        return {};
    }
    
    return metadata;
}

std::vector<uint8_t> MetadataParser::readExternalCompressedData(uint64_t offset, uint64_t size) {
    std::ifstream file(PathFromUtf8(dataPackagePath_), std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open data package: " << dataPackagePath_ << std::endl;
        return {};
    }
    
    DataPackageHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(DataPackageHeader));
    if (!file || header.magic != Constants::DATA_MAGIC_NUMBER) {
        std::cerr << "Invalid data package header" << std::endl;
        return {};
    }
    
    uint64_t absoluteOffset = header.dataOffset + offset;
    if (absoluteOffset + size > header.dataOffset + header.dataSize) {
        std::cerr << "Invalid data offset or size for data package" << std::endl;
        return {};
    }
    
    file.seekg(static_cast<std::streamoff>(absoluteOffset));
    std::vector<uint8_t> compressedData(size);
    file.read(reinterpret_cast<char*>(compressedData.data()), size);
    
    if (file.gcount() != static_cast<std::streamsize>(size)) {
        std::cerr << "Failed to read complete compressed data from data package" << std::endl;
        return {};
    }
    
    return compressedData;
}

bool MetadataParser::validateHeader(const BinaryMetadata& header) {
    if (header.magic != Constants::MAGIC_NUMBER) {
        std::cerr << "Invalid magic number in metadata header" << std::endl;
        return false;
    }
    
    if (!IsSupportedMetadataVersion(header.version)) {
        std::cerr << "Unsupported metadata version: " << header.version << std::endl;
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
        std::cerr << "Invalid end magic number, no embedded data found" << std::endl;
        return false;
    }

    locator.magic = resolvedLocator.magic;
    locator.metadataOffset = resolvedLocator.metadataOffset;
    locator.metadataSize = resolvedLocator.metadataSize;
    locator.dataOffset = resolvedLocator.dataOffset;
    locator.dataSize = resolvedLocator.dataSize;

    if (locator.metadataOffset >= logicalEnd ||
        locator.metadataOffset + locator.metadataSize > logicalEnd) {
        std::cerr << "Invalid metadata offset or size" << std::endl;
        return false;
    }

    if (locator.dataOffset >= logicalEnd ||
        locator.dataOffset + locator.dataSize > logicalEnd) {
        std::cerr << "Invalid data offset or size" << std::endl;
        return false;
    }

    return true;
}

std::string MetadataParser::getCurrentExecutablePath() {
    #ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (len == 0) {
        return std::string();
    }
    return WideToUtf8(std::wstring(buffer, len));
    #else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return std::string(buffer);
    }
    return {};
    #endif
}

} // namespace MultiThreadedInstaller

