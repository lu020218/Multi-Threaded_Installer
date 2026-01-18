#include "installer/installer_helpers.h"
#include "installer/file_system_operator.h"
#include <algorithm>
#include <cctype>

namespace MultiThreadedInstaller {

std::filesystem::path toLongPath(const std::filesystem::path& path) {
#ifdef _WIN32
    std::filesystem::path absPath = std::filesystem::absolute(path);
    std::wstring native = absPath.native();
    if (native.rfind(LR"(\\?\)", 0) == 0) {
        return absPath;
    }
    if (native.rfind(LR"(\\)", 0) == 0) {
        std::wstring unc = LR"(\\?\UNC\)" + native.substr(2);
        return std::filesystem::path(unc);
    }
    std::wstring longPath = LR"(\\?\)" + native;
    return std::filesystem::path(longPath);
#else
    return path;
#endif
}

bool ensureFileWithSize(const std::filesystem::path& path, uint64_t size) {
    std::filesystem::path openPath = toLongPath(path);
    std::fstream file(openPath, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!file) {
        std::ofstream create(openPath, std::ios::binary | std::ios::trunc);
        if (!create) {
            return false;
        }
        create.close();
        file.open(openPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) {
            return false;
        }
    }
    
    if (size > 0) {
        file.seekp(static_cast<std::streamoff>(size - 1));
        char zero = 0;
        file.write(&zero, 1);
        file.flush();
    }
    
    return static_cast<bool>(file);
}

bool openFileForWrite(const std::filesystem::path& path, std::fstream& stream) {
    std::filesystem::path openPath = toLongPath(path);
    stream.open(openPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!stream) {
        std::ofstream create(openPath, std::ios::binary | std::ios::app);
        if (!create) {
            return false;
        }
        create.close();
        stream.open(openPath, std::ios::binary | std::ios::in | std::ios::out);
    }
    return static_cast<bool>(stream);
}

std::wstring toWideUtf8(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) {
        return std::wstring();
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), len);
    return wide;
#else
    (void)text;
    return std::wstring();
#endif
}

std::filesystem::path findPrimaryExecutable(const std::filesystem::path& installRoot,
                                            const std::string& appName) {
    std::filesystem::path candidate = installRoot / (appName + ".exe");
    if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
        return candidate;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(installRoot)) {
        if (entry.is_regular_file() && entry.path().extension() == ".exe") {
            return entry.path();
        }
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(installRoot)) {
        if (!entry.is_directory()) {
            continue;
        }
        for (const auto& fileEntry : std::filesystem::directory_iterator(entry.path())) {
            if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".exe") {
                return fileEntry.path();
            }
        }
    }
    
    return std::filesystem::path();
}

bool setAutoStartup(const std::string& appName, const std::filesystem::path& exePath) {
#ifdef _WIN32
    HKEY key = nullptr;
    LONG status = RegOpenKeyExA(HKEY_CURRENT_USER,
                                "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    
    std::string value = "\"" + exePath.string() + "\"";
    status = RegSetValueExA(key, appName.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>(value.size() + 1));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)appName;
    (void)exePath;
    return false;
#endif
}

bool removeAutoStartup(const std::string& appName) {
#ifdef _WIN32
    HKEY key = nullptr;
    LONG status = RegOpenKeyExA(HKEY_CURRENT_USER,
                                "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    status = RegDeleteValueA(key, appName.c_str());
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)appName;
    return false;
#endif
}

bool createDesktopShortcut(const std::string& appName, const std::filesystem::path& exePath) {
#ifdef _WIN32
    PWSTR desktopPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_CREATE, nullptr, &desktopPath);
    if (FAILED(hr) || !desktopPath) {
        return false;
    }
    
    std::wstring linkPath = std::wstring(desktopPath) + L"\\" + toWideUtf8(appName) + L".lnk";
    CoTaskMemFree(desktopPath);
    
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coInit = (hr == S_OK || hr == S_FALSE);
    if (!coInit && hr != RPC_E_CHANGED_MODE) {
        return false;
    }
    
    IShellLinkW* link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                          reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) {
        if (coInit) {
            CoUninitialize();
        }
        return false;
    }
    
    std::wstring targetPath = toWideUtf8(exePath.string());
    std::wstring workingDir = toWideUtf8(exePath.parent_path().string());
    link->SetPath(targetPath.c_str());
    if (!workingDir.empty()) {
        link->SetWorkingDirectory(workingDir.c_str());
    }
    link->SetDescription(toWideUtf8(appName).c_str());
    
    IPersistFile* persist = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
    if (FAILED(hr) || !persist) {
        link->Release();
        if (coInit) {
            CoUninitialize();
        }
        return false;
    }
    
    hr = persist->Save(linkPath.c_str(), TRUE);
    persist->Release();
    link->Release();
    if (coInit) {
        CoUninitialize();
    }
    
    return SUCCEEDED(hr);
#else
    (void)appName;
    (void)exePath;
    return false;
#endif
}

bool deleteDesktopShortcut(const std::string& appName) {
#ifdef _WIN32
    PWSTR desktopPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktopPath);
    if (FAILED(hr) || !desktopPath) {
        return false;
    }
    std::wstring linkPath = std::wstring(desktopPath) + L"\\" + toWideUtf8(appName) + L".lnk";
    CoTaskMemFree(desktopPath);
    return DeleteFileW(linkPath.c_str()) != 0;
#else
    (void)appName;
    return false;
#endif
}

std::string getCurrentExecutablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0) {
        return "";
    }
    return std::string(buffer, len);
#else
    char buffer[1024];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        return "";
    }
    buffer[len] = '\0';
    return std::string(buffer);
#endif
}

std::string getDefaultManifestPath(const std::string& appName, InstallerPathResolver& resolver) {
    std::string base = "%ProgramData%\\" + appName;
    std::string expanded = resolver.expandEnvironmentVariables(base);
    if (expanded.empty()) {
        return "";
    }
    std::filesystem::path path(expanded);
    path /= "install.manifest.json";
    return path.string();
}

std::string getLocalManifestPath(const std::string& exePath) {
    if (exePath.empty()) {
        return "";
    }
    std::filesystem::path path(exePath);
    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return "";
    }
    parent /= "install.manifest.json";
    return parent.string();
}

bool createUninstallStub(const std::string& sourcePath, const std::string& targetPath) {
    struct DataLocator {
        uint32_t magic;
        uint64_t metadataOffset;
        uint64_t metadataSize;
        uint64_t dataOffset;
        uint64_t dataSize;
    };
    
    std::ifstream in(toLongPath(std::filesystem::path(sourcePath)), std::ios::binary);
    if (!in) {
        return false;
    }
    
    in.seekg(0, std::ios::end);
    std::streampos fileSize = in.tellg();
    size_t locatorSize = sizeof(DataLocator) + sizeof(uint32_t);
    if (fileSize < static_cast<std::streampos>(locatorSize)) {
        return false;
    }
    
    in.seekg(-static_cast<std::streamoff>(sizeof(uint32_t)), std::ios::end);
    uint32_t endMagic = 0;
    in.read(reinterpret_cast<char*>(&endMagic), sizeof(uint32_t));
    if (endMagic != Constants::MAGIC_NUMBER) {
        return false;
    }
    
    in.seekg(-static_cast<std::streamoff>(locatorSize), std::ios::end);
    DataLocator locator{};
    in.read(reinterpret_cast<char*>(&locator), sizeof(DataLocator));
    if (locator.magic != Constants::MAGIC_NUMBER || locator.metadataOffset == 0) {
        return false;
    }
    
    if (locator.metadataOffset >= static_cast<uint64_t>(fileSize)) {
        return false;
    }
    
    std::ofstream out(toLongPath(std::filesystem::path(targetPath)), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    
    in.seekg(0, std::ios::beg);
    const size_t bufSize = 1024 * 1024;
    std::vector<char> buffer(bufSize);
    uint64_t remaining = locator.metadataOffset;
    while (remaining > 0) {
        size_t chunk = remaining > bufSize ? bufSize : static_cast<size_t>(remaining);
        in.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!in) {
            return false;
        }
        out.write(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!out) {
            return false;
        }
        remaining -= chunk;
    }
    return true;
}

} // namespace MultiThreadedInstaller
