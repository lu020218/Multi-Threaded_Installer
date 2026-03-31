#include "packager/configuration_loader.h"
#include "packager/config_parse_collections.h"
#include "packager/config_value_reader.h"

#include "common/utf8_utils.h"

#include <json.hpp>
#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace MultiThreadedInstaller {

namespace {

} // namespace

std::optional<PackagerConfiguration> ConfigurationLoader::loadConfiguration(
    const std::string& inputDirectory) {

    lastError_.clear();
    loadedConfigPath_.clear();

#ifdef _WIN32
    const wchar_t* envConfigW = _wgetenv(L"PACKAGER_CONFIG");
    if (envConfigW && envConfigW[0] != L'\0') {
        std::string envPath = WideToUtf8(envConfigW);
        if (fs::exists(PathFromUtf8(envPath))) {
            return loadConfigurationFromPath(envPath);
        }
        lastError_ = "Configuration file specified in PACKAGER_CONFIG does not exist: " + envPath;
        return std::nullopt;
    }
#else
    const char* envConfig = std::getenv("PACKAGER_CONFIG");
    if (envConfig != nullptr && std::strlen(envConfig) > 0) {
        std::string envPath(envConfig);
        if (fs::exists(PathFromUtf8(envPath))) {
            return loadConfigurationFromPath(envPath);
        }
        lastError_ = "Configuration file specified in PACKAGER_CONFIG does not exist: " + envPath;
        return std::nullopt;
    }
#endif

    auto configPath = findConfigFile(inputDirectory);
    if (!configPath) {
        return std::nullopt;
    }

    return loadConfigurationFromPath(*configPath);
}

std::optional<PackagerConfiguration> ConfigurationLoader::loadConfigurationFromPath(
    const std::string& configPath) {

    lastError_.clear();
    loadedConfigPath_ = configPath;

    const std::string extension =
        ToLowerCopy(fs::path(PathFromUtf8(configPath)).extension().string());

    if (extension == ".yaml" || extension == ".yml") {
        return parseYamlConfig(configPath);
    }
    if (extension == ".json") {
        lastError_ = "JSON configuration is no longer supported. Use packager.yaml or packager.yml: " +
                     configPath;
        return std::nullopt;
    }
    lastError_ = "Unsupported configuration file extension for '" + configPath +
                 "'. Use packager.yaml or packager.yml.";
    return std::nullopt;
}

std::optional<std::string> ConfigurationLoader::findConfigFile(
    const std::string& directory) {

    const std::vector<std::string> configNames = {
        "packager.yaml",
        "packager.yml"
    };

    for (const auto& name : configNames) {
        fs::path configPath = PathFromUtf8(directory) / name;
        if (fs::exists(configPath)) {
            return Utf8FromPath(configPath);
        }
    }

    return std::nullopt;
}

std::optional<PackagerConfiguration> ConfigurationLoader::parseYamlConfig(
    const std::string& filePath) {
    try {
        std::ifstream file(PathFromUtf8(filePath));
        if (!file.is_open()) {
            lastError_ = "Failed to open configuration file: " + filePath;
            return std::nullopt;
        }

        YAML::Node yamlRoot;
        try {
            yamlRoot = YAML::Load(file);
        } catch (const YAML::ParserException& e) {
            lastError_ = "YAML parse error in " + filePath + ": " + e.what();
            return std::nullopt;
        }

        if (!yamlRoot || !yamlRoot.IsMap()) {
            lastError_ = "Invalid YAML config in " + filePath +
                         ": root node must be a mapping/object";
            return std::nullopt;
        }

        const json parsed = YamlNodeToJson(yamlRoot);
        if (!IsStructuredConfigSchema(parsed)) {
            lastError_ = "Unsupported YAML config schema in " + filePath +
                         ": only the structured package/install/folders schema is supported";
            return std::nullopt;
        }
        return parseConfigObject(NormalizeStructuredConfigSchema(parsed), filePath, "YAML");
    } catch (const std::exception& e) {
        lastError_ = "Error parsing YAML configuration file: " + std::string(e.what());
        return std::nullopt;
    }
}

std::optional<PackagerConfiguration> ConfigurationLoader::parseConfigObject(
    const nlohmann::json& configObject,
    const std::string& filePath,
    const std::string& formatLabel) {
    if (!configObject.is_object()) {
        lastError_ = formatLabel + " configuration root must be an object: " + filePath;
        return std::nullopt;
    }

    auto getRequiredString = [&](const char* key, std::string& out) -> bool {
        if (!configObject.contains(key)) {
            lastError_ = "Missing required field '" + std::string(key) +
                         "' in configuration file: " + filePath;
            return false;
        }
        if (!JsonValueToString(configObject[key], out)) {
            lastError_ = "Invalid field '" + std::string(key) + "' in " + formatLabel +
                         " config: expected string-compatible value";
            return false;
        }
        return true;
    };

    auto getOptionalString = [&](const char* key, std::string& out) -> bool {
        if (!configObject.contains(key)) {
            return true;
        }
        if (!JsonValueToString(configObject[key], out)) {
            lastError_ = "Invalid field '" + std::string(key) + "' in " + formatLabel +
                         " config: expected string-compatible value";
            return false;
        }
        return true;
    };

    auto getOptionalStringList = [&](const char* key, std::vector<std::string>& out) -> bool {
        if (!configObject.contains(key)) {
            return true;
        }
        out.clear();
        if (!JsonArrayToStringList(configObject[key], out)) {
            lastError_ = "Invalid field '" + std::string(key) + "' in " + formatLabel +
                         " config: expected string array";
            return false;
        }
        return true;
    };

    auto getOptionalBool = [&](const char* key, bool& out) -> bool {
        if (!configObject.contains(key)) {
            return true;
        }
        if (!JsonValueToBool(configObject[key], out)) {
            lastError_ = "Invalid field '" + std::string(key) + "' in " + formatLabel +
                         " config: expected boolean";
            return false;
        }
        return true;
    };

    auto getOptionalUInt64 = [&](const char* key, uint64_t& out) -> bool {
        if (!configObject.contains(key)) {
            return true;
        }
        if (!JsonValueToUInt64(configObject[key], out)) {
            lastError_ = "Invalid field '" + std::string(key) + "' in " + formatLabel +
                         " config: expected non-negative integer";
            return false;
        }
        return true;
    };

    auto getOptionalInt = [&](const char* key, int& out) -> bool {
        if (!configObject.contains(key)) {
            return true;
        }
        if (!JsonValueToInt(configObject[key], out)) {
            lastError_ = "Invalid field '" + std::string(key) + "' in " + formatLabel +
                         " config: expected integer";
            return false;
        }
        return true;
    };

    auto getOptionalStringMap =
        [&](const char* key, std::unordered_map<std::string, std::string>& out) -> bool {
        if (!configObject.contains(key)) {
            return true;
        }
        if (!JsonObjectToStringMap(configObject[key], out)) {
            lastError_ = "Invalid field '" + std::string(key) + "' in " + formatLabel +
                         " config: expected object<string,string>";
            return false;
        }
        return true;
    };

    PackagerConfiguration config;
    if (!getRequiredString("Version", config.version) ||
        !getRequiredString("AppName", config.applicationName) ||
        !getRequiredString("InstallDir", config.defaultInstallDir)) {
        return std::nullopt;
    }

    if (!getOptionalString("AppId", config.appId) ||
        !getOptionalString("DirectoryName", config.directoryName) ||
        !getOptionalStringList("LegacyAppIds", config.legacyAppIds) ||
        !getOptionalString("DesktopShortcutName", config.desktopShortcutName) ||
        !getOptionalStringMap("DesktopShortcutNameI18n", config.desktopShortcutNameI18n) ||
        !getOptionalStringList("LegacyDesktopShortcutNames", config.legacyDesktopShortcutNames) ||
        !getOptionalString("Icon", config.iconPath) ||
        !getOptionalString("WebPageUrl", config.webPageUrl) ||
        !getOptionalString("ProductName", config.productName) ||
        !getOptionalString("FileVersion", config.fileVersion) ||
        !getOptionalString("ProductVersion", config.productVersion) ||
        !getOptionalString("CompanyName", config.companyName) ||
        !getOptionalString("FileDescription", config.fileDescription) ||
        !getOptionalString("Copyright", config.copyright)) {
        return std::nullopt;
    }

    if (configObject.contains("compressionAlgorithm")) {
        std::string algo;
        if (!JsonValueToString(configObject["compressionAlgorithm"], algo)) {
            lastError_ = "Invalid field 'compressionAlgorithm' in " + formatLabel +
                         " config: expected string";
            return std::nullopt;
        }
        algo = ToLowerCopy(algo);
        if (algo == "xz" || algo == "lzma2" || algo == "xz_lzma2" || algo == "lzma2_xz") {
            config.compressionAlgorithm = CompressionAlgorithm::LZMA2_XZ;
        } else if (algo == "zstd") {
            config.compressionAlgorithm = CompressionAlgorithm::ZSTD;
        } else {
            lastError_ = "Invalid compressionAlgorithm: '" + algo +
                         "' (expected: xz/lzma2 or zstd)";
            return std::nullopt;
        }
    }

    if (!getOptionalInt("compressionLevel", config.compressionLevel)) {
        return std::nullopt;
    }

    auto upsertFolderTarget = [&](FolderTargetConfig folderTarget) {
        auto it = std::find_if(config.folderTargets.begin(),
                               config.folderTargets.end(),
                               [&](const FolderTargetConfig& current) {
                                   return current.folderName == folderTarget.folderName;
                               });
        if (it != config.folderTargets.end()) {
            *it = std::move(folderTarget);
        } else {
            config.folderTargets.push_back(std::move(folderTarget));
        }
    };

    if (configObject.contains("folders")) {
        const auto& folders = configObject["folders"];
        if (!folders.is_array()) {
            lastError_ = "Invalid field 'folders': expected array";
            return std::nullopt;
        }
        for (const auto& item : folders) {
            if (!item.is_object()) {
                lastError_ = "Invalid field 'folders[]': expected object";
                return std::nullopt;
            }

            FolderTargetConfig ftc;
            if (!item.contains("name") || !JsonValueToString(item["name"], ftc.folderName) ||
                ftc.folderName.empty()) {
                lastError_ = "Invalid field 'folders[].name': expected non-empty string";
                return std::nullopt;
            }

            std::string target;
            if (!item.contains("target") || !JsonValueToString(item["target"], target) ||
                target.empty()) {
                lastError_ = "Invalid field 'folders[].target': expected non-empty string";
                return std::nullopt;
            }

            if (item.contains("appendDirectoryName") &&
                !JsonValueToBool(item["appendDirectoryName"], ftc.appendDirectoryName)) {
                lastError_ = "Invalid field 'folders[].appendDirectoryName': expected boolean";
                return std::nullopt;
            }

            ftc.targetDirectory = target;
            const std::string loweredTarget = ToLowerCopy(target);
            if (loweredTarget == "installdirectory" || loweredTarget == "%installdir%") {
                ftc.targetDirectory = "installDirectory";
                ftc.dirType = SpecialDirectoryType::INSTALL_DIRECTORY;
            } else if (loweredTarget.find("%localappdata%") != std::string::npos) {
                ftc.dirType = SpecialDirectoryType::APPDATA_LOCAL;
            } else if (loweredTarget.find("%appdata%") != std::string::npos) {
                ftc.dirType = SpecialDirectoryType::APPDATA_ROAMING;
            } else if (loweredTarget.find("%programfiles(x86)%") != std::string::npos) {
                ftc.dirType = SpecialDirectoryType::PROGRAM_FILES_X86;
            } else if (loweredTarget.find("%programfiles%") != std::string::npos) {
                ftc.dirType = SpecialDirectoryType::PROGRAM_FILES;
            } else if (loweredTarget.find("%programdata%") != std::string::npos) {
                ftc.dirType = SpecialDirectoryType::PROGRAM_DATA;
            } else if (loweredTarget.find("%userprofile%") != std::string::npos) {
                ftc.dirType = SpecialDirectoryType::USER_PROFILE;
            } else {
                ftc.dirType = SpecialDirectoryType::INSTALL_DIRECTORY;
            }

            upsertFolderTarget(std::move(ftc));
        }
    }

    if (configObject.contains("Registry")) {
        if (!ParseRegistryEntryArray(configObject["Registry"], "Registry", config.registry, lastError_)) {
            return std::nullopt;
        }
    }

    if (configObject.contains("KillProcesses")) {
        const auto& killObj = configObject["KillProcesses"];
        if (killObj.is_array()) {
            if (!JsonArrayToStringList(killObj, config.installKillProcesses)) {
                lastError_ = "Invalid field 'KillProcesses': expected array of strings";
                return std::nullopt;
            }
        } else {
            std::string processName;
            if (!JsonValueToString(killObj, processName)) {
                lastError_ = "Invalid field 'KillProcesses': expected string or string array";
                return std::nullopt;
            }
            config.installKillProcesses.push_back(std::move(processName));
        }
    }

    const std::string effectiveIdentity = config.appId.empty() ? config.applicationName : config.appId;
    config.installState.registryPath = "HKEY_CURRENT_USER\\Software\\" + effectiveIdentity;
    config.installState.filePath = "%ProgramData%\\" + effectiveIdentity + "\\install.state";
    config.installState.mutexName = "Global\\" + effectiveIdentity + "_Install";

    if (configObject.contains("InstallState")) {
        if (!configObject["InstallState"].is_object()) {
            lastError_ = "Invalid field 'InstallState': expected object";
            return std::nullopt;
        }
        const auto& state = configObject["InstallState"];

        if (state.contains("Mode")) {
            std::string mode;
            if (!JsonValueToString(state["Mode"], mode)) {
                lastError_ = "Invalid field 'InstallState.Mode': expected string";
                return std::nullopt;
            }
            mode = ToLowerCopy(mode);
            if (mode == "registry") {
                config.installState.mode = InstallStateMode::REGISTRY;
            } else if (mode == "file") {
                config.installState.mode = InstallStateMode::FILE;
            } else if (mode == "both") {
                config.installState.mode = InstallStateMode::BOTH;
            }
        }

        if (state.contains("RegistryPath") &&
            !JsonValueToString(state["RegistryPath"], config.installState.registryPath)) {
            lastError_ = "Invalid field 'InstallState.RegistryPath': expected string";
            return std::nullopt;
        }
        if (state.contains("RegistryKey") &&
            !JsonValueToString(state["RegistryKey"], config.installState.registryKey)) {
            lastError_ = "Invalid field 'InstallState.RegistryKey': expected string";
            return std::nullopt;
        }
        if (state.contains("FilePath") &&
            !JsonValueToString(state["FilePath"], config.installState.filePath)) {
            lastError_ = "Invalid field 'InstallState.FilePath': expected string";
            return std::nullopt;
        }
        if (state.contains("UseMutex") &&
            !JsonValueToBool(state["UseMutex"], config.installState.useMutex)) {
            lastError_ = "Invalid field 'InstallState.UseMutex': expected boolean";
            return std::nullopt;
        }
        if (state.contains("MutexName") &&
            !JsonValueToString(state["MutexName"], config.installState.mutexName)) {
            lastError_ = "Invalid field 'InstallState.MutexName': expected string";
            return std::nullopt;
        }
    }

    if (!getOptionalBool("AutoCleanOldInstall", config.autoCleanOldInstall) ||
        !getOptionalBool("AutoStartup", config.autoStartup) ||
        !getOptionalBool("DesktopIcons", config.desktopIcons) ||
        !getOptionalBool("RequireAdmin", config.requireAdmin)) {
        return std::nullopt;
    }

    if (configObject.contains("MinWindowsVersion")) {
        std::string versionText;
        if (!JsonValueToString(configObject["MinWindowsVersion"], versionText)) {
            lastError_ = "Invalid field 'MinWindowsVersion': expected string";
            return std::nullopt;
        }

        std::vector<int> parts;
        size_t start = 0;
        while (start < versionText.size()) {
            size_t end = versionText.find('.', start);
            if (end == std::string::npos) {
                end = versionText.size();
            }
            const std::string token = versionText.substr(start, end - start);
            if (token.empty()) {
                lastError_ = "Invalid MinWindowsVersion: empty segment";
                return std::nullopt;
            }
            try {
                parts.push_back(std::stoi(token));
            } catch (...) {
                lastError_ = "Invalid MinWindowsVersion: must be numeric (e.g. 10.0.19041)";
                return std::nullopt;
            }
            start = end + 1;
        }

        if (parts.empty() || parts.size() > 3) {
            lastError_ = "Invalid MinWindowsVersion: expected format 'major.minor.build'";
            return std::nullopt;
        }
        config.minWindowsMajor = static_cast<uint16_t>(parts[0]);
        config.minWindowsMinor = static_cast<uint16_t>(parts.size() > 1 ? parts[1] : 0);
        config.minWindowsBuild = static_cast<uint32_t>(parts.size() > 2 ? parts[2] : 0);
    }

    if (!getOptionalUInt64("SparseFileThresholdBytes", config.sparseFileThresholdBytes)) {
        return std::nullopt;
    }

    if (configObject.contains("components")) {
        const auto& components = configObject["components"];
        if (!components.is_array()) {
            lastError_ = "Invalid field 'components' in " + formatLabel +
                         " config: expected array";
            return std::nullopt;
        }

        for (const auto& item : components) {
            if (!item.is_object()) {
                lastError_ = "Invalid field 'components[]': expected object";
                return std::nullopt;
            }

            ComponentConfig component;

            if (item.contains("id") && !JsonValueToString(item["id"], component.id)) {
                lastError_ = "Invalid field 'components[].id': expected string";
                return std::nullopt;
            }
            if (item.contains("name") && !JsonValueToString(item["name"], component.name)) {
                lastError_ = "Invalid field 'components[].name': expected string";
                return std::nullopt;
            }
            if (item.contains("description") &&
                !JsonValueToString(item["description"], component.description)) {
                lastError_ = "Invalid field 'components[].description': expected string";
                return std::nullopt;
            }
            if (item.contains("required") &&
                !JsonValueToBool(item["required"], component.required)) {
                lastError_ = "Invalid field 'components[].required': expected boolean";
                return std::nullopt;
            }
            if (item.contains("defaultSelected") &&
                !JsonValueToBool(item["defaultSelected"], component.defaultSelected)) {
                lastError_ = "Invalid field 'components[].defaultSelected': expected boolean";
                return std::nullopt;
            }
            if (item.contains("sizeHintMB")) {
                uint64_t sizeHint = 0;
                if (!JsonValueToUInt64(item["sizeHintMB"], sizeHint)) {
                    lastError_ = "Invalid field 'components[].sizeHintMB': expected non-negative integer";
                    return std::nullopt;
                }
                component.sizeHintMB = static_cast<uint32_t>(sizeHint);
            }
            if (item.contains("dependsOn")) {
                if (!JsonArrayToStringList(item["dependsOn"], component.dependsOn)) {
                    lastError_ = "Invalid field 'components[].dependsOn': expected string array";
                    return std::nullopt;
                }
            }
            if (item.contains("folders")) {
                if (!JsonArrayToStringList(item["folders"], component.folders)) {
                    lastError_ = "Invalid field 'components[].folders': expected string array";
                    return std::nullopt;
                }
            }

            if (item.contains("source")) {
                const auto& source = item["source"];
                if (!source.is_object()) {
                    lastError_ = "Invalid field 'components[].source': expected object";
                    return std::nullopt;
                }

                if (source.contains("type")) {
                    std::string sourceType;
                    if (!JsonValueToString(source["type"], sourceType)) {
                        lastError_ = "Invalid field 'components[].source.type': expected string";
                        return std::nullopt;
                    }
                    sourceType = ToLowerCopy(sourceType);
                    if (sourceType == "embedded") {
                        component.source.type = ComponentSourceType::EMBEDDED;
                    } else if (sourceType == "local") {
                        component.source.type = ComponentSourceType::LOCAL;
                    } else if (sourceType == "download") {
                        component.source.type = ComponentSourceType::DOWNLOAD;
                    } else {
                        lastError_ = "Invalid field 'components[].source.type': unsupported value '" +
                                     sourceType + "'";
                        return std::nullopt;
                    }
                }

                if (source.contains("local")) {
                    const auto& local = source["local"];
                    if (!local.is_object()) {
                        lastError_ = "Invalid field 'components[].source.local': expected object";
                        return std::nullopt;
                    }
                    if (local.contains("base") &&
                        !JsonValueToString(local["base"], component.source.local.base)) {
                        lastError_ = "Invalid field 'components[].source.local.base': expected string";
                        return std::nullopt;
                    }
                    if (local.contains("installer") &&
                        !JsonValueToString(local["installer"], component.source.local.installer)) {
                        lastError_ = "Invalid field 'components[].source.local.installer': expected string";
                        return std::nullopt;
                    }
                    if (local.contains("args") &&
                        !JsonValueToString(local["args"], component.source.local.args)) {
                        lastError_ = "Invalid field 'components[].source.local.args': expected string";
                        return std::nullopt;
                    }
                    if (local.contains("wait") &&
                        !JsonValueToBool(local["wait"], component.source.local.wait)) {
                        lastError_ = "Invalid field 'components[].source.local.wait': expected boolean";
                        return std::nullopt;
                    }
                    if (local.contains("timeoutSec")) {
                        uint64_t timeout = 0;
                        if (!JsonValueToUInt64(local["timeoutSec"], timeout)) {
                            lastError_ = "Invalid field 'components[].source.local.timeoutSec': expected integer";
                            return std::nullopt;
                        }
                        component.source.local.timeoutSec = static_cast<uint32_t>(timeout);
                    }
                    if (local.contains("uninstall") &&
                        !JsonValueToString(local["uninstall"], component.source.local.uninstall)) {
                        lastError_ = "Invalid field 'components[].source.local.uninstall': expected string";
                        return std::nullopt;
                    }
                }

                if (source.contains("download")) {
                    const auto& download = source["download"];
                    if (!download.is_object()) {
                        lastError_ = "Invalid field 'components[].source.download': expected object";
                        return std::nullopt;
                    }
                    if (download.contains("url") &&
                        !JsonValueToString(download["url"], component.source.download.url)) {
                        lastError_ = "Invalid field 'components[].source.download.url': expected string";
                        return std::nullopt;
                    }
                    if (download.contains("sha256") &&
                        !JsonValueToString(download["sha256"], component.source.download.sha256)) {
                        lastError_ = "Invalid field 'components[].source.download.sha256': expected string";
                        return std::nullopt;
                    }
                    if (download.contains("saveAs") &&
                        !JsonValueToString(download["saveAs"], component.source.download.saveAs)) {
                        lastError_ = "Invalid field 'components[].source.download.saveAs': expected string";
                        return std::nullopt;
                    }
                    if (download.contains("args") &&
                        !JsonValueToString(download["args"], component.source.download.args)) {
                        lastError_ = "Invalid field 'components[].source.download.args': expected string";
                        return std::nullopt;
                    }
                    if (download.contains("wait") &&
                        !JsonValueToBool(download["wait"], component.source.download.wait)) {
                        lastError_ = "Invalid field 'components[].source.download.wait': expected boolean";
                        return std::nullopt;
                    }
                    if (download.contains("timeoutSec")) {
                        uint64_t timeout = 0;
                        if (!JsonValueToUInt64(download["timeoutSec"], timeout)) {
                            lastError_ = "Invalid field 'components[].source.download.timeoutSec': expected integer";
                            return std::nullopt;
                        }
                        component.source.download.timeoutSec = static_cast<uint32_t>(timeout);
                    }
                    if (download.contains("uninstall") &&
                        !JsonValueToString(download["uninstall"], component.source.download.uninstall)) {
                        lastError_ = "Invalid field 'components[].source.download.uninstall': expected string";
                        return std::nullopt;
                    }
                }
            }

            if (item.contains("registry")) {
                if (!ParseRegistryEntryArray(item["registry"],
                                             "components[].registry",
                                             component.registry,
                                             lastError_)) {
                    return std::nullopt;
                }
            }

            if (item.contains("killProcesses")) {
                if (item["killProcesses"].is_array()) {
                    if (!JsonArrayToStringList(item["killProcesses"], component.killProcesses)) {
                        lastError_ = "Invalid field 'components[].killProcesses': expected string array";
                        return std::nullopt;
                    }
                } else {
                    std::string processName;
                    if (!JsonValueToString(item["killProcesses"], processName)) {
                        lastError_ = "Invalid field 'components[].killProcesses': expected string or string array";
                        return std::nullopt;
                    }
                    component.killProcesses.push_back(std::move(processName));
                }
            }

            if (item.contains("createDesktopShortcut") &&
                !JsonValueToBool(item["createDesktopShortcut"], component.createDesktopShortcut)) {
                lastError_ = "Invalid field 'components[].createDesktopShortcut': expected boolean";
                return std::nullopt;
            }
            if (item.contains("autoStartup") &&
                !JsonValueToBool(item["autoStartup"], component.autoStartup)) {
                lastError_ = "Invalid field 'components[].autoStartup': expected boolean";
                return std::nullopt;
            }

            config.components.push_back(std::move(component));
        }
    }

    if (configObject.contains("ui")) {
        const auto& ui = configObject["ui"];
        if (!ui.is_object()) {
            lastError_ = "Invalid field 'ui': expected object";
            return std::nullopt;
        }

        if (ui.contains("componentSelection")) {
            const auto& selection = ui["componentSelection"];
            if (!selection.is_object()) {
                lastError_ = "Invalid field 'ui.componentSelection': expected object";
                return std::nullopt;
            }

            if (selection.contains("mode") &&
                !JsonValueToString(selection["mode"], config.componentUi.mode)) {
                lastError_ = "Invalid field 'ui.componentSelection.mode': expected string";
                return std::nullopt;
            }

            if (selection.contains("binding")) {
                const auto& binding = selection["binding"];
                if (!binding.is_object()) {
                    lastError_ = "Invalid field 'ui.componentSelection.binding': expected object";
                    return std::nullopt;
                }
                if (binding.contains("strategy") &&
                    !JsonValueToString(binding["strategy"], config.componentUi.strategy)) {
                    lastError_ = "Invalid field 'ui.componentSelection.binding.strategy': expected string";
                    return std::nullopt;
                }
                if (binding.contains("tokenPrefix") &&
                    !JsonValueToString(binding["tokenPrefix"], config.componentUi.tokenPrefix)) {
                    lastError_ = "Invalid field 'ui.componentSelection.binding.tokenPrefix': expected string";
                    return std::nullopt;
                }
                if (binding.contains("pages")) {
                    if (!binding["pages"].is_array()) {
                        lastError_ = "Invalid field 'ui.componentSelection.binding.pages': expected array";
                        return std::nullopt;
                    }
                    config.componentUi.pages.clear();
                    for (const auto& pageItem : binding["pages"]) {
                        if (!pageItem.is_object()) {
                            lastError_ = "Invalid field 'ui.componentSelection.binding.pages[]': expected object";
                            return std::nullopt;
                        }
                        UiComponentBindingPage page;
                        if (pageItem.contains("skin") &&
                            !JsonValueToString(pageItem["skin"], page.skin)) {
                            lastError_ = "Invalid field 'ui.componentSelection.binding.pages[].skin': expected string";
                            return std::nullopt;
                        }
                        if (pageItem.contains("controls")) {
                            if (!JsonArrayToStringList(pageItem["controls"], page.controls)) {
                                lastError_ = "Invalid field 'ui.componentSelection.binding.pages[].controls': expected string array";
                                return std::nullopt;
                            }
                        }
                        config.componentUi.pages.push_back(std::move(page));
                    }
                }
            }
        }

        if (ui.contains("links")) {
            const auto& links = ui["links"];
            if (!links.is_array()) {
                lastError_ = "Invalid field 'ui.links': expected array";
                return std::nullopt;
            }
            config.uiLinks.clear();
            for (const auto& item : links) {
                if (!item.is_object()) {
                    lastError_ = "Invalid field 'ui.links[]': expected object";
                    return std::nullopt;
                }
                UiLinkBinding link;
                if (!item.contains("control") ||
                    !JsonValueToString(item["control"], link.control) ||
                    link.control.empty()) {
                    lastError_ = "Invalid field 'ui.links[].control': expected non-empty string";
                    return std::nullopt;
                }
                if (!item.contains("url") ||
                    !JsonValueToString(item["url"], link.url) ||
                    link.url.empty()) {
                    lastError_ = "Invalid field 'ui.links[].url': expected non-empty string";
                    return std::nullopt;
                }
                config.uiLinks.push_back(std::move(link));
            }
        }
    }

    if (configObject.contains("Cleanup")) {
        if (!configObject["Cleanup"].is_object()) {
            lastError_ = "Invalid field 'Cleanup': expected object";
            return std::nullopt;
        }
        const auto& cleanup = configObject["Cleanup"];
        if (cleanup.contains("OnUninstall")) {
            if (!ParseCleanupRuleArray(cleanup["OnUninstall"],
                                       "Cleanup.OnUninstall",
                                       config.uninstallCleanupRules,
                                       lastError_)) {
                return std::nullopt;
            }
        }
        if (cleanup.contains("OnUpgrade")) {
            const auto& onUpgrade = cleanup["OnUpgrade"];
            if (!onUpgrade.is_object()) {
                lastError_ = "Invalid field 'Cleanup.OnUpgrade': expected object";
                return std::nullopt;
            }
            if (onUpgrade.contains("Registry")) {
                const auto& registry = onUpgrade["Registry"];
                if (!registry.is_object()) {
                    lastError_ = "Invalid field 'Cleanup.OnUpgrade.Registry': expected object";
                    return std::nullopt;
                }
                if (registry.contains("DeleteFromManifest") &&
                    !JsonValueToBool(registry["DeleteFromManifest"],
                                     config.upgradeCleanup.registry.deleteFromManifest)) {
                    lastError_ =
                        "Invalid field 'Cleanup.OnUpgrade.Registry.DeleteFromManifest': expected boolean";
                    return std::nullopt;
                }
                if (registry.contains("LegacyKeys") &&
                    !ParseRegistryEntryArray(registry["LegacyKeys"],
                                             "Cleanup.OnUpgrade.Registry.LegacyKeys",
                                             config.upgradeCleanup.registry.legacyKeys,
                                             lastError_)) {
                    return std::nullopt;
                }
            }
            if (onUpgrade.contains("ExtraPaths") &&
                !ParseCleanupRuleArray(onUpgrade["ExtraPaths"],
                                       "Cleanup.OnUpgrade.ExtraPaths",
                                       config.upgradeCleanup.extraPaths,
                                       lastError_)) {
                return std::nullopt;
            }
        }
    }

    return config;
}

} // namespace MultiThreadedInstaller
