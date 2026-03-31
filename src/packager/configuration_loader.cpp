#include "packager/configuration_loader.h"

#include "common/utf8_utils.h"
#include "packager/config_parse_collections.h"
#include "packager/config_value_reader.h"

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
    if (normalized == "file") {
        out = InstallStateMode::FILE;
        return true;
    }
    if (normalized == "both") {
        out = InstallStateMode::BOTH;
        return true;
    }
    lastError = "Invalid field 'install.installState.mode': expected 'registry', 'file', or 'both'";
    return false;
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

    json installState;
    if (GetOptionalObject(section, "installState", installState)) {
        if (installState.contains("mode")) {
            std::string mode;
            if (!JsonValueToString(installState["mode"], mode) ||
                !ParseInstallStateModeValue(mode, out.installState.mode, lastError)) {
                return false;
            }
        }
        if (!GetOptionalString(installState, "registryPath", out.installState.registryPath, lastError) ||
            !GetOptionalString(installState, "registryKey", out.installState.registryKey, lastError) ||
            !GetOptionalString(installState, "filePath", out.installState.filePath, lastError) ||
            !GetOptionalBool(installState, "useMutex", out.installState.useMutex, lastError) ||
            !GetOptionalString(installState, "mutexName", out.installState.mutexName, lastError)) {
            return false;
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
        json destination;
        if (!GetRequiredObject(folderJson, "destination", destination, lastError)) {
            lastError = "Missing required field 'layout.folders[].destination'";
            return false;
        }
        if (!GetRequiredString(destination, "type", folder.destination.type, lastError) ||
            !GetOptionalString(destination, "path", folder.destination.path, lastError) ||
            !GetOptionalBool(destination, "appendDirectoryName", folder.destination.appendDirectoryName, lastError)) {
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

bool ParseLifecycleConfig(const json& root, LifecycleConfig& out, std::string& lastError) {
    json section;
    if (!GetRequiredObject(root, "lifecycle", section, lastError)) {
        return false;
    }

    json compatibility;
    if (GetOptionalObject(section, "compatibility", compatibility)) {
        if (!GetOptionalStringList(compatibility, "legacyAppIds", out.compatibility.legacyAppIds, lastError) ||
            !GetOptionalStringList(compatibility,
                                   "legacyDesktopShortcutNames",
                                   out.compatibility.legacyDesktopShortcutNames,
                                   lastError)) {
            return false;
        }
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
            json registryJson;
            if (GetOptionalObject(onUpgrade, "registry", registryJson)) {
                if (!GetOptionalBool(registryJson,
                                     "deleteFromManifest",
                                     out.cleanup.onUpgrade.registry.deleteFromManifest,
                                     lastError)) {
                    return false;
                }
                if (registryJson.contains("legacyKeys") &&
                    !ParseRegistryEntryArray(registryJson["legacyKeys"],
                                             "lifecycle.cleanup.onUpgrade.registry.legacyKeys",
                                             out.cleanup.onUpgrade.registry.legacyKeys,
                                             lastError)) {
                    return false;
                }
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
        if (GetOptionalObject(cleanup, "onUninstall", onUninstall) && onUninstall.contains("paths")) {
            if (!ParseCleanupRuleArray(onUninstall["paths"],
                                       "lifecycle.cleanup.onUninstall.paths",
                                       out.cleanup.onUninstallPaths,
                                       lastError)) {
                return false;
            }
        }
    }

    json postSetup;
    if (GetOptionalObject(section, "postSetup", postSetup)) {
        json agent;
        if (GetOptionalObject(postSetup, "agent", agent)) {
            if (!GetOptionalBool(agent, "enabled", out.postSetup.agent.enabled, lastError) ||
                !GetOptionalStringList(agent, "tasks", out.postSetup.agent.tasks, lastError)) {
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

std::optional<PackagerConfiguration> ConfigurationLoader::loadConfiguration(const std::string& inputDirectory) {
    lastError_.clear();
    loadedConfigPath_.clear();

    const char* envPath = std::getenv("PACKAGER_CONFIG");
    if (envPath != nullptr && envPath[0] != '\0') {
        return loadConfigurationFromPath(envPath);
    }

    auto configPath = findConfigFile(inputDirectory);
    if (!configPath.has_value()) {
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
    if (!JsonValueToInt(configObject["schemaVersion"], schemaVersion) || schemaVersion != 2) {
        lastError_ = "Invalid field 'schemaVersion': expected integer value 2";
        return std::nullopt;
    }

    PackagerConfiguration config;
    config.schemaVersion = 2;

    if (!ParseAppConfig(configObject, config.app, lastError_) ||
        !ParsePackageConfig(configObject, config.package, lastError_) ||
        !ParseInstallConfig(configObject, config.install, lastError_) ||
        !ParseUiConfig(configObject, config.ui, lastError_) ||
        !ParseLayoutConfig(configObject, config.layout, lastError_) ||
        !ParseLifecycleConfig(configObject, config.lifecycle, lastError_) ||
        !ValidateLayoutReferences(config.layout, lastError_)) {
        return std::nullopt;
    }

    return config;
}

} // namespace MultiThreadedInstaller
