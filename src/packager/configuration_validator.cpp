#include "packager/configuration_validator.h"

#include "common/utf8_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace MultiThreadedInstaller {
namespace {

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool HasIllegalNameChar(const std::string& name) {
    const std::string illegal = "<>:\"/\\|?*";
    for (char c : name) {
        if (illegal.find(c) != std::string::npos ||
            std::iscntrl(static_cast<unsigned char>(c))) {
            return true;
        }
    }
    return false;
}

void ValidateHook(const HookScript& hook,
                  const std::string& label,
                  const std::string& configDirectory,
                  ConfigurationValidator::ValidationResult& result) {
    if (!hook.present) {
        return;
    }
    if (hook.sourcePath.empty()) {
        result.errors.push_back("ERROR: " + label + ".path is required");
        result.isValid = false;
        return;
    }
    fs::path scriptPath = PathFromUtf8(hook.sourcePath);
    if (!scriptPath.is_absolute()) {
        scriptPath = PathFromUtf8(configDirectory) / scriptPath;
    }
    if (!fs::exists(scriptPath) || !fs::is_regular_file(scriptPath)) {
        result.errors.push_back("ERROR: " + label + " script not found: " + Utf8FromPath(scriptPath));
        result.isValid = false;
    }
    // keep=true 时必须指定保留目录，否则不知道往哪拷贝。
    if (hook.keep && hook.keepDir.empty()) {
        result.errors.push_back("ERROR: " + label + ".keepDir is required when keep is true");
        result.isValid = false;
    }
}

} // namespace

ConfigurationValidator::ValidationResult ConfigurationValidator::validate(
    const PackagerConfiguration& config,
    const std::string& inputDirectory,
    const std::string& configDirectory) {
    ValidationResult result;

    // app
    if (config.app.productName.empty()) {
        result.errors.push_back("ERROR: Missing required field 'app.productName'");
        result.isValid = false;
    } else if (HasIllegalNameChar(config.app.productName)) {
        result.errors.push_back("ERROR: Invalid app.productName: contains illegal character");
        result.isValid = false;
    }
    // appName = 主 exe 程序名（不含 .exe）：不允许路径分隔符等非法文件名字符与 .exe 后缀。
    if (config.app.appName.empty()) {
        result.errors.push_back("ERROR: Missing required field 'app.appName'");
        result.isValid = false;
    } else if (HasIllegalNameChar(config.app.appName)) {
        result.errors.push_back("ERROR: Invalid app.appName: contains illegal character");
        result.isValid = false;
    } else {
        const std::string lowered = ToLowerCopy(config.app.appName);
        if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".exe") == 0) {
            result.errors.push_back("ERROR: Invalid app.appName: must not end with '.exe' (program name only)");
            result.isValid = false;
        }
    }
    if (config.app.appId.empty()) {
        result.errors.push_back("ERROR: Missing required field 'app.appId'");
        result.isValid = false;
    }
    if (config.app.publisher.empty()) {
        result.errors.push_back("ERROR: Missing required field 'app.publisher'");
        result.isValid = false;
    }
    if (config.app.version.empty()) {
        result.errors.push_back("ERROR: Missing required field 'app.version'");
        result.isValid = false;
    }

    // package.compression.level
    const int level = config.package.compression.level;
    if (level != -1) {
        bool valid = true;
        if (config.package.compression.algorithm == CompressionAlgorithm::LZMA2_XZ) {
            valid = level >= 0 && level <= 9;
        } else if (config.package.compression.algorithm == CompressionAlgorithm::ZSTD) {
            valid = level >= 1 && level <= 22;
        }
        if (!valid) {
            result.errors.push_back("ERROR: Invalid package.compression.level for selected algorithm");
            result.isValid = false;
        }
    }

    // icon
    if (!config.appIcon.empty()) {
        fs::path iconPath = PathFromUtf8(config.appIcon);
        if (!iconPath.is_absolute()) {
            iconPath = PathFromUtf8(configDirectory) / iconPath;
        }
        if (!fs::exists(iconPath)) {
            result.errors.push_back("ERROR: Icon file not found: " + Utf8FromPath(iconPath));
            result.isValid = false;
        } else if (ToLowerCopy(Utf8FromPath(iconPath.extension())) != ".ico") {
            result.errors.push_back("ERROR: Icon file must be .ico: " + Utf8FromPath(iconPath));
            result.isValid = false;
        }
    }

    // hooks：每个钩子点可有多个脚本，逐个校验路径存在。
    for (size_t i = 0; i < config.hooks.preInstall.size(); ++i) {
        ValidateHook(config.hooks.preInstall[i],
                     "hooks.preInstall[" + std::to_string(i) + "]", configDirectory, result);
    }
    for (size_t i = 0; i < config.hooks.postInstall.size(); ++i) {
        ValidateHook(config.hooks.postInstall[i],
                     "hooks.postInstall[" + std::to_string(i) + "]", configDirectory, result);
    }

    // input
    const fs::path inputDir = PathFromUtf8(inputDirectory);
    if (!fs::exists(inputDir) || !fs::is_directory(inputDir)) {
        result.errors.push_back("ERROR: Input directory does not exist: " + inputDirectory);
        result.isValid = false;
    }

    return result;
}

} // namespace MultiThreadedInstaller
