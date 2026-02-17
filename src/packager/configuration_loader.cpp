#include "packager/configuration_loader.h"

#include "common/utf8_utils.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace MultiThreadedInstaller {

namespace {

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool JsonValueToString(const json& value, std::string& out) {
    if (value.is_string()) {
        out = value.get<std::string>();
        return true;
    }
    if (value.is_number_integer()) {
        out = std::to_string(value.get<int64_t>());
        return true;
    }
    if (value.is_number_unsigned()) {
        out = std::to_string(value.get<uint64_t>());
        return true;
    }
    if (value.is_number_float()) {
        std::ostringstream oss;
        oss << value.get<double>();
        out = oss.str();
        return true;
    }
    if (value.is_boolean()) {
        out = value.get<bool>() ? "true" : "false";
        return true;
    }
    return false;
}

bool JsonValueToBool(const json& value, bool& out) {
    if (value.is_boolean()) {
        out = value.get<bool>();
        return true;
    }
    if (value.is_string()) {
        std::string lowered = ToLowerCopy(value.get<std::string>());
        if (lowered == "true" || lowered == "1" || lowered == "yes") {
            out = true;
            return true;
        }
        if (lowered == "false" || lowered == "0" || lowered == "no") {
            out = false;
            return true;
        }
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        if (value.is_number_unsigned()) {
            out = value.get<uint64_t>() != 0;
        } else {
            out = value.get<int64_t>() != 0;
        }
        return true;
    }
    return false;
}

bool JsonValueToUInt64(const json& value, uint64_t& out) {
    if (value.is_number_unsigned()) {
        out = value.get<uint64_t>();
        return true;
    }
    if (value.is_number_integer()) {
        const int64_t signedValue = value.get<int64_t>();
        if (signedValue < 0) {
            return false;
        }
        out = static_cast<uint64_t>(signedValue);
        return true;
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (text.empty()) {
            return false;
        }
        size_t pos = 0;
        try {
            const unsigned long long parsed = std::stoull(text, &pos, 10);
            if (pos != text.size()) {
                return false;
            }
            out = static_cast<uint64_t>(parsed);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool JsonArrayToStringList(const json& arrayValue, std::vector<std::string>& outList) {
    if (!arrayValue.is_array()) {
        return false;
    }
    for (const auto& item : arrayValue) {
        std::string text;
        if (!JsonValueToString(item, text)) {
            return false;
        }
        outList.push_back(std::move(text));
    }
    return true;
}

bool IsStructuredConfigSchema(const json& root) {
    return root.is_object() &&
           (root.contains("package") || root.contains("install") || root.contains("folders"));
}

void SetNormalizedFieldIfMissing(json& target,
                                 const std::string& normalizedKey,
                                 const json& source,
                                 const std::string& sourceKey) {
    if (!target.contains(normalizedKey) && source.contains(sourceKey)) {
        target[normalizedKey] = source[sourceKey];
    }
}

json NormalizeStructuredConfigSchema(const json& root) {
    if (!root.is_object() || !IsStructuredConfigSchema(root)) {
        return root;
    }

    json normalized = root;

    if (root.contains("package") && root["package"].is_object()) {
        const json& package = root["package"];
        SetNormalizedFieldIfMissing(normalized, "Version", package, "version");
        SetNormalizedFieldIfMissing(normalized, "AppName", package, "appName");
        SetNormalizedFieldIfMissing(normalized, "Icon", package, "icon");
        SetNormalizedFieldIfMissing(normalized, "WebPageUrl", package, "webPageUrl");
        SetNormalizedFieldIfMissing(normalized, "ProductName", package, "productName");
        SetNormalizedFieldIfMissing(normalized, "FileVersion", package, "fileVersion");
        SetNormalizedFieldIfMissing(normalized, "ProductVersion", package, "productVersion");
        SetNormalizedFieldIfMissing(normalized, "CompanyName", package, "companyName");
        SetNormalizedFieldIfMissing(normalized, "FileDescription", package, "fileDescription");
        SetNormalizedFieldIfMissing(normalized, "Copyright", package, "copyright");
    }

    if (root.contains("install") && root["install"].is_object()) {
        const json& install = root["install"];
        SetNormalizedFieldIfMissing(normalized, "InstallDir", install, "defaultInstallDir");
        SetNormalizedFieldIfMissing(normalized, "AutoStartup", install, "autoStartup");
        SetNormalizedFieldIfMissing(normalized, "DesktopIcons", install, "desktopIcons");
        SetNormalizedFieldIfMissing(normalized, "AutoCleanOldInstall", install, "autoCleanOldInstall");
        SetNormalizedFieldIfMissing(normalized, "RequireAdmin", install, "requireAdmin");
        SetNormalizedFieldIfMissing(normalized, "MinWindowsVersion", install, "minWindowsVersion");
        SetNormalizedFieldIfMissing(normalized, "SparseFileThresholdBytes", install, "sparseFileThresholdBytes");
        SetNormalizedFieldIfMissing(normalized, "KillProcesses", install, "killProcesses");

        if (!normalized.contains("InstallState") &&
            install.contains("installState") && install["installState"].is_object()) {
            const json& state = install["installState"];
            json normalizedState = json::object();
            if (state.contains("mode")) {
                normalizedState["Mode"] = state["mode"];
            }
            if (state.contains("registryPath")) {
                normalizedState["RegistryPath"] = state["registryPath"];
            }
            if (state.contains("registryKey")) {
                normalizedState["RegistryKey"] = state["registryKey"];
            }
            if (state.contains("filePath")) {
                normalizedState["FilePath"] = state["filePath"];
            }
            if (state.contains("useMutex")) {
                normalizedState["UseMutex"] = state["useMutex"];
            }
            if (state.contains("mutexName")) {
                normalizedState["MutexName"] = state["mutexName"];
            }
            normalized["InstallState"] = std::move(normalizedState);
        }
    }

    if (!normalized.contains("Registry") && root.contains("registry")) {
        normalized["Registry"] = root["registry"];
    }

    if (root.contains("folders") && root["folders"].is_array()) {
        json folderMap = json::object();
        if (normalized.contains("Folder") && normalized["Folder"].is_object()) {
            folderMap = normalized["Folder"];
        }

        for (const auto& item : root["folders"]) {
            if (!item.is_object()) {
                continue;
            }
            std::string folderName;
            std::string target;
            if (!item.contains("name") || !JsonValueToString(item["name"], folderName)) {
                continue;
            }
            if (!item.contains("target") || !JsonValueToString(item["target"], target)) {
                continue;
            }

            const std::string loweredTarget = ToLowerCopy(target);
            if (loweredTarget == "installdirectory" || loweredTarget == "%installdir%") {
                folderMap["InstallDir"] = folderName;
            } else if (loweredTarget.find("%appdata%") != std::string::npos) {
                folderMap["Roaming"] = folderName;
            } else if (loweredTarget.find("%localappdata%") != std::string::npos) {
                folderMap["Local"] = folderName;
            }
        }

        if (!folderMap.empty()) {
            normalized["Folder"] = std::move(folderMap);
        }
    }

    return normalized;
}

json ParseYamlScalar(const std::string& scalar) {
    const std::string lowered = ToLowerCopy(scalar);
    if (lowered == "null" || lowered == "~") {
        return nullptr;
    }
    if (lowered == "true") {
        return true;
    }
    if (lowered == "false") {
        return false;
    }

    const bool hasLeadingSign = !scalar.empty() && (scalar[0] == '+' || scalar[0] == '-');
    const size_t start = hasLeadingSign ? 1 : 0;
    const bool allDigits = start < scalar.size() &&
                           std::all_of(scalar.begin() + static_cast<std::ptrdiff_t>(start),
                                       scalar.end(),
                                       [](unsigned char ch) { return std::isdigit(ch) != 0; });
    if (allDigits) {
        try {
            if (hasLeadingSign && scalar[0] == '-') {
                return std::stoll(scalar);
            }
            return std::stoull(scalar);
        } catch (...) {
            return scalar;
        }
    }

    return scalar;
}

json YamlNodeToJson(const YAML::Node& node) {
    switch (node.Type()) {
        case YAML::NodeType::Null:
        case YAML::NodeType::Undefined:
            return nullptr;
        case YAML::NodeType::Scalar:
            return ParseYamlScalar(node.Scalar());
        case YAML::NodeType::Sequence: {
            json arr = json::array();
            for (const auto& child : node) {
                arr.push_back(YamlNodeToJson(child));
            }
            return arr;
        }
        case YAML::NodeType::Map: {
            json obj = json::object();
            for (const auto& kv : node) {
                std::string key;
                if (kv.first.IsScalar()) {
                    key = kv.first.Scalar();
                } else {
                    key = YAML::Dump(kv.first);
                }
                obj[key] = YamlNodeToJson(kv.second);
            }
            return obj;
        }
        default:
            return nullptr;
    }
}

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
        return parseJsonConfig(configPath);
    }

    auto jsonConfig = parseJsonConfig(configPath);
    if (jsonConfig.has_value()) {
        return jsonConfig;
    }
    std::string jsonError = lastError_;

    auto yamlConfig = parseYamlConfig(configPath);
    if (yamlConfig.has_value()) {
        return yamlConfig;
    }
    std::string yamlError = lastError_;

    lastError_ = "Unable to parse configuration file '" + configPath +
                 "' as JSON or YAML.\n  JSON error: " + jsonError +
                 "\n  YAML error: " + yamlError;
    return std::nullopt;
}

std::optional<std::string> ConfigurationLoader::findConfigFile(
    const std::string& directory) {

    const std::vector<std::string> configNames = {
        "packager.yaml",
        "packager.yml",
        "packager.json",
        ".packager.json"
    };

    for (const auto& name : configNames) {
        fs::path configPath = PathFromUtf8(directory) / name;
        if (fs::exists(configPath)) {
            return Utf8FromPath(configPath);
        }
    }

    return std::nullopt;
}

std::optional<PackagerConfiguration> ConfigurationLoader::parseJsonConfig(
    const std::string& filePath) {
    try {
        std::ifstream file(PathFromUtf8(filePath));
        if (!file.is_open()) {
            lastError_ = "Failed to open configuration file: " + filePath;
            return std::nullopt;
        }

        json parsed;
        try {
            file >> parsed;
        } catch (const json::parse_error& e) {
            lastError_ = "JSON parse error in " + filePath + ": " + e.what();
            return std::nullopt;
        }

        return parseConfigObject(NormalizeStructuredConfigSchema(parsed), filePath, "JSON");
    } catch (const std::exception& e) {
        lastError_ = "Error parsing JSON configuration file: " + std::string(e.what());
        return std::nullopt;
    }
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

    PackagerConfiguration config;
    if (!getRequiredString("Version", config.version) ||
        !getRequiredString("AppName", config.applicationName) ||
        !getRequiredString("InstallDir", config.defaultInstallDir)) {
        return std::nullopt;
    }

    if (!getOptionalString("Icon", config.iconPath) ||
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
        if (algo == "lzma") {
            config.compressionAlgorithm = CompressionAlgorithm::LZMA_HIGH;
        } else {
            lastError_ = "Invalid compressionAlgorithm: '" + algo +
                         "' (expected: lzma)";
            return std::nullopt;
        }
    }

    if (configObject.contains("Folder")) {
        if (!configObject["Folder"].is_object()) {
            lastError_ = "Invalid field 'Folder': expected object";
            return std::nullopt;
        }
        const auto& folderObj = configObject["Folder"];

        if (folderObj.contains("InstallDir")) {
            std::string folderName;
            if (!JsonValueToString(folderObj["InstallDir"], folderName)) {
                lastError_ = "Invalid field 'Folder.InstallDir': expected string";
                return std::nullopt;
            }
            FolderTargetConfig ftc;
            ftc.folderName = folderName;
            ftc.targetDirectory = "installDirectory";
            ftc.dirType = SpecialDirectoryType::INSTALL_DIRECTORY;
            config.folderTargets.push_back(std::move(ftc));
        }

        if (folderObj.contains("Roaming")) {
            std::string folderName;
            if (!JsonValueToString(folderObj["Roaming"], folderName)) {
                lastError_ = "Invalid field 'Folder.Roaming': expected string";
                return std::nullopt;
            }
            FolderTargetConfig ftc;
            ftc.folderName = folderName;
            ftc.targetDirectory = "%AppData%\\Roaming";
            ftc.dirType = SpecialDirectoryType::APPDATA_ROAMING;
            config.folderTargets.push_back(std::move(ftc));
        }

        if (folderObj.contains("Local")) {
            std::string folderName;
            if (!JsonValueToString(folderObj["Local"], folderName)) {
                lastError_ = "Invalid field 'Folder.Local': expected string";
                return std::nullopt;
            }
            FolderTargetConfig ftc;
            ftc.folderName = folderName;
            ftc.targetDirectory = "%LocalAppData%";
            ftc.dirType = SpecialDirectoryType::APPDATA_LOCAL;
            config.folderTargets.push_back(std::move(ftc));
        }
    }

    if (configObject.contains("Registry")) {
        if (!configObject["Registry"].is_array()) {
            lastError_ = "Invalid field 'Registry': expected array";
            return std::nullopt;
        }
        for (const auto& entry : configObject["Registry"]) {
            if (!entry.is_object()) {
                continue;
            }

            RegistryEntry reg;
            if (entry.contains("path") && !JsonValueToString(entry["path"], reg.path)) {
                lastError_ = "Invalid field 'Registry[].path': expected string";
                return std::nullopt;
            }
            if (entry.contains("key") && !JsonValueToString(entry["key"], reg.key)) {
                lastError_ = "Invalid field 'Registry[].key': expected string";
                return std::nullopt;
            }
            if (entry.contains("value")) {
                if (entry["value"].is_number_integer() || entry["value"].is_number_unsigned()) {
                    reg.type = RegistryValueType::DWORD;
                    reg.value = std::to_string(entry["value"].get<uint32_t>());
                } else if (!JsonValueToString(entry["value"], reg.value)) {
                    lastError_ = "Invalid field 'Registry[].value': expected string or integer";
                    return std::nullopt;
                }
            }
            if (entry.contains("type")) {
                std::string type;
                if (!JsonValueToString(entry["type"], type)) {
                    lastError_ = "Invalid field 'Registry[].type': expected string";
                    return std::nullopt;
                }
                type = ToLowerCopy(type);
                if (type == "dword") {
                    reg.type = RegistryValueType::DWORD;
                } else if (type == "expand" || type == "expand_string") {
                    reg.type = RegistryValueType::EXPAND_STRING;
                } else {
                    reg.type = RegistryValueType::STRING;
                }
            }
            config.registry.push_back(std::move(reg));
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

    config.installState.registryPath = "HKEY_CURRENT_USER\\Software\\" + config.applicationName;
    config.installState.filePath = "%ProgramData%\\" + config.applicationName + "\\install.state";
    config.installState.mutexName = "Global\\" + config.applicationName + "_Install";

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
                if (!item["registry"].is_array()) {
                    lastError_ = "Invalid field 'components[].registry': expected array";
                    return std::nullopt;
                }
                for (const auto& regItem : item["registry"]) {
                    if (!regItem.is_object()) {
                        lastError_ = "Invalid field 'components[].registry[]': expected object";
                        return std::nullopt;
                    }
                    RegistryEntry reg;
                    if (regItem.contains("path") && !JsonValueToString(regItem["path"], reg.path)) {
                        lastError_ = "Invalid field 'components[].registry[].path': expected string";
                        return std::nullopt;
                    }
                    if (regItem.contains("key") && !JsonValueToString(regItem["key"], reg.key)) {
                        lastError_ = "Invalid field 'components[].registry[].key': expected string";
                        return std::nullopt;
                    }
                    if (regItem.contains("value")) {
                        if (regItem["value"].is_number_integer() || regItem["value"].is_number_unsigned()) {
                            reg.type = RegistryValueType::DWORD;
                            reg.value = std::to_string(regItem["value"].get<uint32_t>());
                        } else if (!JsonValueToString(regItem["value"], reg.value)) {
                            lastError_ = "Invalid field 'components[].registry[].value': expected string or integer";
                            return std::nullopt;
                        }
                    }
                    if (regItem.contains("type")) {
                        std::string regType;
                        if (!JsonValueToString(regItem["type"], regType)) {
                            lastError_ = "Invalid field 'components[].registry[].type': expected string";
                            return std::nullopt;
                        }
                        regType = ToLowerCopy(regType);
                        if (regType == "dword") {
                            reg.type = RegistryValueType::DWORD;
                        } else if (regType == "expand" || regType == "expand_string") {
                            reg.type = RegistryValueType::EXPAND_STRING;
                        } else {
                            reg.type = RegistryValueType::STRING;
                        }
                    }
                    component.registry.push_back(std::move(reg));
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
    }

    return config;
}

SpecialDirectoryType ConfigurationLoader::parseDirectoryType(
    const std::string& dirStr) {

    if (dirStr == "installDirectory") {
        return SpecialDirectoryType::INSTALL_DIRECTORY;
    }

    if (dirStr.find("%ProgramFiles%") != std::string::npos) {
        return SpecialDirectoryType::PROGRAM_FILES;
    }

    if (dirStr.find("%AppData%") != std::string::npos) {
        return SpecialDirectoryType::APPDATA_ROAMING;
    }

    if (dirStr.find("%LocalAppData%") != std::string::npos) {
        return SpecialDirectoryType::APPDATA_LOCAL;
    }

    if (dirStr.find("%ProgramData%") != std::string::npos) {
        return SpecialDirectoryType::PROGRAM_DATA;
    }

    return SpecialDirectoryType::INSTALL_DIRECTORY;
}

} // namespace MultiThreadedInstaller
