#include "packager/configuration_loader.h"

#include "common/utf8_utils.h"
#include "packager/config_value_reader.h"

#include <filesystem>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;
using nlohmann::json;

namespace MultiThreadedInstaller {
namespace {

bool GetRequiredObject(const json& parent, const char* key, json& out, std::string& lastError) {
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

bool GetRequiredString(const json& parent, const char* key, std::string& out, std::string& lastError) {
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
    if (normalized == "none") {
        out = CompressionAlgorithm::NONE;
        return true;
    }
    lastError = "Invalid field 'package.compression.algorithm': expected 'xz', 'zstd' or 'none'";
    return false;
}

bool ParseAppConfig(const json& root, AppConfig& out, std::string& lastError) {
    json app;
    if (!GetRequiredObject(root, "app", app, lastError)) {
        return false;
    }
    if (!GetRequiredString(app, "productName", out.productName, lastError) ||
        !GetRequiredString(app, "appName", out.appName, lastError) ||
        !GetRequiredString(app, "appId", out.appId, lastError) ||
        !GetRequiredString(app, "publisher", out.publisher, lastError) ||
        !GetRequiredString(app, "version", out.version, lastError) ||
        !GetOptionalString(app, "defaultDir", out.defaultDir, lastError) ||
        !GetOptionalString(app, "icon", out.icon, lastError) ||
        !GetOptionalString(app, "copyright", out.copyright, lastError)) {
        return false;
    }
    return true;
}

bool ParsePackageConfig(const json& root, PackageConfig& out, std::string& lastError) {
    json section;
    json compression;
    if (!GetRequiredObject(root, "package", section, lastError) ||
        !GetRequiredObject(section, "compression", compression, lastError)) {
        return false;
    }
    std::string algorithm;
    if (!GetRequiredString(compression, "algorithm", algorithm, lastError) ||
        !ParseCompressionAlgorithmValue(algorithm, out.compression.algorithm, lastError) ||
        !GetOptionalInt(compression, "level", out.compression.level, lastError)) {
        return false;
    }
    // package.compression.blockSize（可选，单位 MiB）：XZ 多线程分块大小；0/缺省=自动(对齐解码并行度)。
    if (compression.contains("blockSize")) {
        if (!GetOptionalInt(compression, "blockSize", out.compression.blockSizeMiB, lastError)) {
            return false;
        }
        if (out.compression.blockSizeMiB < 0) {
            lastError = "Invalid field 'package.compression.blockSize': must be >= 0 (MiB; 0=auto)";
            return false;
        }
    }

    // package.layout（可选）：逐文件夹落点声明。
    if (section.contains("layout")) {
        if (!section["layout"].is_array()) {
            lastError = "Invalid field 'package.layout': expected a list";
            return false;
        }
        for (const auto& item : section["layout"]) {
            if (!item.is_object()) {
                lastError = "Invalid entry in 'package.layout': expected an object with id/target";
                return false;
            }
            LayoutFolderTarget entry;
            if (!GetRequiredString(item, "source", entry.source, lastError) ||
                !GetRequiredString(item, "target", entry.target, lastError)) {
                lastError = "Each 'package.layout' entry requires non-empty 'source' and 'target'";
                return false;
            }
            out.layout.push_back(std::move(entry));
        }
    }
    return true;
}

bool ParseOnFailureValue(const std::string& raw, HookOnFailure& out, std::string& lastError,
                         const std::string& field) {
    const std::string normalized = ToLowerCopy(raw);
    if (normalized == "abort") {
        out = HookOnFailure::ABORT;
        return true;
    }
    if (normalized == "continue") {
        out = HookOnFailure::CONTINUE;
        return true;
    }
    lastError = "Invalid field '" + field + "': expected 'abort' or 'continue'";
    return false;
}

// 解析单个 hook 对象（path/args/onFailure/timeoutSec）。path 支持 .bat/.cmd/.ps1。
bool ParseHookEntry(const json& hook, const std::string& label, HookConfig& out,
                    std::string& lastError) {
    if (!hook.is_object()) {
        lastError = "Invalid '" + label + "': expected a mapping with at least 'path'";
        return false;
    }
    if (!GetRequiredString(hook, "path", out.path, lastError)) {
        lastError = "Missing required field '" + label + ".path'";
        return false;
    }
    out.present = true;
    if (!GetOptionalString(hook, "args", out.args, lastError)) {
        return false;
    }
    if (hook.contains("onFailure")) {
        std::string onFailure;
        if (!JsonValueToString(hook["onFailure"], onFailure) ||
            !ParseOnFailureValue(onFailure, out.onFailure, lastError, label + ".onFailure")) {
            return false;
        }
    }
    if (hook.contains("timeoutSec")) {
        int timeout = 0;
        if (!JsonValueToInt(hook["timeoutSec"], timeout) || timeout <= 0) {
            lastError = "Invalid field '" + label + ".timeoutSec': expected positive integer";
            return false;
        }
        out.timeoutSec = static_cast<uint32_t>(timeout);
    }
    if (hook.contains("keep")) {
        bool keep = false;
        if (!JsonValueToBool(hook["keep"], keep)) {
            lastError = "Invalid field '" + label + ".keep': expected boolean";
            return false;
        }
        out.keep = keep;
    }
    if (!GetOptionalString(hook, "keepDir", out.keepDir, lastError)) {
        return false;
    }
    return true;
}

// 解析一个 hook 列表：hooks.<key> 可写成数组（多个脚本，按序执行），
// 也兼容单个对象（按单元素列表处理）。整段缺省即无脚本。
bool ParseHookList(const json& hooks, const char* key, std::vector<HookConfig>& out,
                   std::string& lastError) {
    if (!hooks.contains(key) || hooks[key].is_null()) {
        return true;  // 该 hook 点可选
    }
    const std::string label = std::string("hooks.") + key;
    const json& node = hooks[key];
    if (node.is_array()) {
        out.reserve(node.size());
        for (size_t i = 0; i < node.size(); ++i) {
            HookConfig entry;
            if (!ParseHookEntry(node[i], label + "[" + std::to_string(i) + "]", entry, lastError)) {
                return false;
            }
            out.push_back(std::move(entry));
        }
        return true;
    }
    // 兼容单对象写法。
    HookConfig entry;
    if (!ParseHookEntry(node, label, entry, lastError)) {
        return false;
    }
    out.push_back(std::move(entry));
    return true;
}

bool ParseHooksConfig(const json& root, HooksConfig& out, std::string& lastError) {
    json hooks;
    if (!GetOptionalObject(root, "hooks", hooks)) {
        return true;  // hooks 整块可选
    }
    return ParseHookList(hooks, "preInstall", out.preInstall, lastError) &&
           ParseHookList(hooks, "postInstall", out.postInstall, lastError);
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

    PackagerConfiguration config;
    if (!ParseAppConfig(configObject, config.app, lastError_) ||
        !ParsePackageConfig(configObject, config.package, lastError_) ||
        !ParseHooksConfig(configObject, config.hooks, lastError_)) {
        return std::nullopt;
    }
    return config;
}

} // namespace MultiThreadedInstaller
