#include "installer/shortcut_startup_utils.h"

#include "common/utf8_utils.h"

#ifdef _WIN32
#include <Windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#endif

namespace MultiThreadedInstaller {

std::filesystem::path findPrimaryExecutable(const std::filesystem::path& installRoot,
                                            const std::string& appName) {
    std::filesystem::path candidate = installRoot / PathFromUtf8(appName + ".exe");
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
    LONG status = RegOpenKeyExW(HKEY_CURRENT_USER,
                                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    std::wstring name = Utf8ToWide(appName);
    std::wstring value = L"\"" + exePath.wstring() + L"\"";
    status = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
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
    LONG status = RegOpenKeyExW(HKEY_CURRENT_USER,
                                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    std::wstring name = Utf8ToWide(appName);
    status = RegDeleteValueW(key, name.c_str());
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

    std::wstring linkPath = std::wstring(desktopPath) + L"\\" + Utf8ToWide(appName) + L".lnk";
    CoTaskMemFree(desktopPath);

    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coInit = (hr == S_OK || hr == S_FALSE);
    if (!coInit && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    IShellLinkW* link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink,
                          nullptr,
                          CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW,
                          reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) {
        if (coInit) {
            CoUninitialize();
        }
        return false;
    }

    std::wstring targetPath = exePath.wstring();
    std::wstring workingDir = exePath.parent_path().wstring();
    link->SetPath(targetPath.c_str());
    if (!workingDir.empty()) {
        link->SetWorkingDirectory(workingDir.c_str());
    }
    link->SetDescription(Utf8ToWide(appName).c_str());

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
    std::wstring linkPath = std::wstring(desktopPath) + L"\\" + Utf8ToWide(appName) + L".lnk";
    CoTaskMemFree(desktopPath);
    return DeleteFileW(linkPath.c_str()) != 0;
#else
    (void)appName;
    return false;
#endif
}

}  // namespace MultiThreadedInstaller
