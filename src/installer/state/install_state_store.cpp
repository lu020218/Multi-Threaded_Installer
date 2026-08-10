#include "installer/state/install_state_store.h"

#include "common/engine_defaults.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/platform/file_system_operator.h"
#include "installer/platform/installer_helpers.h"
#include "installer/state/registry_utils.h"

#include <filesystem>
#include <fstream>
#include <json.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {
namespace {

using json = nlohmann::json;

// 写一个值到产品注册表键 HKLM\Software\<product>。
void WriteProductRegistryValue(const std::string& registryPath,
                               const std::string& key,
                               const std::string& value) {
    RegistryEntry entry;
    entry.path = registryPath;
    entry.key = key;
    entry.value = value;
    entry.type = RegistryValueType::STRING;
    writeRegistryValue(entry, value, RegistryValueType::STRING);
}

bool WriteInstallStateFile(const std::string& filePath, const json& root) {
    std::filesystem::path path = PathFromUtf8(filePath);
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        FileSystemOperator fs;
        if (!fs.createDirectoryRecursive(Utf8FromPath(parent))) {
            logInstallerWarning("[InstallState] Failed to create state directory: " + Utf8FromPath(parent));
            return false;
        }
    }
    std::ofstream out(toLongPath(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        logInstallerWarning("[InstallState] Failed to open state file: " + filePath);
        return false;
    }
    const std::string payload = root.dump(2, ' ', false, json::error_handler_t::replace);
    out.write(payload.c_str(), static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(out);
}

} // namespace

std::string GetCurrentUserNameForInstallState() {
#ifdef _WIN32
    DWORD size = 0;
    GetUserNameW(nullptr, &size);
    if (size == 0) {
        return {};
    }
    std::wstring value(size, L'\0');
    if (!GetUserNameW(value.data(), &size) || size == 0) {
        return {};
    }
    value.resize(size - 1);
    return WideToUtf8(value);
#else
    return {};
#endif
}

bool ApplyInstallState(const InstallStateContext& context, InstallerPathResolver& resolver) {
    if (context.appName.empty()) {
        return false;
    }

    // [安装收尾-注册表写入] 产品状态注册表：写引擎写死的 HKLM\Software\<product> 下
    // Version/InstallDir/InstallSource/InstallState/InstalledBy 字段。这是覆盖/升级探测
    // 与卸载发现安装目录的权威来源（见 [旧版本-发现安装路径]）。
    const std::string registryPath = EngineDefaults::RegistryPath(context.appName);
    WriteProductRegistryValue(registryPath, "Version", context.version);
    WriteProductRegistryValue(registryPath, "InstallDir", context.installDir);
    WriteProductRegistryValue(registryPath, "InstallSource", context.installSource);
    WriteProductRegistryValue(registryPath, "InstallState", context.state);
    WriteProductRegistryValue(registryPath, "InstalledBy", context.userName);
    // 语言设置（--language / GUI 选择 / 系统推断）：写产品注册表，供应用与后续升级读取。
    if (!context.language.empty()) {
        WriteProductRegistryValue(registryPath, "Language", context.language);
    }

    // 文件：%ProgramData%\<product>\install-state.json。
    const std::string filePath =
        resolver.expandEnvironmentVariables(EngineDefaults::InstallStateFilePath(context.appName));
    json root = json::object();
    root["productName"] = context.appName;
    root["version"] = context.version;
    root["installDir"] = context.installDir;
    root["installSource"] = context.installSource;
    root["state"] = context.state;
    root["installedBy"] = context.userName;
    root["language"] = context.language;
    return WriteInstallStateFile(filePath, root);
}

bool CleanupInstallState(const InstallStateContext& context, InstallerPathResolver& resolver) {
    if (context.appName.empty()) {
        return true;
    }
    // 删产品注册表键与 install-state.json。
    deleteRegistryPath(EngineDefaults::RegistryPath(context.appName));
    const std::string filePath =
        resolver.expandEnvironmentVariables(EngineDefaults::InstallStateFilePath(context.appName));
    std::error_code ec;
    const std::filesystem::path statePath = PathFromUtf8(filePath);
    std::filesystem::remove(statePath, ec);
    // 删空的产品数据目录（install-state.json 的父目录 %ProgramData%\<product>）。仅当目录为空时
    // 删除——避免误删 hook/组件/应用写入该目录的其它数据；非空则保留。
    const std::filesystem::path parentDir = statePath.parent_path();
    std::error_code dirEc;
    if (!parentDir.empty() && std::filesystem::is_directory(parentDir, dirEc) &&
        std::filesystem::is_empty(parentDir, dirEc)) {
        std::filesystem::remove(parentDir, dirEc);
    }
    return true;
}

} // namespace MultiThreadedInstaller
