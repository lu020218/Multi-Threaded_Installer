#include "installer/platform/shortcut_startup_utils.h"

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
        if (!entry.is_directory()) { continue; }
        for (const auto& fileEntry : std::filesystem::directory_iterator(entry.path())) {
            if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".exe") {
                return fileEntry.path();
            }
        }
    }
    return std::filesystem::path();
}

// [安装收尾-开机自启] 实现：在 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 下
// 写一个名为 appName、值为带引号 exe 路径的字符串值，实现当前用户开机自启动。
bool setAutoStartup(const std::string& appName, const std::filesystem::path& exePath) {
#ifdef _WIN32
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(HKEY_CURRENT_USER,
                                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) { return false; }
    std::wstring name = Utf8ToWide(appName);
    std::wstring value = L"\"" + exePath.wstring() + L"\"";
    status = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)appName; (void)exePath; return false;
#endif
}

bool removeAutoStartup(const std::string& appName) {
#ifdef _WIN32
    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(HKEY_CURRENT_USER,
                                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_SET_VALUE, &key);
    if (status != ERROR_SUCCESS) { return false; }
    std::wstring name = Utf8ToWide(appName);
    status = RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)appName; return false;
#endif
}

#ifdef _WIN32
namespace {

bool CreateShellLink(const std::wstring& linkPath,
                     const std::filesystem::path& exePath,
                     const std::string& description,
                     const std::filesystem::path& iconPath = {}) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coInit = (hr == S_OK || hr == S_FALSE);
    if (!coInit && hr != RPC_E_CHANGED_MODE) { return false; }

    IShellLinkW* link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) {
        if (coInit) { CoUninitialize(); }
        return false;
    }

    link->SetPath(exePath.wstring().c_str());
    std::wstring workingDir = exePath.parent_path().wstring();
    if (!workingDir.empty()) { link->SetWorkingDirectory(workingDir.c_str()); }
    link->SetDescription(Utf8ToWide(description).c_str());

    // [安装收尾-快捷方式图标] 支持手动指定图标：当 iconPath 非空且文件存在时，显式把
    // 快捷方式图标设为该文件（如安装目录下的 app.ico）；否则回退为目标 exe 自带图标。
    std::error_code iconEc;
    if (!iconPath.empty() && std::filesystem::exists(iconPath, iconEc)) {
        link->SetIconLocation(iconPath.wstring().c_str(), 0);
    }

    IPersistFile* persist = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
    if (FAILED(hr) || !persist) {
        link->Release();
        if (coInit) { CoUninitialize(); }
        return false;
    }

    hr = persist->Save(linkPath.c_str(), TRUE);
    persist->Release();
    link->Release();
    if (coInit) { CoUninitialize(); }
    return SUCCEEDED(hr);
}

} // namespace
#endif

// [安装收尾-桌面快捷方式] 实现：在桌面目录创建 <appName>.lnk，指向主 exe。
// iconPath 非空时（如安装目录下的 app.ico）作为快捷方式图标手动指定，否则用 exe 自带图标。
bool createDesktopShortcut(const std::string& appName,
                           const std::filesystem::path& exePath,
                           const std::filesystem::path& iconPath) {
#ifdef _WIN32
    PWSTR desktopPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_CREATE, nullptr, &desktopPath);
    if (FAILED(hr) || !desktopPath) { return false; }
    std::wstring linkPath = std::wstring(desktopPath) + L"\\" + Utf8ToWide(appName) + L".lnk";
    CoTaskMemFree(desktopPath);
    return CreateShellLink(linkPath, exePath, appName, iconPath);
#else
    (void)appName; (void)exePath; (void)iconPath; return false;
#endif
}

bool deleteDesktopShortcut(const std::string& appName) {
#ifdef _WIN32
    PWSTR desktopPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktopPath);
    if (FAILED(hr) || !desktopPath) { return false; }
    std::wstring linkPath = std::wstring(desktopPath) + L"\\" + Utf8ToWide(appName) + L".lnk";
    CoTaskMemFree(desktopPath);
    return DeleteFileW(linkPath.c_str()) != 0;
#else
    (void)appName; return false;
#endif
}

// [安装收尾-开始菜单快捷方式] 实现：在开始菜单 Programs 下建 <appName> 目录及同名 .lnk。
bool createStartMenuShortcut(const std::string& appName,
                             const std::filesystem::path& exePath,
                             const std::string& uninstallDisplayName) {
#ifdef _WIN32
    (void)uninstallDisplayName;
    PWSTR programsDir = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &programsDir);
    if (FAILED(hr) || !programsDir) { return false; }

    std::filesystem::path appFolder = std::filesystem::path(programsDir) / Utf8ToWide(appName);
    CoTaskMemFree(programsDir);

    std::error_code ec;
    std::filesystem::create_directories(appFolder, ec);
    if (ec) { return false; }

    std::wstring linkPath = (appFolder / (Utf8ToWide(appName) + L".lnk")).wstring();
    return CreateShellLink(linkPath, exePath, appName);
#else
    (void)appName; (void)exePath; (void)uninstallDisplayName;
    return false;
#endif
}

bool deleteStartMenuShortcut(const std::string& appName) {
#ifdef _WIN32
    PWSTR programsDir = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_DEFAULT, nullptr, &programsDir);
    if (FAILED(hr) || !programsDir) { return false; }

    std::filesystem::path appFolder = std::filesystem::path(programsDir) / Utf8ToWide(appName);
    CoTaskMemFree(programsDir);

    std::error_code ec;
    std::filesystem::remove_all(appFolder, ec);
    return !ec;
#else
    (void)appName;
    return false;
#endif
}

}  // namespace MultiThreadedInstaller
