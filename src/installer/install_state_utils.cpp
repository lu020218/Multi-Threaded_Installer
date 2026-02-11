#include "installer/install_state_utils.h"
#include "installer/file_system_operator.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"
#include "common/utf8_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace MultiThreadedInstaller {

bool applyInstallStateRegistry(const InstallStateConfig& config, const std::string& stateValue) {
#ifdef _WIN32
    if (config.registryPath.empty()) {
        return false;
    }
    
    std::string path = config.registryPath;
    std::string pathUpper = path;
    std::transform(pathUpper.begin(), pathUpper.end(), pathUpper.begin(), ::toupper);
    
    HKEY root = nullptr;
    std::string subkey;
    const std::string hkcu = "HKEY_CURRENT_USER\\";
    const std::string hklm = "HKEY_LOCAL_MACHINE\\";
    const std::string hkcuShort = "HKCU\\";
    const std::string hklmShort = "HKLM\\";
    
    if (pathUpper.rfind(hkcu, 0) == 0) {
        root = HKEY_CURRENT_USER;
        subkey = path.substr(hkcu.size());
    } else if (pathUpper.rfind(hklm, 0) == 0) {
        root = HKEY_LOCAL_MACHINE;
        subkey = path.substr(hklm.size());
    } else if (pathUpper.rfind(hkcuShort, 0) == 0) {
        root = HKEY_CURRENT_USER;
        subkey = path.substr(hkcuShort.size());
    } else if (pathUpper.rfind(hklmShort, 0) == 0) {
        root = HKEY_LOCAL_MACHINE;
        subkey = path.substr(hklmShort.size());
    } else {
        return false;
    }
    
    std::wstring subkeyW = Utf8ToWide(subkey);
    if (subkeyW.empty()) {
        return false;
    }
    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG status = RegCreateKeyExW(root, subkeyW.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disposition);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    
    const std::string& name = config.registryKey.empty() ? std::string("InstallState") : config.registryKey;
    std::wstring nameW = Utf8ToWide(name);
    std::wstring valueW = Utf8ToWide(stateValue);
    if (nameW.empty()) {
        RegCloseKey(key);
        return false;
    }
    status = RegSetValueExW(key, nameW.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(valueW.c_str()),
                            static_cast<DWORD>((valueW.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)config;
    (void)stateValue;
    return false;
#endif
}

bool applyInstallStateFile(const InstallStateConfig& config, const std::string& stateValue,
                           InstallerPathResolver& resolver) {
    if (config.filePath.empty()) {
        return false;
    }
    
    std::string expandedPath = resolver.expandEnvironmentVariables(config.filePath);
    if (expandedPath.empty()) {
        return false;
    }
    
    std::filesystem::path filePath = PathFromUtf8(expandedPath);
    std::filesystem::path parent = filePath.parent_path();
    if (!parent.empty()) {
        FileSystemOperator fs;
        if (!fs.createDirectoryRecursive(Utf8FromPath(parent))) {
            return false;
        }
    }
    
    std::ofstream out(toLongPath(filePath), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(stateValue.c_str(), static_cast<std::streamsize>(stateValue.size()));
    return static_cast<bool>(out);
}

HANDLE acquireInstallMutex(const InstallStateConfig& config) {
#ifdef _WIN32
    if (config.mutexName.empty()) {
        return nullptr;
    }
    std::wstring name = Utf8ToWide(config.mutexName);
    if (name.empty()) {
        return nullptr;
    }
    HANDLE handle = CreateMutexW(nullptr, FALSE, name.c_str());
    return handle;
#else
    (void)config;
    return nullptr;
#endif
}

void releaseInstallMutex(HANDLE handle) {
#ifdef _WIN32
    if (handle) {
        CloseHandle(handle);
    }
#else
    (void)handle;
#endif
}

void applyInstallState(const InstallStateConfig& config, const std::string& stateValue,
                       InstallerPathResolver& resolver) {
    if (config.mode == InstallStateMode::REGISTRY || config.mode == InstallStateMode::BOTH) {
        applyInstallStateRegistry(config, stateValue);
    }
    if (config.mode == InstallStateMode::FILE || config.mode == InstallStateMode::BOTH) {
        applyInstallStateFile(config, stateValue, resolver);
    }
}

bool removeInstallStateArtifacts(const InstallStateConfig& config, InstallerPathResolver& resolver) {
    bool ok = true;
    if (config.mode == InstallStateMode::REGISTRY || config.mode == InstallStateMode::BOTH) {
        RegistryEntry entry;
        entry.path = config.registryPath;
        entry.key = config.registryKey.empty() ? "InstallState" : config.registryKey;
        ok = deleteRegistryValue(entry) && ok;
    }
    if (config.mode == InstallStateMode::FILE || config.mode == InstallStateMode::BOTH) {
        std::string expanded = resolver.expandEnvironmentVariables(config.filePath);
        if (!expanded.empty()) {
            std::filesystem::remove(toLongPath(PathFromUtf8(expanded)));
        }
    }
    return ok;
}

} // namespace MultiThreadedInstaller
