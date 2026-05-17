#include "installer/install_state_store.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/file_system_operator.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"

#include <filesystem>
#include <fstream>
#include <json.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {
namespace {

using json = nlohmann::json;

std::string ReplaceAll(std::string value, const std::string& token, const std::string& replacement) {
    if (token.empty()) {
        return value;
    }
    size_t pos = 0;
    while ((pos = value.find(token, pos)) != std::string::npos) {
        value.replace(pos, token.size(), replacement);
        pos += replacement.size();
    }
    return value;
}

std::string NormalizeCleanupMode(const std::string& mode) {
    std::string normalized = mode;
    for (char& ch : normalized) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (normalized.empty()) {
        return "delete";
    }
    return normalized;
}

bool DeleteFileStore(const InstallStateFileStoreConfig& store,
                     const InstallStateContext& context,
                     InstallerPathResolver& resolver) {
    const std::string expandedPath = ExpandInstallStateTokenValue(store.path, context, resolver);
    if (expandedPath.empty()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::remove(PathFromUtf8(expandedPath), ec);
    if (ec) {
        logInstallerWarning("[InstallState] Failed to delete file store: " + expandedPath +
                            " error=" + ec.message());
        return false;
    }
    return true;
}

bool WriteRegistryStore(const InstallStateRegistryStoreConfig& store,
                        const InstallStateContext& context,
                        InstallerPathResolver& resolver) {
    if (store.path.empty()) {
        return false;
    }
    const std::string expandedPath = ExpandInstallStateTokenValue(store.path, context, resolver);
    bool wroteAny = false;
    bool ok = true;
    for (const auto& pair : store.values) {
        const auto& valueConfig = pair.second;
        if (valueConfig.key.empty()) {
            logInstallerWarning("[InstallState] Registry value key is empty for logical value: " + pair.first);
            continue;
        }
        RegistryEntry entry;
        entry.path = expandedPath;
        entry.key = valueConfig.key;
        entry.value = valueConfig.value;
        entry.type = valueConfig.type;
        const std::string expandedValue =
            ExpandInstallStateTokenValue(valueConfig.value, context, resolver);
        wroteAny = true;
        ok = writeRegistryValue(entry, expandedValue, entry.type) && ok;
    }
    return wroteAny && ok;
}

bool WriteFileStore(const InstallStateFileStoreConfig& store,
                    const InstallStateContext& context,
                    InstallerPathResolver& resolver) {
    if (store.path.empty()) {
        return false;
    }
    std::string format = store.format;
    for (char& ch : format) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (!format.empty() && format != "json") {
        logInstallerWarning("[InstallState] Unsupported file store format: " + store.format);
        return false;
    }

    const std::string expandedPath = ExpandInstallStateTokenValue(store.path, context, resolver);
    if (expandedPath.empty()) {
        return false;
    }

    json root = json::object();
    for (const auto& pair : store.values) {
        const auto& valueConfig = pair.second;
        const std::string fieldName = valueConfig.name.empty() ? pair.first : valueConfig.name;
        if (fieldName.empty()) {
            continue;
        }
        root[fieldName] = ExpandInstallStateTokenValue(valueConfig.value, context, resolver);
    }

    std::filesystem::path path = PathFromUtf8(expandedPath);
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        FileSystemOperator fs;
        if (!fs.createDirectoryRecursive(Utf8FromPath(parent))) {
            logInstallerWarning("[InstallState] Failed to create file store directory: " + Utf8FromPath(parent));
            return false;
        }
    }

    std::ofstream out(toLongPath(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        logInstallerWarning("[InstallState] Failed to open file store: " + expandedPath);
        return false;
    }
    const std::string payload = root.dump(2, ' ', false, json::error_handler_t::replace);
    out.write(payload.c_str(), static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(out);
}

} // namespace

std::string ExpandInstallStateTokenValue(const std::string& value,
                                         const InstallStateContext& context,
                                         InstallerPathResolver& resolver) {
    std::string expanded = value;
    expanded = ReplaceAll(expanded, "%InstallDir%", context.installDir);
    expanded = ReplaceAll(expanded, "%Version%", context.version);
    expanded = ReplaceAll(expanded, "%AppName%", context.appName);
    expanded = ReplaceAll(expanded, "%AppId%", context.appId);
    expanded = ReplaceAll(expanded, "%InstallState%", context.state);
    expanded = ReplaceAll(expanded, "%UserName%", context.userName);
    expanded = ReplaceAll(expanded, "%InstallSource%", context.installSource);
    return resolver.expandEnvironmentVariables(expanded);
}

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

bool ApplyInstallState(const InstallStateConfig& config,
                       const InstallStateContext& context,
                       InstallerPathResolver& resolver) {
    bool touchedAny = false;
    bool ok = true;
    for (const auto& store : config.registries) {
        touchedAny = true;
        ok = WriteRegistryStore(store, context, resolver) && ok;
    }
    for (const auto& store : config.files) {
        touchedAny = true;
        ok = WriteFileStore(store, context, resolver) && ok;
    }
    return touchedAny && ok;
}

bool CleanupInstallState(const InstallStateConfig& config,
                         const std::string& mode,
                         const InstallStateContext& context,
                         InstallerPathResolver& resolver) {
    const std::string normalizedMode = NormalizeCleanupMode(mode);
    if (normalizedMode == "keep") {
        return true;
    }
    if (normalizedMode == "markuninstalled" || normalizedMode == "mark_uninstalled") {
        InstallStateContext uninstalledContext = context;
        uninstalledContext.state = "uninstalled";
        return ApplyInstallState(config, uninstalledContext, resolver);
    }

    bool ok = true;
    for (const auto& store : config.registries) {
        const std::string expandedPath = ExpandInstallStateTokenValue(store.path, context, resolver);
        for (const auto& pair : store.values) {
            if (pair.second.key.empty()) {
                continue;
            }
            RegistryEntry entry;
            entry.path = expandedPath;
            entry.key = pair.second.key;
            ok = deleteRegistryValue(entry) && ok;
        }
    }
    for (const auto& store : config.files) {
        ok = DeleteFileStore(store, context, resolver) && ok;
    }
    return ok;
}

} // namespace MultiThreadedInstaller
