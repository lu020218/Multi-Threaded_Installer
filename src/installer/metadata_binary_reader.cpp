#include "installer/metadata_binary_reader.h"

#include "installer/metadata_parser.h"
#include "common/installer_logger.h"

#include <string>
#include <utility>

namespace MultiThreadedInstaller {
namespace {

#define META_LOG() \
    do { \
        logInstallerError(std::string("[Metadata] Parse/read error at line ") + std::to_string(__LINE__)); \
    } while (0)

}  // namespace

bool IsSupportedMetadataVersion(uint32_t version) {
    return version == Constants::VERSION;
}

bool ReadString(const std::vector<uint8_t>& data,
                size_t& offset,
                std::string& out,
                const char* label) {
    (void)label;
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
    (void)label;
    uint32_t count = 0;
    if (!ReadPod<uint32_t>(data, offset, count)) {
        META_LOG();
        return false;
    }
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        RegistryEntry reg;
        if (!ReadString(data, offset, reg.path, "lifecycleInstallRegistry path") ||
            !ReadString(data, offset, reg.key, "lifecycleInstallRegistry key")) {
            return false;
        }
        uint8_t valueType = 0;
        if (!ReadPod<uint8_t>(data, offset, valueType)) {
            META_LOG();
            return false;
        }
        reg.type = static_cast<RegistryValueType>(valueType);
        if (!ReadString(data, offset, reg.value, "lifecycleInstallRegistry value")) {
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
        uint8_t installAutoStartup = 0;
        if (!ReadPod<uint8_t>(data, offset, createDesktopShortcut) ||
            !ReadPod<uint8_t>(data, offset, installAutoStartup)) {
            META_LOG();
            return false;
        }
        component.createDesktopShortcut = createDesktopShortcut != 0;
        component.autoStartup = installAutoStartup != 0;

        out.push_back(std::move(component));
    }
    return true;
}

bool ReadComponentUiConfig(const std::vector<uint8_t>& data,
                           size_t& offset,
                           UiComponentSelectionConfig& out) {
    if (!ReadString(data, offset, out.mode, "uiComponentSelection.mode") ||
        !ReadString(data, offset, out.strategy, "uiComponentSelection.strategy") ||
        !ReadString(data, offset, out.tokenPrefix, "uiComponentSelection.tokenPrefix")) {
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
        if (!ReadString(data, offset, page.skin, "uiComponentSelection.pages.skin") ||
            !ReadStringList(data, offset, page.controls, "uiComponentSelection.pages.controls")) {
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

}  // namespace MultiThreadedInstaller
