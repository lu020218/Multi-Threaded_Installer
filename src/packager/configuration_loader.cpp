#include "packager/configuration_loader.h"

#include "common/utf8_utils.h"
#include "packager/config_parse_collections.h"
#include "packager/config_value_reader.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <unordered_set>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;
using nlohmann::json;

namespace MultiThreadedInstaller {
namespace {

bool GetRequiredObject(const json& parent,
                       const char* key,
                       json& out,
                       std::string& lastError) {
    if (!parent.contains(key) || !parent[key].is_object()) {
        lastError = "Missing required field '" + std::string(key) + "'";
        return false;
    }
    out = parent[key];
    return true;
}

bool GetOptionalObject(const json& parent, const char* key, json& out) {
    if (!parent.contains(key) || !parent[key].is_object()) {
        return false;
    }
    out = parent[key];
    return true;
}

bool GetRequiredString(const json& parent,
                       const char* key,
                       std::string& out,
                       std::string& lastError) {
    if (!parent.contains(key) || !JsonValueToString(parent[key], out) || out.empty()) {
        lastError = "Missing required field '" + std::string(key) + "'";
        return false;
    }
    return true;
}

bool GetOptionalString(const json& parent, const char* key, std::string& out, std::string& lastError) {
    if (!parent.contains(key)) {
        return true;
    }
    if (!JsonValueToString(parent[key], out)) {
        lastError = "Invalid field '" + std::string(key) + "': expected string";
        return false;
    }
    return true;
}

bool GetOptionalBool(const json& parent, const char* key, bool& out, std::string& lastError) {
    if (!parent.contains(key)) {
        return true;
    }
    if (!JsonValueToBool(parent[key], out)) {
        lastError = "Invalid field '" + std::string(key) + "': expected boolean";
        return false;
    }
    return true;
}

bool GetOptionalInt(const json& parent, const char* key, int& out, std::string& lastError) {
    if (!parent.contains(key)) {
        return true;
    }
    if (!JsonValueToInt(parent[key], out)) {
        lastError = "Invalid field '" + std::string(key) + "': expected integer";
        return false;
    }
    return true;
}

bool GetOptionalUInt64(const json& parent, const char* key, uint64_t& out, std::string& lastError) {
    if (!parent.contains(key)) {
        return true;
    }
    if (!JsonValueToUInt64(parent[key], out)) {
        lastError = "Invalid field '" + std::string(key) + "': expected unsigned integer";
        return false;
    }
    return true;
}

bool GetOptionalStringList(const json& parent,
                           const char* key,
                           std::vector<std::string>& out,
                           std::string& lastError) {
    if (!parent.contains(key)) {
        return true;
    }
    if (!JsonArrayToStringList(parent[key], out)) {
        lastError = "Invalid field '" + std::string(key) + "': expected string array";
        return false;
    }
    return true;
}

bool GetOptionalStringMap(const json& parent,
                          const char* key,
                          std::unordered_map<std::string, std::string>& out,
                          std::string& lastError) {
    if (!parent.contains(key)) {
        return true;
    }
    if (!JsonObjectToStringMap(parent[key], out)) {
        lastError = "Invalid field '" + std::string(key) + "': expected string map";
        return false;
    }
    return true;
}

bool ParseCompressionAlgorithmValue(const std::string& raw,
                                    CompressionAlgorithm& out,
                                    std::string& lastError) {
    const std::string normalized = ToLowerCopy(raw);
    if (normalized == "xz") {
        out = CompressionAlgorithm::LZMA2_XZ;
        return true;
    }
    if (normalized == "zstd") {
        out = CompressionAlgorithm::ZSTD;
        return true;
    }
    lastError = "Invalid field 'package.compression.algorithm': expected 'xz' or 'zstd'";
    return false;
}

bool ParseInstallStateModeValue(const std::string& raw,
                                InstallStateMode& out,
                                std::string& lastError) {
    const std::string normalized = ToLowerCopy(raw);
    if (normalized == "registry") {
        out = InstallStateMode::REGISTRY;
        return true;
    }
    lastError = "Invalid field 'install.installInfo.mode': expected 'registry'";
    return false;
}

bool ParseUninstallEntryScopeValue(const std::string& raw,
                                   UninstallEntryScope& out,
                                   std::string& lastError,
                                   const std::string& fieldLabel) {
    const std::string normalized = ToLowerCopy(raw);
    if (normalized == "currentuser" || normalized == "user") {
        out = UninstallEntryScope::CURRENT_USER;
        return true;
    }
    if (normalized == "localmachine" || normalized == "machine") {
        out = UninstallEntryScope::LOCAL_MACHINE;
        return true;
    }
    if (normalized == "wow6432") {
        out = UninstallEntryScope::WOW6432;
        return true;
    }
    if (normalized == "any" || normalized == "auto") {
        out = UninstallEntryScope::ANY;
        return true;
    }
    if (normalized == "both") {
        out = UninstallEntryScope::BOTH;
        return true;
    }
    lastError = "Invalid field '" + fieldLabel + "': expected 'user', 'machine', 'currentUser', 'localMachine', 'wow6432', 'any', or 'both'";
    return false;
}

bool ParseSystemUninstallEntryCleanupItems(const json& arrayValue,
                                           const std::string& fieldLabel,
                                           std::vector<SystemUninstallEntryCleanupItem>& out,
                                           std::string& lastError) {
    if (!arrayValue.is_array()) {
        lastError = "Invalid field '" + fieldLabel + "': expected array";
        return false;
    }
    out.clear();
    for (const auto& item : arrayValue) {
        if (!item.is_object()) {
            lastError = "Invalid field '" + fieldLabel + "[]': expected object";
            return false;
        }
        SystemUninstallEntryCleanupItem entry;
        if (!GetRequiredString(item, "displayName", entry.displayName, lastError)) {
            lastError = "Invalid field '" + fieldLabel + "[]': missing required 'displayName'";
            return false;
        }
        std::string scope;
        if (!GetRequiredString(item, "scope", scope, lastError) ||
            !ParseUninstallEntryScopeValue(scope, entry.scope, lastError, fieldLabel + "[].scope")) {
            return false;
        }
        out.push_back(std::move(entry));
    }
    return true;
}

bool ParseSystemUninstallEntryCleanupObject(const json& owner,
                                            const std::string& fieldLabel,
                                            SystemUninstallEntryCleanupConfig& out,
                                            std::string& lastError,
                                            bool requireCurrentEntry) {
    json entry;
    if (owner.contains("systemUninstallEntry") && !owner["systemUninstallEntry"].is_object()) {
        lastError = "Unsupported field '" + fieldLabel + "': expected object";
        return false;
    }
    if (!GetOptionalObject(owner, "systemUninstallEntry", entry)) {
        return true;
    }
    if (entry.contains("displayName") &&
        !GetOptionalString(entry, "displayName", out.displayName, lastError)) {
        return false;
    }
    if (entry.contains("scope")) {
        std::string scope;
        if (!GetRequiredString(entry, "scope", scope, lastError) ||
            !ParseUninstallEntryScopeValue(scope, out.scope, lastError, fieldLabel + ".scope")) {
            return false;
        }
    }
    if (requireCurrentEntry && (out.displayName.empty() || out.scope == UninstallEntryScope::ANY)) {
        lastError = "Missing required field '" + fieldLabel + ".displayName' or '" + fieldLabel + ".scope'";
        return false;
    }
    if (entry.contains("legacyEntries") &&
        !ParseSystemUninstallEntryCleanupItems(entry["legacyEntries"],
                                               fieldLabel + ".legacyEntries",
                                               out.legacyEntries,
                                               lastError)) {
        return false;
    }
    return true;
}

bool ParseRegistryLookupArray(const json& arrayValue,
                              const std::string& fieldLabel,
                              std::vector<RegistryLookupEntry>& out,
                              std::string& lastError) {
    if (!arrayValue.is_array()) {
        lastError = "Invalid field '" + fieldLabel + "': expected array";
        return false;
    }
    out.clear();
    for (const auto& item : arrayValue) {
        if (!item.is_object()) {
            lastError = "Invalid field '" + fieldLabel + "[]': expected object";
            return false;
        }
        RegistryLookupEntry entry;
        if (!GetRequiredString(item, "path", entry.path, lastError) ||
            !GetRequiredString(item, "key", entry.key, lastError)) {
            lastError = "Invalid field '" + fieldLabel + "[]': missing required 'path' or 'key'";
            return false;
        }
        out.push_back(std::move(entry));
    }
    return true;
}

bool ParseNamedCleanupArray(const json& arrayValue,
                            const std::string& fieldLabel,
                            std::vector<NamedCleanupEntry>& out,
                            std::string& lastError) {
    if (!arrayValue.is_array()) {
        lastError = "Invalid field '" + fieldLabel + "': expected array";
        return false;
    }
    out.clear();
    for (const auto& item : arrayValue) {
        if (!item.is_object()) {
            lastError = "Invalid field '" + fieldLabel + "[]': expected object";
            return false;
        }
        NamedCleanupEntry entry;
        if (!GetRequiredString(item, "name", entry.name, lastError)) {
            lastError = "Invalid field '" + fieldLabel + "[]': missing required 'name'";
            return false;
        }
        out.push_back(std::move(entry));
    }
    return true;
}

bool ParseUninstallEntryCleanupArray(const json& arrayValue,
                                     const std::string& fieldLabel,
                                     std::vector<UninstallEntryCleanup>& out,
                                     std::string& lastError) {
    if (!arrayValue.is_array()) {
        lastError = "Invalid field '" + fieldLabel + "': expected array";
        return false;
    }
    out.clear();
    for (const auto& item : arrayValue) {
        if (!item.is_object()) {
            lastError = "Invalid field '" + fieldLabel + "[]': expected object";
            return false;
        }
        UninstallEntryCleanup entry;
        if (!GetRequiredString(item, "name", entry.name, lastError)) {
            lastError = "Invalid field '" + fieldLabel + "[]': missing required 'name'";
            return false;
        }
        std::string scope = "any";
        if (!GetOptionalString(item, "scope", scope, lastError) ||
            !ParseUninstallEntryScopeValue(scope, entry.scope, lastError, fieldLabel + "[].scope")) {
            return false;
        }
        out.push_back(std::move(entry));
    }
    return true;
}

bool ParseComponentSourceTypeValue(const std::string& raw,
                                   ComponentSourceType& out,
                                   std::string& lastError) {
    const std::string normalized = ToLowerCopy(raw);
    if (normalized == "embedded") {
        out = ComponentSourceType::EMBEDDED;
        return true;
    }
    if (normalized == "local") {
        out = ComponentSourceType::LOCAL;
        return true;
    }
    if (normalized == "download") {
        out = ComponentSourceType::DOWNLOAD;
        return true;
    }
    lastError = "Invalid field 'layout.components[].source.type': expected 'embedded', 'local', or 'download'";
    return false;
}

bool ParseAppConfig(const json& root, AppConfig& out, std::string& lastError) {
    json section;
    if (!GetRequiredObject(root, "app", section, lastError)) {
        return false;
    }
    if (!GetRequiredString(section, "name", out.name, lastError) ||
        !GetRequiredString(section, "version", out.version, lastError) ||
        !GetOptionalString(section, "id", out.id, lastError) ||
        !GetOptionalString(section, "directoryName", out.directoryName, lastError) ||
        !GetOptionalString(section, "website", out.website, lastError)) {
        return false;
    }

    json product;
    if (GetOptionalObject(section, "product", product)) {
        if (!GetOptionalString(product, "icon", out.product.iconPath, lastError) ||
            !GetOptionalString(product, "productName", out.product.productName, lastError) ||
            !GetOptionalString(product, "fileVersion", out.product.fileVersion, lastError) ||
            !GetOptionalString(product, "productVersion", out.product.productVersion, lastError) ||
            !GetOptionalString(product, "companyName", out.product.companyName, lastError) ||
            !GetOptionalString(product, "fileDescription", out.product.fileDescription, lastError) ||
            !GetOptionalString(product, "copyright", out.product.copyright, lastError)) {
            return false;
        }
    }

    return true;
}

bool ParsePackageConfig(const json& root, PackageConfig& out, std::string& lastError) {
    json section;
    json compression;
    if (!GetRequiredObject(root, "package", section, lastError) ||
        !GetRequiredObject(section, "compression", compression, lastError)) {
        if (lastError == "Missing required field 'compression'") {
            lastError = "Missing required field 'package.compression'";
        } else if (lastError == "Missing required field 'package'") {
            lastError = "Missing required field 'package'";
        }
        return false;
    }

    std::string algorithm;
    if (!GetRequiredString(compression, "algorithm", algorithm, lastError) ||
        !ParseCompressionAlgorithmValue(algorithm, out.compression.algorithm, lastError) ||
        !GetOptionalInt(compression, "level", out.compression.level, lastError)) {
        return false;
    }

    if (compression.contains("threads")) {
        if (compression["threads"].is_string()) {
            std::string value;
            if (!JsonValueToString(compression["threads"], value)) {
                lastError = "Invalid field 'package.compression.threads': expected integer or 'auto'";
                return false;
            }
            value = ToLowerCopy(value);
            if (value == "auto") {
                out.compression.threads = 0;
            } else {
                lastError = "Invalid field 'package.compression.threads': expected integer or 'auto'";
                return false;
            }
        } else if (!JsonValueToInt(compression["threads"], out.compression.threads)) {
            lastError = "Invalid field 'package.compression.threads': expected integer or 'auto'";
            return false;
        }
    }

    return true;
}

bool ParseInstallConfig(const json& root, PackagerInstallConfig& out, std::string& lastError) {
    json section;
    if (!GetRequiredObject(root, "install", section, lastError)) {
        return false;
    }
    if (!GetRequiredString(section, "defaultDir", out.defaultDir, lastError) ||
        !GetOptionalBool(section, "requireAdmin", out.requireAdmin, lastError) ||
        !GetOptionalBool(section, "autoCleanOldInstall", out.autoCleanOldInstall, lastError) ||
        !GetOptionalBool(section, "autoStartup", out.autoStartup, lastError) ||
        !GetOptionalBool(section, "desktopIcon", out.desktopIcon, lastError) ||
        !GetOptionalBool(section, "useMutex", out.useMutex, lastError) ||
        !GetOptionalString(section, "mutexName", out.mutexName, lastError) ||
        !GetOptionalUInt64(section, "sparseFileThresholdBytes", out.sparseFileThresholdBytes, lastError) ||
        !GetOptionalStringList(section, "killProcesses", out.killProcesses, lastError)) {
        return false;
    }

    json minWindows;
    if (GetOptionalObject(section, "minWindows", minWindows)) {
        uint64_t major = out.minWindows.major;
        uint64_t minor = out.minWindows.minor;
        uint64_t build = out.minWindows.build;
        if (!GetOptionalUInt64(minWindows, "major", major, lastError) ||
            !GetOptionalUInt64(minWindows, "minor", minor, lastError) ||
            !GetOptionalUInt64(minWindows, "build", build, lastError)) {
            return false;
        }
        out.minWindows.major = static_cast<uint16_t>(major);
        out.minWindows.minor = static_cast<uint16_t>(minor);
        out.minWindows.build = static_cast<uint32_t>(build);
    }

    json installInfo;
    if (GetOptionalObject(section, "installInfo", installInfo)) {
        if (installInfo.contains("mode")) {
            std::string mode;
            if (!JsonValueToString(installInfo["mode"], mode) ||
                !ParseInstallStateModeValue(mode, out.installInfo.mode, lastError)) {
                return false;
            }
        }
        if (!GetOptionalString(installInfo, "path", out.installInfo.path, lastError)) {
            return false;
        }
        json values;
        if (GetOptionalObject(installInfo, "values", values)) {
            out.installInfo.values.clear();
            for (auto it = values.begin(); it != values.end(); ++it) {
                if (!it.value().is_object()) {
                    lastError = "Invalid field 'install.installInfo.values." + it.key() + "': expected object";
                    return false;
                }
                InstallInfoValueConfig entry;
                if (!GetRequiredString(it.value(), "key", entry.key, lastError)) {
                    lastError = "Missing required field 'install.installInfo.values." + it.key() + ".key'";
                    return false;
                }
                if (it.value().contains("value")) {
                    if (it.value()["value"].is_number_integer() || it.value()["value"].is_number_unsigned()) {
                        entry.type = RegistryValueType::DWORD;
                        entry.value = std::to_string(it.value()["value"].get<uint32_t>());
                    } else if (!JsonValueToString(it.value()["value"], entry.value)) {
                        lastError = "Invalid field 'install.installInfo.values." + it.key() + ".value': expected string or integer";
                        return false;
                    }
                }
                if (it.value().contains("type")) {
                    std::string type;
                    if (!JsonValueToString(it.value()["type"], type)) {
                        lastError = "Invalid field 'install.installInfo.values." + it.key() + ".type': expected string";
                        return false;
                    }
                    type = ToLowerCopy(type);
                    if (type == "dword") {
                        entry.type = RegistryValueType::DWORD;
                    } else if (type == "expand" || type == "expand_string") {
                        entry.type = RegistryValueType::EXPAND_STRING;
                    } else {
                        entry.type = RegistryValueType::STRING;
                    }
                }
                out.installInfo.values.emplace(it.key(), std::move(entry));
            }
        }
    }

    return true;
}

bool ParseUiConfig(const json& root, UiConfig& out, std::string& lastError) {
    json section;
    if (!GetRequiredObject(root, "ui", section, lastError)) {
        return false;
    }
    if (!GetOptionalString(section, "defaultLanguage", out.defaultLanguage, lastError)) {
        return false;
    }

    json shortcut;
    if (GetOptionalObject(section, "desktopShortcut", shortcut)) {
        if (!GetOptionalString(shortcut, "defaultName", out.desktopShortcut.defaultName, lastError) ||
            !GetOptionalStringMap(shortcut, "i18n", out.desktopShortcut.i18n, lastError)) {
            return false;
        }
    }

    if (section.contains("links")) {
        if (!section["links"].is_array()) {
            lastError = "Invalid field 'ui.links': expected array";
            return false;
        }
        out.links.clear();
        for (const auto& item : section["links"]) {
            if (!item.is_object()) {
                lastError = "Invalid field 'ui.links[]': expected object";
                return false;
            }
            UiLinkBinding link;
            if (!GetRequiredString(item, "control", link.control, lastError) ||
                !GetRequiredString(item, "url", link.url, lastError)) {
                lastError = "Invalid field 'ui.links[]': missing required 'control' or 'url'";
                return false;
            }
            out.links.push_back(std::move(link));
        }
    }

    json componentSelection;
    if (GetOptionalObject(section, "componentSelection", componentSelection)) {
        if (!GetOptionalString(componentSelection, "mode", out.componentSelection.mode, lastError)) {
            return false;
        }

        json binding;
        if (GetOptionalObject(componentSelection, "binding", binding)) {
            if (!GetOptionalString(binding, "strategy", out.componentSelection.strategy, lastError) ||
                !GetOptionalString(binding, "tokenPrefix", out.componentSelection.tokenPrefix, lastError)) {
                return false;
            }
            if (binding.contains("pages")) {
                if (!binding["pages"].is_array()) {
                    lastError = "Invalid field 'ui.componentSelection.binding.pages': expected array";
                    return false;
                }
                out.componentSelection.pages.clear();
                for (const auto& pageJson : binding["pages"]) {
                    if (!pageJson.is_object()) {
                        lastError = "Invalid field 'ui.componentSelection.binding.pages[]': expected object";
                        return false;
                    }
                    UiComponentBindingPage page;
                    if (!GetRequiredString(pageJson, "skin", page.skin, lastError) ||
                        !GetOptionalStringList(pageJson, "controls", page.controls, lastError)) {
                        return false;
                    }
                    out.componentSelection.pages.push_back(std::move(page));
                }
            }
        }
    }

    return true;
}

bool ParseLayoutConfig(const json& root, LayoutConfig& out, std::string& lastError) {
    json section;
    if (!GetRequiredObject(root, "layout", section, lastError)) {
        return false;
    }
    if (!section.contains("folders") || !section["folders"].is_array() || section["folders"].empty()) {
        lastError = "Missing required field 'layout.folders'";
        return false;
    }

    out.folders.clear();
    for (const auto& folderJson : section["folders"]) {
        if (!folderJson.is_object()) {
            lastError = "Invalid field 'layout.folders[]': expected object";
            return false;
        }
        LayoutFolderConfig folder;
        if (!GetRequiredString(folderJson, "id", folder.id, lastError) ||
            !GetRequiredString(folderJson, "source", folder.source, lastError)) {
            return false;
        }
        if (!GetRequiredString(folderJson, "target", folder.target, lastError)) {
            lastError = "Missing required field 'layout.folders[].target'";
            return false;
        }
        out.folders.push_back(std::move(folder));
    }

    out.components.clear();
    if (!section.contains("components")) {
        return true;
    }
    if (!section["components"].is_array()) {
        lastError = "Invalid field 'layout.components': expected array";
        return false;
    }

    for (const auto& componentJson : section["components"]) {
        if (!componentJson.is_object()) {
            lastError = "Invalid field 'layout.components[]': expected object";
            return false;
        }

        ComponentConfig component;
        if (!GetRequiredString(componentJson, "id", component.id, lastError) ||
            !GetRequiredString(componentJson, "name", component.name, lastError) ||
            !GetOptionalString(componentJson, "description", component.description, lastError) ||
            !GetOptionalBool(componentJson, "required", component.required, lastError) ||
            !GetOptionalBool(componentJson, "defaultSelected", component.defaultSelected, lastError) ||
            !GetOptionalStringList(componentJson, "dependsOn", component.dependsOn, lastError) ||
            !GetOptionalStringList(componentJson, "folders", component.folders, lastError)) {
            return false;
        }

        if (componentJson.contains("sizeHintMB")) {
            uint64_t sizeHint = 0;
            if (!JsonValueToUInt64(componentJson["sizeHintMB"], sizeHint)) {
                lastError = "Invalid field 'layout.components[].sizeHintMB': expected unsigned integer";
                return false;
            }
            component.sizeHintMB = static_cast<uint32_t>(sizeHint);
        }

        json source;
        if (!GetRequiredObject(componentJson, "source", source, lastError)) {
            lastError = "Missing required field 'layout.components[].source'";
            return false;
        }
        std::string sourceType;
        if (!GetRequiredString(source, "type", sourceType, lastError) ||
            !ParseComponentSourceTypeValue(sourceType, component.source.type, lastError)) {
            return false;
        }

        json local;
        if (GetOptionalObject(source, "local", local)) {
            if (!GetOptionalString(local, "base", component.source.local.base, lastError) ||
                !GetOptionalString(local, "installer", component.source.local.installer, lastError) ||
                !GetOptionalString(local, "args", component.source.local.args, lastError) ||
                !GetOptionalBool(local, "wait", component.source.local.wait, lastError) ||
                !GetOptionalString(local, "uninstall", component.source.local.uninstall, lastError)) {
                return false;
            }
            uint64_t timeout = component.source.local.timeoutSec;
            if (!GetOptionalUInt64(local, "timeoutSec", timeout, lastError)) {
                return false;
            }
            component.source.local.timeoutSec = static_cast<uint32_t>(timeout);
        }

        json download;
        if (GetOptionalObject(source, "download", download)) {
            if (!GetOptionalString(download, "url", component.source.download.url, lastError) ||
                !GetOptionalString(download, "sha256", component.source.download.sha256, lastError) ||
                !GetOptionalString(download, "saveAs", component.source.download.saveAs, lastError) ||
                !GetOptionalString(download, "args", component.source.download.args, lastError) ||
                !GetOptionalBool(download, "wait", component.source.download.wait, lastError) ||
                !GetOptionalString(download, "uninstall", component.source.download.uninstall, lastError)) {
                return false;
            }
            uint64_t timeout = component.source.download.timeoutSec;
            if (!GetOptionalUInt64(download, "timeoutSec", timeout, lastError)) {
                return false;
            }
            component.source.download.timeoutSec = static_cast<uint32_t>(timeout);
        }

        json install;
        if (GetOptionalObject(componentJson, "install", install)) {
            if (install.contains("registry") &&
                !ParseRegistryEntryArray(install["registry"],
                                         "layout.components[].install.registry",
                                         component.registry,
                                         lastError)) {
                return false;
            }
            if (!GetOptionalStringList(install, "killProcesses", component.killProcesses, lastError) ||
                !GetOptionalBool(install, "desktopShortcut", component.createDesktopShortcut, lastError) ||
                !GetOptionalBool(install, "autoStartup", component.autoStartup, lastError)) {
                return false;
            }
        }

        out.components.push_back(std::move(component));
    }

    return true;
}

bool ParseRegistryValueTypeValue(const std::string& raw,
                                 RegistryValueType& out,
                                 const std::string& fieldLabel,
                                 std::string& lastError) {
    const std::string normalized = ToLowerCopy(raw);
    if (normalized == "dword") {
        out = RegistryValueType::DWORD;
        return true;
    }
    if (normalized == "expand" || normalized == "expand_string") {
        out = RegistryValueType::EXPAND_STRING;
        return true;
    }
    if (normalized == "string" || normalized.empty()) {
        out = RegistryValueType::STRING;
        return true;
    }
    lastError = "Invalid field '" + fieldLabel + "': expected 'string', 'expand', or 'dword'";
    return false;
}

bool ParseInstallStateValues(const json& values,
                             const std::string& fieldLabel,
                             std::unordered_map<std::string, InstallStateValueConfig>& out,
                             bool registryValues,
                             std::string& lastError) {
    if (!values.is_object()) {
        lastError = "Invalid field '" + fieldLabel + "': expected object";
        return false;
    }
    out.clear();
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!it.value().is_object()) {
            lastError = "Invalid field '" + fieldLabel + "." + it.key() + "': expected object";
            return false;
        }
        InstallStateValueConfig entry;
        if (registryValues) {
            if (!GetRequiredString(it.value(), "key", entry.key, lastError)) {
                lastError = "Missing required field '" + fieldLabel + "." + it.key() + ".key'";
                return false;
            }
            entry.name = it.key();
        } else {
            entry.key = it.key();
            if (!GetOptionalString(it.value(), "name", entry.name, lastError)) {
                return false;
            }
            if (entry.name.empty()) {
                entry.name = it.key();
            }
        }
        if (it.value().contains("value")) {
            if (it.value()["value"].is_number_integer() || it.value()["value"].is_number_unsigned()) {
                entry.value = std::to_string(it.value()["value"].get<uint32_t>());
                entry.type = RegistryValueType::DWORD;
            } else if (!JsonValueToString(it.value()["value"], entry.value)) {
                lastError = "Invalid field '" + fieldLabel + "." + it.key() + ".value': expected string or integer";
                return false;
            }
        }
        if (it.value().contains("type")) {
            std::string type;
            if (!JsonValueToString(it.value()["type"], type) ||
                !ParseRegistryValueTypeValue(type, entry.type, fieldLabel + "." + it.key() + ".type", lastError)) {
                return false;
            }
        }
        out.emplace(it.key(), std::move(entry));
    }
    return true;
}

bool ParseV3App(const json& root, AppConfig& out, std::string& lastError) {
    json app;
    if (!GetRequiredObject(root, "app", app, lastError)) {
        return false;
    }
    if (!GetRequiredString(app, "id", out.id, lastError) ||
        !GetRequiredString(app, "name", out.name, lastError) ||
        !GetRequiredString(app, "version", out.version, lastError) ||
        !GetOptionalString(app, "publisher", out.publisher, lastError) ||
        !GetOptionalString(app, "website", out.website, lastError) ||
        !GetOptionalString(app, "icon", out.icon, lastError)) {
        return false;
    }
    out.product.iconPath = out.icon;
    json versionInfo;
    if (GetOptionalObject(app, "versionInfo", versionInfo)) {
        if (!GetOptionalString(versionInfo, "productName", out.versionInfo.productName, lastError) ||
            !GetOptionalString(versionInfo, "fileDescription", out.versionInfo.fileDescription, lastError) ||
            !GetOptionalString(versionInfo, "fileVersion", out.versionInfo.fileVersion, lastError) ||
            !GetOptionalString(versionInfo, "productVersion", out.versionInfo.productVersion, lastError) ||
            !GetOptionalString(versionInfo, "companyName", out.versionInfo.companyName, lastError) ||
            !GetOptionalString(versionInfo, "copyright", out.versionInfo.copyright, lastError)) {
            return false;
        }
    }
    out.product = out.versionInfo;
    if (out.product.companyName.empty()) {
        out.product.companyName = out.publisher;
    }
    if (out.product.productName.empty()) {
        out.product.productName = out.name;
    }
    if (out.product.fileVersion.empty()) {
        out.product.fileVersion = out.version;
    }
    if (out.product.productVersion.empty()) {
        out.product.productVersion = out.version;
    }
    out.product.iconPath = out.icon;
    return true;
}

bool ParseV3MinWindows(const json& installer, MinWindowsConfig& out, std::string& lastError) {
    if (!installer.contains("minWindows")) {
        return true;
    }
    if (installer["minWindows"].is_string()) {
        std::string text;
        JsonValueToString(installer["minWindows"], text);
        std::vector<uint32_t> parts;
        size_t start = 0;
        try {
            while (start <= text.size()) {
                size_t dot = text.find('.', start);
                std::string part = text.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
                if (part.empty()) {
                    lastError = "Invalid field 'installer.minWindows': expected version like 10.0.19041";
                    return false;
                }
                parts.push_back(static_cast<uint32_t>(std::stoul(part)));
                if (dot == std::string::npos) {
                    break;
                }
                start = dot + 1;
            }
        } catch (const std::exception&) {
            lastError = "Invalid field 'installer.minWindows': expected version like 10.0.19041";
            return false;
        }
        if (parts.size() < 2 || parts.size() > 3) {
            lastError = "Invalid field 'installer.minWindows': expected version like 10.0.19041";
            return false;
        }
        out.major = static_cast<uint16_t>(parts[0]);
        out.minor = static_cast<uint16_t>(parts[1]);
        out.build = parts.size() >= 3 ? parts[2] : 0;
        return true;
    }
    if (installer["minWindows"].is_object()) {
        uint64_t major = out.major;
        uint64_t minor = out.minor;
        uint64_t build = out.build;
        if (!GetOptionalUInt64(installer["minWindows"], "major", major, lastError) ||
            !GetOptionalUInt64(installer["minWindows"], "minor", minor, lastError) ||
            !GetOptionalUInt64(installer["minWindows"], "build", build, lastError)) {
            return false;
        }
        out.major = static_cast<uint16_t>(major);
        out.minor = static_cast<uint16_t>(minor);
        out.build = static_cast<uint32_t>(build);
        return true;
    }
    lastError = "Invalid field 'installer.minWindows': expected string or object";
    return false;
}

bool ParseV3InstallState(const json& installer, InstallStateConfig& out, std::string& lastError) {
    json state;
    if (!GetOptionalObject(installer, "installState", state)) {
        return true;
    }
    if (state.contains("registries")) {
        if (!state["registries"].is_array()) {
            lastError = "Invalid field 'installer.installState.registries': expected array";
            return false;
        }
        out.registries.clear();
        for (const auto& item : state["registries"]) {
            if (!item.is_object()) {
                lastError = "Invalid field 'installer.installState.registries[]': expected object";
                return false;
            }
            InstallStateRegistryStoreConfig store;
            if (!GetOptionalString(item, "id", store.id, lastError) ||
                !GetRequiredString(item, "path", store.path, lastError)) {
                return false;
            }
            json values;
            if (!GetRequiredObject(item, "values", values, lastError) ||
                !ParseInstallStateValues(values, "installer.installState.registries[].values", store.values, true, lastError)) {
                return false;
            }
            out.registries.push_back(std::move(store));
        }
    }
    if (state.contains("files")) {
        if (!state["files"].is_array()) {
            lastError = "Invalid field 'installer.installState.files': expected array";
            return false;
        }
        out.files.clear();
        for (const auto& item : state["files"]) {
            if (!item.is_object()) {
                lastError = "Invalid field 'installer.installState.files[]': expected object";
                return false;
            }
            InstallStateFileStoreConfig store;
            if (!GetOptionalString(item, "id", store.id, lastError) ||
                !GetRequiredString(item, "path", store.path, lastError) ||
                !GetOptionalString(item, "format", store.format, lastError)) {
                return false;
            }
            json values;
            if (!GetRequiredObject(item, "values", values, lastError) ||
                !ParseInstallStateValues(values, "installer.installState.files[].values", store.values, false, lastError)) {
                return false;
            }
            out.files.push_back(std::move(store));
        }
    }
    json detect;
    if (GetOptionalObject(state, "detect", detect)) {
        json primary;
        if (GetOptionalObject(detect, "primary", primary)) {
            if (!GetOptionalString(primary, "registry", out.detect.primary.registry, lastError) ||
                !GetOptionalString(primary, "value", out.detect.primary.value, lastError)) {
                return false;
            }
        }
        if (detect.contains("legacy")) {
            if (!detect["legacy"].is_array()) {
                lastError = "Invalid field 'installer.installState.detect.legacy': expected array";
                return false;
            }
            out.detect.legacy.clear();
            for (const auto& item : detect["legacy"]) {
                if (!item.is_object()) {
                    lastError = "Invalid field 'installer.installState.detect.legacy[]': expected object";
                    return false;
                }
                InstalledInstanceDetectInstallStateConfig legacy;
                if (!GetOptionalString(item, "id", legacy.id, lastError) ||
                    !GetOptionalString(item, "path", legacy.path, lastError) ||
                    !GetOptionalString(item, "installDirValue", legacy.installDirValue, lastError)) {
                    return false;
                }
                out.detect.legacy.push_back(std::move(legacy));
            }
        }
    }
    return true;
}

bool ParseV3InstallerUi(const json& installer, UiConfig& out, std::string& lastError) {
    json ui;
    if (!GetOptionalObject(installer, "ui", ui)) {
        return true;
    }
    if (!GetOptionalString(ui, "defaultLanguage", out.defaultLanguage, lastError)) {
        return false;
    }
    json shortcutName;
    if (GetOptionalObject(ui, "desktopShortcutName", shortcutName)) {
        if (!GetOptionalString(shortcutName, "default", out.desktopShortcut.defaultName, lastError)) {
            return false;
        }
        out.desktopShortcut.i18n.clear();
        for (auto it = shortcutName.begin(); it != shortcutName.end(); ++it) {
            if (it.key() == "default") {
                continue;
            }
            std::string value;
            if (!JsonValueToString(it.value(), value)) {
                lastError = "Invalid field 'installer.ui.desktopShortcutName." + it.key() + "': expected string";
                return false;
            }
            out.desktopShortcut.i18n[it.key()] = value;
        }
    }
    if (ui.contains("links")) {
        if (!ui["links"].is_array()) {
            lastError = "Invalid field 'installer.ui.links': expected array";
            return false;
        }
        out.links.clear();
        for (const auto& item : ui["links"]) {
            UiLinkBinding link;
            if (!item.is_object() ||
                !GetRequiredString(item, "control", link.control, lastError) ||
                !GetRequiredString(item, "url", link.url, lastError)) {
                lastError = "Invalid field 'installer.ui.links[]': expected object with control and url";
                return false;
            }
            out.links.push_back(std::move(link));
        }
    }
    json componentSelection;
    if (GetOptionalObject(ui, "componentSelection", componentSelection)) {
        if (!GetOptionalString(componentSelection, "mode", out.componentSelection.mode, lastError)) {
            return false;
        }
        json binding;
        if (GetOptionalObject(componentSelection, "binding", binding)) {
            if (!GetOptionalString(binding, "strategy", out.componentSelection.strategy, lastError) ||
                !GetOptionalString(binding, "tokenPrefix", out.componentSelection.tokenPrefix, lastError)) {
                return false;
            }
            if (binding.contains("pages")) {
                if (!binding["pages"].is_array()) {
                    lastError = "Invalid field 'installer.ui.componentSelection.binding.pages': expected array";
                    return false;
                }
                out.componentSelection.pages.clear();
                for (const auto& pageJson : binding["pages"]) {
                    UiComponentBindingPage page;
                    if (!pageJson.is_object() ||
                        !GetRequiredString(pageJson, "skin", page.skin, lastError) ||
                        !GetOptionalStringList(pageJson, "controls", page.controls, lastError)) {
                        return false;
                    }
                    out.componentSelection.pages.push_back(std::move(page));
                }
            }
        }
    }
    return true;
}

bool ParseV3Payload(const json& installer, std::vector<PayloadConfig>& out, std::string& lastError) {
    if (!installer.contains("payload") || !installer["payload"].is_array()) {
        lastError = "Missing required field 'installer.payload'";
        return false;
    }
    out.clear();
    for (const auto& item : installer["payload"]) {
        if (!item.is_object()) {
            lastError = "Invalid field 'installer.payload[]': expected object";
            return false;
        }
        PayloadConfig payload;
        if (!GetRequiredString(item, "id", payload.id, lastError) ||
            !GetRequiredString(item, "source", payload.source, lastError) ||
            !GetRequiredString(item, "target", payload.target, lastError) ||
            !GetOptionalBool(item, "required", payload.required, lastError)) {
            return false;
        }
        out.push_back(std::move(payload));
    }
    return true;
}

bool ParseV3Components(const json& installer, std::vector<ComponentConfig>& out, std::string& lastError) {
    out.clear();
    if (!installer.contains("components")) {
        return true;
    }
    if (!installer["components"].is_array()) {
        lastError = "Invalid field 'installer.components': expected array";
        return false;
    }
    for (const auto& item : installer["components"]) {
        if (!item.is_object()) {
            lastError = "Invalid field 'installer.components[]': expected object";
            return false;
        }
        ComponentConfig component;
        if (!GetRequiredString(item, "id", component.id, lastError) ||
            !GetRequiredString(item, "name", component.name, lastError) ||
            !GetOptionalString(item, "description", component.description, lastError) ||
            !GetOptionalBool(item, "required", component.required, lastError) ||
            !GetOptionalBool(item, "defaultSelected", component.defaultSelected, lastError) ||
            !GetOptionalStringList(item, "dependsOn", component.dependsOn, lastError) ||
            !GetOptionalStringList(item, "payload", component.folders, lastError)) {
            return false;
        }
        json install;
        if (GetOptionalObject(item, "install", install)) {
            std::string command;
            std::string workingDirectory;
            if (!GetOptionalString(install, "command", command, lastError) ||
                !GetOptionalString(install, "args", component.source.local.args, lastError) ||
                !GetOptionalString(install, "workingDirectory", workingDirectory, lastError) ||
                !GetOptionalBool(install, "wait", component.source.local.wait, lastError)) {
                return false;
            }
            uint64_t timeout = component.source.local.timeoutSec;
            if (!GetOptionalUInt64(install, "timeoutSec", timeout, lastError)) {
                return false;
            }
            if (!command.empty()) {
                component.source.type = ComponentSourceType::LOCAL;
                component.source.local.base = workingDirectory.empty() ? "%InstallDir%" : workingDirectory;
                component.source.local.installer = command;
                component.source.local.timeoutSec = static_cast<uint32_t>(timeout);
            }
        }
        json uninstall;
        if (GetOptionalObject(item, "uninstall", uninstall)) {
            std::string command;
            std::string args;
            if (!GetOptionalString(uninstall, "command", command, lastError) ||
                !GetOptionalString(uninstall, "args", args, lastError)) {
                return false;
            }
            component.source.local.uninstall = command;
            if (!args.empty()) {
                component.source.local.uninstall += " " + args;
            }
        }
        out.push_back(std::move(component));
    }
    return true;
}

bool ParseV3SystemUninstallEntry(const json& installer,
                                 SystemUninstallEntryConfig& out,
                                 std::string& lastError) {
    json entry;
    if (!GetRequiredObject(installer, "systemUninstallEntry", entry, lastError)) {
        return false;
    }
    if (entry.contains("enabled")) {
        lastError = "Unsupported field 'installer.systemUninstallEntry.enabled'";
        return false;
    }
    if (!GetRequiredString(entry, "displayName", out.displayName, lastError) ||
        !GetOptionalString(entry, "publisher", out.publisher, lastError)) {
        return false;
    }
    std::string scope;
    if (!GetRequiredString(entry, "scope", scope, lastError) ||
        !ParseUninstallEntryScopeValue(scope, out.scope, lastError, "installer.systemUninstallEntry.scope")) {
        return false;
    }
    return true;
}

bool ParseV3InstallerCleanup(const json& installer,
                             InstallerCleanupConfig& out,
                             std::string& lastError) {
    json cleanup;
    if (!GetOptionalObject(installer, "cleanup", cleanup)) {
        return true;
    }
    if (!ParseSystemUninstallEntryCleanupObject(cleanup,
                                                "installer.cleanup.systemUninstallEntry",
                                                out.systemUninstallEntry,
                                                lastError,
                                                false)) {
        return false;
    }
    json legacy;
    if (GetOptionalObject(cleanup, "legacy", legacy)) {
        if (!GetOptionalStringList(legacy, "desktopShortcutNames", out.legacy.desktopShortcutNames, lastError) ||
            !GetOptionalStringList(legacy, "startupNames", out.legacy.startupNames, lastError)) {
            return false;
        }
    }
    json registry;
    if (GetOptionalObject(cleanup, "registry", registry)) {
        if (!GetOptionalStringList(registry, "deleteKeys", out.registry.deleteKeys, lastError)) {
            return false;
        }
        if (registry.contains("deleteValues") &&
            !ParseRegistryEntryArray(registry["deleteValues"],
                                     "installer.cleanup.registry.deleteValues",
                                     out.registry.deleteValues,
                                     lastError)) {
            return false;
        }
    }
    if (cleanup.contains("paths") &&
        !ParseCleanupRuleArray(cleanup["paths"], "installer.cleanup.paths", out.paths, lastError)) {
        return false;
    }
    return true;
}

bool ParseV3RegistryWrite(const json& installer,
                          InstallerRegistryConfig& out,
                          std::string& lastError) {
    json registry;
    if (!GetOptionalObject(installer, "registry", registry) || !registry.contains("write")) {
        return true;
    }
    if (!registry["write"].is_array()) {
        lastError = "Invalid field 'installer.registry.write': expected array";
        return false;
    }
    out.write.clear();
    for (const auto& item : registry["write"]) {
        if (!item.is_object()) {
            lastError = "Invalid field 'installer.registry.write[]': expected object";
            return false;
        }
        InstallerRegistryWriteGroup group;
        if (!GetRequiredString(item, "path", group.path, lastError)) {
            return false;
        }
        json values;
        if (!GetRequiredObject(item, "values", values, lastError) ||
            !ParseInstallStateValues(values, "installer.registry.write[].values", group.values, false, lastError)) {
            return false;
        }
        out.write.push_back(std::move(group));
    }
    return true;
}

bool ParseV3Installer(const json& root, InstallerConfig& out, std::string& lastError) {
    json installer;
    if (!GetRequiredObject(root, "installer", installer, lastError)) {
        return false;
    }
    if (!GetRequiredString(installer, "defaultDir", out.defaultDir, lastError) ||
        !GetRequiredString(installer, "directoryName", out.directoryName, lastError) ||
        !GetOptionalBool(installer, "requireAdmin", out.requireAdmin, lastError) ||
        !GetOptionalUInt64(installer, "largeFileThresholdBytes", out.largeFileThresholdBytes, lastError) ||
        !GetOptionalString(installer, "mutex", out.mutex, lastError) ||
        !GetOptionalStringList(installer, "killBeforeInstall", out.killBeforeInstall, lastError) ||
        !ParseV3MinWindows(installer, out.minWindows, lastError) ||
        !ParseV3InstallState(installer, out.installState, lastError) ||
        !ParseV3SystemUninstallEntry(installer, out.systemUninstallEntry, lastError) ||
        !ParseV3InstallerCleanup(installer, out.cleanup, lastError) ||
        !ParseV3InstallerUi(installer, out.ui, lastError) ||
        !ParseV3Payload(installer, out.payload, lastError) ||
        !ParseV3Components(installer, out.components, lastError)) {
        return false;
    }
    json defaults;
    if (GetOptionalObject(installer, "defaults", defaults)) {
        if (!GetOptionalBool(defaults, "autoStartup", out.defaults.autoStartup, lastError) ||
            !GetOptionalBool(defaults, "desktopShortcut", out.defaults.desktopShortcut, lastError)) {
            return false;
        }
    }
    return true;
}

bool ParseV3Uninstaller(const json& root, UninstallerConfig& out, std::string& lastError) {
    json uninstaller;
    if (!GetRequiredObject(root, "uninstaller", uninstaller, lastError)) {
        return false;
    }
    if (!GetOptionalBool(uninstaller, "requireAdmin", out.requireAdmin, lastError) ||
        !GetOptionalStringList(uninstaller, "killBeforeUninstall", out.killBeforeUninstall, lastError)) {
        return false;
    }
    if (uninstaller.contains("detect")) {
        lastError = "Unsupported field 'uninstaller.detect'. Use 'installer.installState.detect' instead.";
        return false;
    }
    json cleanup;
    if (GetOptionalObject(uninstaller, "cleanup", cleanup)) {
        if (!GetOptionalString(cleanup, "installedFiles", out.cleanup.installedFiles, lastError) ||
            !GetOptionalString(cleanup, "missingManifestFallback", out.cleanup.missingManifestFallback, lastError) ||
            !GetOptionalString(cleanup, "installState", out.cleanup.installState, lastError) ||
            !GetOptionalString(cleanup, "autoStartup", out.cleanup.autoStartup, lastError) ||
            !GetOptionalString(cleanup, "desktopShortcut", out.cleanup.desktopShortcut, lastError) ||
            !ParseSystemUninstallEntryCleanupObject(cleanup,
                                                    "uninstaller.cleanup.systemUninstallEntry",
                                                    out.cleanup.systemUninstallEntry,
                                                    lastError,
                                                    true)) {
            return false;
        }
        json legacy;
        if (GetOptionalObject(cleanup, "legacy", legacy)) {
            GetOptionalStringList(legacy, "desktopShortcutNames", out.cleanup.legacy.desktopShortcutNames, lastError);
            GetOptionalStringList(legacy, "startupNames", out.cleanup.legacy.startupNames, lastError);
            if (legacy.contains("uninstallEntries")) {
                lastError = "Unsupported field 'uninstaller.cleanup.legacy.uninstallEntries'. Use 'uninstaller.cleanup.systemUninstallEntry.legacyEntries' instead.";
                return false;
            }
        }
        json registry;
        if (GetOptionalObject(cleanup, "registry", registry)) {
            if (!GetOptionalStringList(registry, "deleteKeys", out.cleanup.registry.deleteKeys, lastError)) {
                return false;
            }
            if (registry.contains("deleteValues") &&
                !ParseRegistryEntryArray(registry["deleteValues"],
                                         "uninstaller.cleanup.registry.deleteValues",
                                         out.cleanup.registry.deleteValues,
                                         lastError)) {
                return false;
            }
        }
        if (cleanup.contains("paths") &&
            !ParseCleanupRuleArray(cleanup["paths"], "uninstaller.cleanup.paths", out.cleanup.paths, lastError)) {
            return false;
        }
    }
    json ui;
    if (GetOptionalObject(uninstaller, "ui", ui)) {
        if (!GetOptionalString(ui, "defaultLanguage", out.ui.defaultLanguage, lastError) ||
            !GetOptionalString(ui, "title", out.ui.title, lastError) ||
            !GetOptionalString(ui, "confirmMessage", out.ui.confirmMessage, lastError)) {
            return false;
        }
    }
    return true;
}

void PopulateV3RuntimeFields(PackagerConfiguration& config) {
    config.app.directoryName = config.installer.directoryName;
    config.install.defaultDir = config.installer.defaultDir;
    config.install.requireAdmin = config.installer.requireAdmin;
    config.install.autoStartup = config.installer.defaults.autoStartup;
    config.install.desktopIcon = config.installer.defaults.desktopShortcut;
    config.install.minWindows = config.installer.minWindows;
    config.install.sparseFileThresholdBytes = config.installer.largeFileThresholdBytes;
    config.install.killProcesses = config.installer.killBeforeInstall;
    config.install.mutexName = config.installer.mutex;
    config.install.useMutex = !config.installer.mutex.empty();

    config.ui = config.installer.ui;
    config.layout.folders.clear();
    for (const auto& payload : config.installer.payload) {
        LayoutFolderConfig folder;
        folder.id = payload.id;
        folder.source = payload.source;
        folder.target = payload.target;
        config.layout.folders.push_back(std::move(folder));
    }
    config.layout.components = config.installer.components;
}

bool ParseLifecycleConfig(const json& root, LifecycleConfig& out, std::string& lastError) {
    json section;
    if (!GetRequiredObject(root, "lifecycle", section, lastError)) {
        return false;
    }

    json registry;
    if (GetOptionalObject(section, "registry", registry) && registry.contains("onInstall")) {
        if (!ParseRegistryEntryArray(registry["onInstall"],
                                     "lifecycle.registry.onInstall",
                                     out.registry.onInstall,
                                     lastError)) {
            return false;
        }
    }

    json cleanup;
    if (GetOptionalObject(section, "cleanup", cleanup)) {
        json onUpgrade;
        if (GetOptionalObject(cleanup, "onUpgrade", onUpgrade)) {
            if (onUpgrade.contains("installRoots") &&
                !ParseRegistryLookupArray(onUpgrade["installRoots"],
                                          "lifecycle.cleanup.onUpgrade.installRoots",
                                          out.cleanup.onUpgrade.installRoots,
                                          lastError)) {
                return false;
            }
            json registryJson;
            if (GetOptionalObject(onUpgrade, "registry", registryJson)) {
                if (registryJson.contains("legacyKeys") &&
                    !ParseRegistryEntryArray(registryJson["legacyKeys"],
                                             "lifecycle.cleanup.onUpgrade.registry.legacyKeys",
                                             out.cleanup.onUpgrade.registry.legacyKeys,
                                             lastError)) {
                    return false;
                }
            }

            json uninstallEntries;
            if (GetOptionalObject(onUpgrade, "uninstallEntries", uninstallEntries) &&
                uninstallEntries.contains("entries") &&
                !ParseUninstallEntryCleanupArray(uninstallEntries["entries"],
                                                 "lifecycle.cleanup.onUpgrade.uninstallEntries.entries",
                                                 out.cleanup.onUpgrade.uninstallEntries,
                                                 lastError)) {
                return false;
            }

            if (onUpgrade.contains("shortcuts") &&
                !ParseNamedCleanupArray(onUpgrade["shortcuts"],
                                        "lifecycle.cleanup.onUpgrade.shortcuts",
                                        out.cleanup.onUpgrade.shortcuts,
                                        lastError)) {
                return false;
            }

            if (onUpgrade.contains("startup") &&
                !ParseNamedCleanupArray(onUpgrade["startup"],
                                        "lifecycle.cleanup.onUpgrade.startup",
                                        out.cleanup.onUpgrade.startup,
                                        lastError)) {
                return false;
            }

            if (onUpgrade.contains("extraPaths") &&
                !ParseCleanupRuleArray(onUpgrade["extraPaths"],
                                       "lifecycle.cleanup.onUpgrade.extraPaths",
                                       out.cleanup.onUpgrade.extraPaths,
                                       lastError)) {
                return false;
            }
        }

        json onUninstall;
        if (GetOptionalObject(cleanup, "onUninstall", onUninstall)) {
            if (onUninstall.contains("processes") &&
                !ParseNamedCleanupArray(onUninstall["processes"],
                                        "lifecycle.cleanup.onUninstall.processes",
                                        out.cleanup.onUninstall.processes,
                                        lastError)) {
                return false;
            }

            json uninstallRegistry;
            if (GetOptionalObject(onUninstall, "registry", uninstallRegistry) &&
                uninstallRegistry.contains("legacyKeys") &&
                !ParseRegistryEntryArray(uninstallRegistry["legacyKeys"],
                                         "lifecycle.cleanup.onUninstall.registry.legacyKeys",
                                         out.cleanup.onUninstall.registry.legacyKeys,
                                         lastError)) {
                return false;
            }

            json uninstallEntries;
            if (GetOptionalObject(onUninstall, "uninstallEntries", uninstallEntries) &&
                uninstallEntries.contains("entries") &&
                !ParseUninstallEntryCleanupArray(uninstallEntries["entries"],
                                                 "lifecycle.cleanup.onUninstall.uninstallEntries.entries",
                                                 out.cleanup.onUninstall.uninstallEntries,
                                                 lastError)) {
                return false;
            }

            if (onUninstall.contains("shortcuts") &&
                !ParseNamedCleanupArray(onUninstall["shortcuts"],
                                        "lifecycle.cleanup.onUninstall.shortcuts",
                                        out.cleanup.onUninstall.shortcuts,
                                        lastError)) {
                return false;
            }

            if (onUninstall.contains("startup") &&
                !ParseNamedCleanupArray(onUninstall["startup"],
                                        "lifecycle.cleanup.onUninstall.startup",
                                        out.cleanup.onUninstall.startup,
                                        lastError)) {
                return false;
            }

            if (onUninstall.contains("paths") &&
                !ParseCleanupRuleArray(onUninstall["paths"],
                                       "lifecycle.cleanup.onUninstall.paths",
                                       out.cleanup.onUninstall.paths,
                                       lastError)) {
                return false;
            }
        }
    }

    return true;
}

bool ValidateLayoutReferences(const LayoutConfig& layout, std::string& lastError) {
    std::unordered_set<std::string> folderIds;
    for (const auto& folder : layout.folders) {
        if (!folderIds.insert(folder.id).second) {
            lastError = "Invalid field 'layout.folders[].id': duplicate folder id '" + folder.id + "'";
            return false;
        }
    }

    for (const auto& component : layout.components) {
        for (const auto& folderId : component.folders) {
            if (folderIds.find(folderId) == folderIds.end()) {
                lastError = "Invalid field 'layout.components[].folders': unknown folder id '" + folderId + "'";
                return false;
            }
        }
    }

    return true;
}

} // namespace

std::optional<PackagerConfiguration> ConfigurationLoader::loadConfiguration(const std::string& configDirectory) {
    lastError_.clear();
    loadedConfigPath_.clear();

    auto configPath = findConfigFile(configDirectory);
    if (!configPath.has_value()) {
        lastError_ = "Configuration file not found in config directory: " + configDirectory +
                     " (expected packager.yaml or packager.yml)";
        return std::nullopt;
    }
    return loadConfigurationFromPath(configPath.value());
}

std::optional<PackagerConfiguration> ConfigurationLoader::loadConfigurationFromPath(const std::string& configPath) {
    lastError_.clear();
    loadedConfigPath_.clear();

    fs::path path = PathFromUtf8(configPath);
    if (!fs::exists(path)) {
        lastError_ = "Configuration file not found: " + configPath;
        return std::nullopt;
    }

    const std::string ext = ToLowerCopy(Utf8FromPath(path.extension()));
    if (ext != ".yaml" && ext != ".yml") {
        lastError_ = "Unsupported configuration file format. Only .yaml and .yml are supported";
        return std::nullopt;
    }

    auto config = parseYamlConfig(configPath);
    if (config.has_value()) {
        loadedConfigPath_ = configPath;
    }
    return config;
}

std::optional<std::string> ConfigurationLoader::findConfigFile(const std::string& directory) {
    const fs::path base = PathFromUtf8(directory);
    const fs::path yaml = base / "packager.yaml";
    if (fs::exists(yaml) && fs::is_regular_file(yaml)) {
        return Utf8FromPath(yaml);
    }
    const fs::path yml = base / "packager.yml";
    if (fs::exists(yml) && fs::is_regular_file(yml)) {
        return Utf8FromPath(yml);
    }
    return std::nullopt;
}

std::optional<PackagerConfiguration> ConfigurationLoader::parseYamlConfig(const std::string& filePath) {
    try {
        const YAML::Node yamlRoot = YAML::LoadFile(filePath);
        const json configObject = YamlNodeToJson(yamlRoot);
        return parseConfigObject(configObject, filePath, "YAML");
    } catch (const std::exception& ex) {
        lastError_ = "Failed to parse YAML configuration '" + filePath + "': " + ex.what();
        return std::nullopt;
    }
}

std::optional<PackagerConfiguration> ConfigurationLoader::parseConfigObject(const json& configObject,
                                                                            const std::string& filePath,
                                                                            const std::string& formatLabel) {
    if (!configObject.is_object()) {
        lastError_ = formatLabel + " configuration root must be an object: " + filePath;
        return std::nullopt;
    }

    if (!configObject.contains("schemaVersion")) {
        lastError_ = "Missing required field 'schemaVersion'";
        return std::nullopt;
    }

    int schemaVersion = 0;
    if (!JsonValueToInt(configObject["schemaVersion"], schemaVersion) || schemaVersion != 3) {
        lastError_ = "Unsupported schemaVersion. Only schemaVersion 3 is supported.";
        return std::nullopt;
    }

    PackagerConfiguration config;
    config.schemaVersion = 3;

    if (!ParseV3App(configObject, config.app, lastError_) ||
        !ParsePackageConfig(configObject, config.package, lastError_) ||
        !ParseV3Installer(configObject, config.installer, lastError_) ||
        !ParseV3Uninstaller(configObject, config.uninstaller, lastError_)) {
        return std::nullopt;
    }

    json installer;
    GetRequiredObject(configObject, "installer", installer, lastError_);
    if (!ParseV3RegistryWrite(installer, config.installer.registry, lastError_)) {
        return std::nullopt;
    }

    PopulateV3RuntimeFields(config);

    if (!ValidateLayoutReferences(config.layout, lastError_)) {
        return std::nullopt;
    }

    return config;
}

} // namespace MultiThreadedInstaller
