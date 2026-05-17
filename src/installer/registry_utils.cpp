#include "installer/registry_utils.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
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

std::string quoteIfNeeded(const std::string& value) {
    if (value.find(' ') == std::string::npos && value.find('\t') == std::string::npos) {
        return value;
    }
    return "\"" + value + "\"";
}

std::string normalizeRegistrySubkey(const std::string& rawSubkey) {
    std::string normalized;
    normalized.reserve(rawSubkey.size());

    bool previousSlash = false;
    for (char ch : rawSubkey) {
        char current = (ch == '/') ? '\\' : ch;
        if (current == '\\') {
            if (previousSlash) {
                continue;
            }
            previousSlash = true;
        } else {
            previousSlash = false;
        }
        normalized.push_back(current);
    }

    size_t start = 0;
    while (start < normalized.size() && normalized[start] == '\\') {
        ++start;
    }

    size_t end = normalized.size();
    while (end > start && normalized[end - 1] == '\\') {
        --end;
    }

    return normalized.substr(start, end - start);
}

std::string normalizePathForCompareLocal(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    while (!normalized.empty() && (normalized.back() == '\\' || normalized.back() == '/')) {
        normalized.pop_back();
    }
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

std::string extractExecutablePathFromCommand(const std::string& command) {
    std::string trimmed = command;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(),
                                                [](unsigned char c) { return !std::isspace(c); }));
    if (trimmed.empty()) {
        return {};
    }
    if (trimmed.front() == '"') {
        size_t endQuote = trimmed.find('"', 1);
        if (endQuote != std::string::npos) {
            return trimmed.substr(1, endQuote - 1);
        }
    }
    size_t firstSpace = trimmed.find_first_of(" \t");
    return firstSpace == std::string::npos ? trimmed : trimmed.substr(0, firstSpace);
}

#ifdef _WIN32
bool deleteRegistryTree(HKEY root, const std::string& subkey) {
    std::wstring subkeyW = Utf8ToWide(subkey);
    if (subkeyW.empty()) {
        return false;
    }
    LONG status = RegDeleteTreeW(root, subkeyW.c_str());
    if (status == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    return status == ERROR_SUCCESS;
}

std::string queryRegistryString(HKEY key, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD size = 0;
    LONG status = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
        return {};
    }

    std::wstring value;
    value.resize(size / sizeof(wchar_t));
    status = RegQueryValueExW(key, valueName, nullptr, &type,
                              reinterpret_cast<BYTE*>(&value[0]), &size);
    if (status != ERROR_SUCCESS) {
        return {};
    }
    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    if (type == REG_EXPAND_SZ) {
        DWORD expandedSize = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (expandedSize > 0) {
            std::wstring expanded(expandedSize, L'\0');
            if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), expandedSize) > 0) {
                if (!expanded.empty() && expanded.back() == L'\0') {
                    expanded.pop_back();
                }
                return WideToUtf8(expanded);
            }
        }
    }
    return WideToUtf8(value);
}

std::string normalizeRegistryNameForCompare(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool deleteUninstallRegistryEntriesByDisplayName(const std::string& displayName, bool perMachine) {
    const std::string targetName = normalizeRegistryNameForCompare(displayName);
    if (targetName.empty()) {
        return false;
    }

    HKEY root = perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    const wchar_t* uninstallSubkeyW = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    HKEY uninstallKey = nullptr;
    LONG status = RegOpenKeyExW(root, uninstallSubkeyW, 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &uninstallKey);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    std::vector<std::string> matchedSubkeys;
    DWORD index = 0;
    wchar_t nameBuffer[512];
    DWORD nameLen = static_cast<DWORD>(std::size(nameBuffer));
    while (RegEnumKeyExW(uninstallKey, index, nameBuffer, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        std::wstring subkeyNameW(nameBuffer, nameLen);
        HKEY entryKey = nullptr;
        if (RegOpenKeyExW(root, (std::wstring(uninstallSubkeyW) + L"\\" + subkeyNameW).c_str(),
                          0, KEY_QUERY_VALUE, &entryKey) == ERROR_SUCCESS) {
            const std::string entryDisplayName = normalizeRegistryNameForCompare(
                queryRegistryString(entryKey, L"DisplayName"));
            RegCloseKey(entryKey);
            if (entryDisplayName == targetName) {
                matchedSubkeys.push_back("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" +
                                         WideToUtf8(subkeyNameW));
            }
        }

        ++index;
        nameLen = static_cast<DWORD>(std::size(nameBuffer));
    }

    RegCloseKey(uninstallKey);

    bool removedAny = false;
    for (const auto& subkey : matchedSubkeys) {
        if (deleteRegistryTree(root, subkey)) {
            removedAny = true;
        }
    }
    return removedAny;
}
#endif

} // namespace

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

    subkey = normalizeRegistrySubkey(subkey);
    if (subkey.empty()) {
        return false;
    }

    std::wstring subkeyW = Utf8ToWide(subkey);
    std::wstring keyW = Utf8ToWide(entry.key);
    if (subkeyW.empty() || keyW.empty()) {
        return false;
    }

    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(root, subkeyW.c_str(), 0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    status = RegDeleteValueW(key, keyW.c_str());
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)entry;
    return false;
#endif
}

bool deleteRegistryPath(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) {
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

    subkey = normalizeRegistrySubkey(subkey);
    if (subkey.empty()) {
        return false;
    }

    return deleteRegistryTree(root, subkey);
#else
    (void)path;
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

    subkey = normalizeRegistrySubkey(subkey);
    if (subkey.empty()) {
        return false;
    }

    std::wstring subkeyW = Utf8ToWide(subkey);
    std::wstring keyW = Utf8ToWide(entry.key);
    if (subkeyW.empty() || keyW.empty()) {
        return false;
    }

    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG status = RegCreateKeyExW(root, subkeyW.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, &disposition);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    
    bool ok = false;
    if (type == RegistryValueType::DWORD) {
        try {
            uint32_t number = std::stoul(value, nullptr, 0);
            status = RegSetValueExW(key, keyW.c_str(), 0, REG_DWORD,
                                    reinterpret_cast<const BYTE*>(&number),
                                    static_cast<DWORD>(sizeof(uint32_t)));
            ok = (status == ERROR_SUCCESS);
        } catch (...) {
            ok = false;
        }
    } else if (type == RegistryValueType::EXPAND_STRING) {
        std::wstring wideValue = Utf8ToWide(value);
        status = RegSetValueExW(key, keyW.c_str(), 0, REG_EXPAND_SZ,
                                reinterpret_cast<const BYTE*>(wideValue.c_str()),
                                static_cast<DWORD>((wideValue.size() + 1) * sizeof(wchar_t)));
        ok = (status == ERROR_SUCCESS);
    } else {
        std::wstring wideValue = Utf8ToWide(value);
        status = RegSetValueExW(key, keyW.c_str(), 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(wideValue.c_str()),
                                static_cast<DWORD>((wideValue.size() + 1) * sizeof(wchar_t)));
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
            logInstallerInfo(std::string("[REG] Registry write ok: ") + entry.path +
                             " [" + entry.key + "]=" + expanded);
        } else {
            logInstallerWarning(std::string("[REG] Registry write failed: ") + entry.path +
                                " [" + entry.key + "]");
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

    subkey = normalizeRegistrySubkey(subkey);
    if (subkey.empty()) {
        return false;
    }

    std::wstring subkeyW = Utf8ToWide(subkey);
    std::wstring keyW = Utf8ToWide(key);
    if (subkeyW.empty() || keyW.empty()) {
        return false;
    }

    HKEY hKey = nullptr;
    LONG status = RegOpenKeyExW(root, subkeyW.c_str(), 0, KEY_QUERY_VALUE, &hKey);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    status = RegQueryValueExW(hKey, keyW.c_str(), nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(hKey);
        return false;
    }

    std::wstring buffer;
    buffer.resize(size / sizeof(wchar_t));
    status = RegQueryValueExW(hKey, keyW.c_str(), nullptr, &type,
                              reinterpret_cast<BYTE*>(&buffer[0]), &size);
    RegCloseKey(hKey);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    if (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }

    if (type == REG_EXPAND_SZ) {
        DWORD expandedSize = ExpandEnvironmentStringsW(buffer.c_str(), nullptr, 0);
        if (expandedSize > 0) {
            std::wstring expanded;
            expanded.resize(expandedSize);
            if (ExpandEnvironmentStringsW(buffer.c_str(), expanded.data(), expandedSize) > 0) {
                if (!expanded.empty() && expanded.back() == L'\0') {
                    expanded.pop_back();
                }
                value = WideToUtf8(expanded);
                return true;
            }
        }
    }

    value = WideToUtf8(buffer);
    return !value.empty();
#else
    (void)path;
    (void)key;
    (void)value;
    return false;
#endif
}


bool writeUninstallRegistryEntry(const std::string& appName,
                                 const std::string& displayName,
                                 const std::string& version,
                                 const std::string& installDir,
                                 const std::string& uninstallExePath,
                                 bool perMachine,
                                 const std::string& publisher) {
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

    // DisplayName uses the user-visible name, not the internal appId.
    const std::string& effectiveDisplayName = displayName.empty() ? appName : displayName;
    entry.key = "DisplayName";
    if (!writeRegistryValue(entry, effectiveDisplayName, RegistryValueType::STRING)) {
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
    writeRegistryValue(entry, publisher.empty() ? appName : publisher, RegistryValueType::STRING);

    entry.key = "NoModify";
    writeRegistryValue(entry, "1", RegistryValueType::DWORD);

    entry.key = "NoRepair";
    writeRegistryValue(entry, "1", RegistryValueType::DWORD);

    return true;
#else
    (void)appName;
    (void)displayName;
    (void)version;
    (void)installDir;
    (void)uninstallExePath;
    (void)perMachine;
    (void)publisher;
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

    return deleteRegistryTree(root, subkey);
#else
    (void)appName;
    (void)perMachine;
    return false;
#endif
}

bool deleteSystemUninstallEntryByDisplayName(const std::string& displayName,
                                             UninstallEntryScope scope) {
#ifdef _WIN32
    switch (scope) {
    case UninstallEntryScope::CURRENT_USER:
        return deleteUninstallRegistryEntriesByDisplayName(displayName, false);
    case UninstallEntryScope::LOCAL_MACHINE:
    case UninstallEntryScope::WOW6432:
        return deleteUninstallRegistryEntriesByDisplayName(displayName, true);
    case UninstallEntryScope::BOTH:
        return deleteUninstallRegistryEntriesByDisplayName(displayName, false) ||
               deleteUninstallRegistryEntriesByDisplayName(displayName, true);
    case UninstallEntryScope::ANY:
    default:
        return false;
    }
#else
    (void)displayName;
    (void)scope;
    return false;
#endif
}

bool deleteMatchingUninstallRegistryEntries(const std::string& installDir,
                                            const std::string& uninstallExePath,
                                            bool perMachine) {
#ifdef _WIN32
    const std::string normalizedInstallDir = normalizePathForCompareLocal(installDir);
    const std::string normalizedUninstallExe = normalizePathForCompareLocal(uninstallExePath);
    if (normalizedInstallDir.empty() && normalizedUninstallExe.empty()) {
        return false;
    }

    HKEY root = perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    const wchar_t* uninstallSubkeyW = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    HKEY uninstallKey = nullptr;
    LONG status = RegOpenKeyExW(root, uninstallSubkeyW, 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &uninstallKey);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    std::vector<std::string> matchedSubkeys;
    DWORD index = 0;
    wchar_t nameBuffer[512];
    DWORD nameLen = static_cast<DWORD>(std::size(nameBuffer));
    while (RegEnumKeyExW(uninstallKey, index, nameBuffer, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        std::wstring subkeyNameW(nameBuffer, nameLen);
        std::string subkeyName = WideToUtf8(subkeyNameW);
        std::string fullPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + subkeyName;

        HKEY entryKey = nullptr;
        if (RegOpenKeyExW(root, (std::wstring(uninstallSubkeyW) + L"\\" + subkeyNameW).c_str(),
                          0, KEY_QUERY_VALUE, &entryKey) == ERROR_SUCCESS) {
            auto queryString = [&](const wchar_t* valueName) -> std::string {
                DWORD type = 0;
                DWORD size = 0;
                LONG queryStatus = RegQueryValueExW(entryKey, valueName, nullptr, &type, nullptr, &size);
                if (queryStatus != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) {
                    return {};
                }
                std::wstring value;
                value.resize(size / sizeof(wchar_t));
                queryStatus = RegQueryValueExW(entryKey, valueName, nullptr, &type,
                                               reinterpret_cast<BYTE*>(&value[0]), &size);
                if (queryStatus != ERROR_SUCCESS) {
                    return {};
                }
                if (!value.empty() && value.back() == L'\0') {
                    value.pop_back();
                }
                if (type == REG_EXPAND_SZ) {
                    DWORD expandedSize = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
                    if (expandedSize > 0) {
                        std::wstring expanded(expandedSize, L'\0');
                        if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), expandedSize) > 0) {
                            if (!expanded.empty() && expanded.back() == L'\0') {
                                expanded.pop_back();
                            }
                            return WideToUtf8(expanded);
                        }
                    }
                }
                return WideToUtf8(value);
            };

            const std::string entryInstallLocation = normalizePathForCompareLocal(queryString(L"InstallLocation"));
            const std::string entryUninstallPath =
                normalizePathForCompareLocal(extractExecutablePathFromCommand(queryString(L"UninstallString")));
            RegCloseKey(entryKey);

            bool matches = false;
            if (!normalizedInstallDir.empty() && !entryInstallLocation.empty() &&
                entryInstallLocation == normalizedInstallDir) {
                matches = true;
            }
            if (!matches && !normalizedUninstallExe.empty() && !entryUninstallPath.empty() &&
                entryUninstallPath == normalizedUninstallExe) {
                matches = true;
            }
            if (matches) {
                matchedSubkeys.push_back(fullPath);
            }
        }

        ++index;
        nameLen = static_cast<DWORD>(std::size(nameBuffer));
    }

    RegCloseKey(uninstallKey);

    bool removedAny = false;
    for (const auto& subkey : matchedSubkeys) {
        if (deleteRegistryTree(root, subkey)) {
            removedAny = true;
        }
    }
    return removedAny;
#else
    (void)installDir;
    (void)uninstallExePath;
    (void)perMachine;
    return false;
#endif
}


} // namespace MultiThreadedInstaller
