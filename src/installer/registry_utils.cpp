#include "installer/registry_utils.h"
#include <algorithm>
#include <cctype>
#include <iostream>

namespace MultiThreadedInstaller {

namespace {

std::string replaceAll(std::string value, const std::string& token, const std::string& replacement) {
    if (token.empty()) {
        return value;
    }
    size_t pos = 0;
    while ((pos = value.find(token, pos)) != std::string::npos) {
        value.replace(pos, token.length(), replacement);
        pos += replacement.length();
    }
    return value;
}

std::string expandRegistryValue(const std::string& value,
                                const std::string& installDir,
                                const std::string& configVersion,
                                const std::string& appName) {
    std::string result = value;
    result = replaceAll(result, "%InstallDir%", installDir);
    result = replaceAll(result, "%Version%", configVersion);
    result = replaceAll(result, "%AppName%", appName);
    return result;
}

std::string sanitizeRegistryKeyName(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    if (result.empty()) {
        result = "Application";
    }
    return result;
}

std::string quoteIfNeeded(const std::string& value) {
    if (value.find(' ') == std::string::npos && value.find('\t') == std::string::npos) {
        return value;
    }
    return "\"" + value + "\"";
}

} // namespace

bool deleteRegistryValue(const RegistryEntry& entry) {
#ifdef _WIN32
    if (entry.path.empty() || entry.key.empty()) {
        return false;
    }
    
    std::string path = entry.path;
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
    
    HKEY key = nullptr;
    LONG status = RegOpenKeyExA(root, subkey.c_str(), 0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    
    status = RegDeleteValueA(key, entry.key.c_str());
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)entry;
    return false;
#endif
}

bool writeRegistryValue(const RegistryEntry& entry, const std::string& value, RegistryValueType type) {
#ifdef _WIN32
    if (entry.path.empty() || entry.key.empty()) {
        return false;
    }
    
    std::string path = entry.path;
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
    
    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG status = RegCreateKeyExA(root, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disposition);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    
    bool ok = false;
    if (type == RegistryValueType::DWORD) {
        try {
            uint32_t number = std::stoul(value, nullptr, 0);
            status = RegSetValueExA(key, entry.key.c_str(), 0, REG_DWORD,
                                    reinterpret_cast<const BYTE*>(&number),
                                    static_cast<DWORD>(sizeof(uint32_t)));
            ok = (status == ERROR_SUCCESS);
        } catch (...) {
            ok = false;
        }
    } else if (type == RegistryValueType::EXPAND_STRING) {
        status = RegSetValueExA(key, entry.key.c_str(), 0, REG_EXPAND_SZ,
                                reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>(value.size() + 1));
        ok = (status == ERROR_SUCCESS);
    } else {
        status = RegSetValueExA(key, entry.key.c_str(), 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>(value.size() + 1));
        ok = (status == ERROR_SUCCESS);
    }
    
    RegCloseKey(key);
    return ok;
#else
    (void)entry;
    (void)value;
    (void)type;
    return false;
#endif
}

void applyRegistryEntries(const std::vector<RegistryEntry>& entries,
                          const std::string& installDir,
                          const std::string& configVersion,
                          const std::string& appName) {
    for (const auto& entry : entries) {
        std::string expanded = expandRegistryValue(entry.value, installDir, configVersion, appName);
        bool ok = writeRegistryValue(entry, expanded, entry.type);
        if (ok) {
            std::cout << "INFO: Registry write ok: " << entry.path
                      << " [" << entry.key << "]=" << expanded << std::endl;
        } else {
            std::cout << "WARNING: Registry write failed: " << entry.path
                      << " [" << entry.key << "]" << std::endl;
        }
    }
}

bool readRegistryStringValue(const std::string& path, const std::string& key, std::string& value) {
#ifdef _WIN32
    if (path.empty() || key.empty()) {
        return false;
    }

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

    HKEY hKey = nullptr;
    LONG status = RegOpenKeyExA(root, subkey.c_str(), 0, KEY_QUERY_VALUE, &hKey);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    status = RegQueryValueExA(hKey, key.c_str(), nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(hKey);
        return false;
    }

    std::string buffer(size, '\0');
    status = RegQueryValueExA(hKey, key.c_str(), nullptr, &type,
                              reinterpret_cast<BYTE*>(&buffer[0]), &size);
    RegCloseKey(hKey);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    if (!buffer.empty() && buffer.back() == '\0') {
        buffer.pop_back();
    }

    if (type == REG_EXPAND_SZ) {
        char expanded[MAX_PATH];
        DWORD expandedSize = ExpandEnvironmentStringsA(buffer.c_str(), expanded, MAX_PATH);
        if (expandedSize > 0 && expandedSize < MAX_PATH) {
            value.assign(expanded);
            return true;
        }
    }

    value = buffer;
    return !value.empty();
#else
    (void)path;
    (void)key;
    (void)value;
    return false;
#endif
}

bool writeUninstallRegistryEntry(const std::string& appName,
                                 const std::string& version,
                                 const std::string& installDir,
                                 const std::string& uninstallExePath,
                                 bool perMachine) {
#ifdef _WIN32
    if (appName.empty() || uninstallExePath.empty()) {
        return false;
    }

    std::string keyName = sanitizeRegistryKeyName(appName);
    std::string basePath = perMachine
        ? "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
        : "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
    std::string fullPath = basePath + keyName;

    RegistryEntry entry;
    entry.path = fullPath;

    entry.key = "DisplayName";
    if (!writeRegistryValue(entry, appName, RegistryValueType::STRING)) {
        return false;
    }

    if (!version.empty()) {
        entry.key = "DisplayVersion";
        writeRegistryValue(entry, version, RegistryValueType::STRING);
    }

    if (!installDir.empty()) {
        entry.key = "InstallLocation";
        writeRegistryValue(entry, installDir, RegistryValueType::STRING);
    }

    entry.key = "UninstallString";
    std::string uninstallCommand = quoteIfNeeded(uninstallExePath);
    if (!writeRegistryValue(entry, uninstallCommand, RegistryValueType::STRING)) {
        return false;
    }

    entry.key = "DisplayIcon";
    writeRegistryValue(entry, uninstallCommand, RegistryValueType::STRING);

    entry.key = "Publisher";
    writeRegistryValue(entry, appName, RegistryValueType::STRING);

    entry.key = "NoModify";
    writeRegistryValue(entry, "1", RegistryValueType::DWORD);

    entry.key = "NoRepair";
    writeRegistryValue(entry, "1", RegistryValueType::DWORD);

    return true;
#else
    (void)appName;
    (void)version;
    (void)installDir;
    (void)uninstallExePath;
    (void)perMachine;
    return false;
#endif
}

bool deleteUninstallRegistryEntry(const std::string& appName, bool perMachine) {
#ifdef _WIN32
    if (appName.empty()) {
        return false;
    }
    std::string keyName = sanitizeRegistryKeyName(appName);
    std::string basePath = perMachine
        ? "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
        : "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
    std::string fullPath = basePath + keyName;

    std::string pathUpper = fullPath;
    std::transform(pathUpper.begin(), pathUpper.end(), pathUpper.begin(), ::toupper);

    HKEY root = nullptr;
    std::string subkey;
    const std::string hkcu = "HKEY_CURRENT_USER\\";
    const std::string hklm = "HKEY_LOCAL_MACHINE\\";
    const std::string hkcuShort = "HKCU\\";
    const std::string hklmShort = "HKLM\\";

    if (pathUpper.rfind(hkcu, 0) == 0) {
        root = HKEY_CURRENT_USER;
        subkey = fullPath.substr(hkcu.size());
    } else if (pathUpper.rfind(hklm, 0) == 0) {
        root = HKEY_LOCAL_MACHINE;
        subkey = fullPath.substr(hklm.size());
    } else if (pathUpper.rfind(hkcuShort, 0) == 0) {
        root = HKEY_CURRENT_USER;
        subkey = fullPath.substr(hkcuShort.size());
    } else if (pathUpper.rfind(hklmShort, 0) == 0) {
        root = HKEY_LOCAL_MACHINE;
        subkey = fullPath.substr(hklmShort.size());
    } else {
        return false;
    }

    LONG status = RegDeleteTreeA(root, subkey.c_str());
    if (status == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    return status == ERROR_SUCCESS;
#else
    (void)appName;
    (void)perMachine;
    return false;
#endif
}

} // namespace MultiThreadedInstaller
