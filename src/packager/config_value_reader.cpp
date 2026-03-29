#include "packager/config_value_reader.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace MultiThreadedInstaller {

using json = nlohmann::json;

namespace {

void SetNormalizedFieldIfMissing(json& target,
                                 const std::string& normalizedKey,
                                 const json& source,
                                 const std::string& sourceKey) {
    if (!target.contains(normalizedKey) && source.contains(sourceKey)) {
        target[normalizedKey] = source[sourceKey];
    }
}

}  // namespace

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
        out = value.is_number_unsigned() ? value.get<uint64_t>() != 0 : value.get<int64_t>() != 0;
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

bool JsonValueToInt(const json& value, int& out) {
    if (value.is_number_integer()) {
        const int64_t signedValue = value.get<int64_t>();
        if (signedValue < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
            signedValue > static_cast<int64_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(signedValue);
        return true;
    }
    if (value.is_number_unsigned()) {
        const uint64_t unsignedValue = value.get<uint64_t>();
        if (unsignedValue > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        out = static_cast<int>(unsignedValue);
        return true;
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (text.empty()) {
            return false;
        }
        size_t pos = 0;
        try {
            const long long parsed = std::stoll(text, &pos, 10);
            if (pos != text.size()) {
                return false;
            }
            if (parsed < static_cast<long long>(std::numeric_limits<int>::min()) ||
                parsed > static_cast<long long>(std::numeric_limits<int>::max())) {
                return false;
            }
            out = static_cast<int>(parsed);
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

bool JsonObjectToStringMap(const json& objectValue,
                           std::unordered_map<std::string, std::string>& outMap) {
    if (!objectValue.is_object()) {
        return false;
    }
    outMap.clear();
    for (auto it = objectValue.begin(); it != objectValue.end(); ++it) {
        std::string value;
        if (!JsonValueToString(it.value(), value)) {
            return false;
        }
        outMap[it.key()] = std::move(value);
    }
    return true;
}

bool IsStructuredConfigSchema(const json& root) {
    return root.is_object() &&
           (root.contains("package") || root.contains("install") || root.contains("folders"));
}

json NormalizeStructuredConfigSchema(const json& root) {
    json normalized = root;

    if (normalized.contains("package") && normalized["package"].is_object()) {
        const json& package = normalized["package"];
        SetNormalizedFieldIfMissing(normalized, "Version", package, "version");
        SetNormalizedFieldIfMissing(normalized, "AppName", package, "appName");
        SetNormalizedFieldIfMissing(normalized, "AppId", package, "appId");
        SetNormalizedFieldIfMissing(normalized, "DirectoryName", package, "directoryName");
        SetNormalizedFieldIfMissing(normalized, "LegacyAppIds", package, "legacyAppIds");
        SetNormalizedFieldIfMissing(normalized, "Icon", package, "icon");
        SetNormalizedFieldIfMissing(normalized, "WebPageUrl", package, "webPageUrl");
        SetNormalizedFieldIfMissing(normalized, "ProductName", package, "productName");
        SetNormalizedFieldIfMissing(normalized, "FileVersion", package, "fileVersion");
        SetNormalizedFieldIfMissing(normalized, "ProductVersion", package, "productVersion");
        SetNormalizedFieldIfMissing(normalized, "CompanyName", package, "companyName");
        SetNormalizedFieldIfMissing(normalized, "FileDescription", package, "fileDescription");
        SetNormalizedFieldIfMissing(normalized, "Copyright", package, "copyright");
        SetNormalizedFieldIfMissing(normalized, "compressionAlgorithm", package, "compressionAlgorithm");
        SetNormalizedFieldIfMissing(normalized, "compressionLevel", package, "compressionLevel");
    }

    if (normalized.contains("install") && normalized["install"].is_object()) {
        const json& install = normalized["install"];
        SetNormalizedFieldIfMissing(normalized, "InstallDir", install, "defaultInstallDir");
        SetNormalizedFieldIfMissing(normalized, "DesktopShortcutName", install, "desktopShortcutName");
        SetNormalizedFieldIfMissing(normalized, "DesktopShortcutNameI18n", install, "desktopShortcutNameI18n");
        SetNormalizedFieldIfMissing(normalized, "LegacyDesktopShortcutNames", install, "legacyDesktopShortcutNames");
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

    if (!normalized.contains("Registry") && normalized.contains("registry")) {
        normalized["Registry"] = normalized["registry"];
    }

    if (!normalized.contains("Cleanup") &&
        normalized.contains("cleanup") && normalized["cleanup"].is_object()) {
        const json& cleanup = normalized["cleanup"];
        normalized["Cleanup"] = json::object();
        if (cleanup.contains("onUninstall")) {
            normalized["Cleanup"]["OnUninstall"] = cleanup["onUninstall"];
        }
        if (cleanup.contains("onUpgrade") && cleanup["onUpgrade"].is_object()) {
            const json& onUpgrade = cleanup["onUpgrade"];
            json normalizedUpgrade = json::object();
            if (onUpgrade.contains("extraPaths")) {
                normalizedUpgrade["ExtraPaths"] = onUpgrade["extraPaths"];
            }
            if (onUpgrade.contains("registry") && onUpgrade["registry"].is_object()) {
                const json& registry = onUpgrade["registry"];
                json normalizedRegistry = json::object();
                if (registry.contains("deleteFromManifest")) {
                    normalizedRegistry["DeleteFromManifest"] = registry["deleteFromManifest"];
                }
                if (registry.contains("legacyKeys")) {
                    normalizedRegistry["LegacyKeys"] = registry["legacyKeys"];
                }
                normalizedUpgrade["Registry"] = std::move(normalizedRegistry);
            }
            normalized["Cleanup"]["OnUpgrade"] = std::move(normalizedUpgrade);
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

}  // namespace MultiThreadedInstaller
