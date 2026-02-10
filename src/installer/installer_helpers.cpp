#include "installer/installer_helpers.h"
#include "installer/file_system_operator.h"
#include "common/utf8_utils.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <exception>
#include <cstdlib>
#include <iostream>
#include <unordered_set>

#ifdef _WIN32
#include <Windows.h>
#include <winioctl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#endif

namespace MultiThreadedInstaller {

namespace {

bool startsWithNoCase(const std::string& value, const std::string& prefix) {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::string normalizePathForCompare(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    while (!normalized.empty() && (normalized.back() == '\\' || normalized.back() == '/')) {
        normalized.pop_back();
    }
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

std::string trimAscii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    if (start == value.size()) {
        return std::string();
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string normalizeProcessNameInternal(const std::string& name) {
    std::string trimmed = trimAscii(name);
    if (trimmed.empty()) {
        return std::string();
    }
    std::filesystem::path path = PathFromUtf8(trimmed);
    std::string base = Utf8FromPath(path.filename());
    if (base.empty()) {
        return std::string();
    }
    std::string lower = base;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".exe") {
        lower += ".exe";
    }
    return lower;
}
bool isPathUnder(const std::string& path, const std::string& base) {
    if (path.empty() || base.empty()) {
        return false;
    }
    std::string normalizedPath = normalizePathForCompare(path);
    std::string normalizedBase = normalizePathForCompare(base);
    if (normalizedBase.empty()) {
        return false;
    }
    if (normalizedBase.back() != '\\') {
        normalizedBase.push_back('\\');
    }
    if (normalizedPath == normalizedBase.substr(0, normalizedBase.size() - 1)) {
        return true;
    }
    return startsWithNoCase(normalizedPath, normalizedBase);
}

bool registryPathRequiresAdmin(const std::string& path) {
    return startsWithNoCase(path, "HKEY_LOCAL_MACHINE") ||
           startsWithNoCase(path, "HKLM");
}

std::wstring quoteArgument(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    bool needsQuotes = arg.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needsQuotes) {
        return arg;
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            backslashes++;
            continue;
        }
        if (ch == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
            continue;
        }
        if (backslashes > 0) {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
        }
        quoted.push_back(ch);
    }
    if (backslashes > 0) {
        quoted.append(backslashes * 2, L'\\');
    }
    quoted.push_back(L'"');
    return quoted;
}

std::wstring buildRelaunchArguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc <= 1) {
        if (argv) {
            LocalFree(argv);
        }
        return std::wstring();
    }
    std::wstring args;
    for (int i = 1; i < argc; ++i) {
        if (!args.empty()) {
            args += L" ";
        }
        args += quoteArgument(argv[i]);
    }
    LocalFree(argv);
    return args;
}

std::string wstringToUtf8(const std::wstring& value) {
    return WideToUtf8(value);
}

} // namespace

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

bool ensureFileWithSize(const std::filesystem::path& path, uint64_t size,
                        uint64_t sparseThresholdBytes) {
#ifdef _WIN32
    std::filesystem::path openPath = toLongPath(path);
    HANDLE handle = CreateFileW(openPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesReturned = 0;
    if (size >= sparseThresholdBytes) {
        (void)DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
    }

    if (size > 0) {
        LARGE_INTEGER newSize;
        newSize.QuadPart = static_cast<LONGLONG>(size);
        if (!SetFilePointerEx(handle, newSize, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(handle)) {
            CloseHandle(handle);
            return false;
        }
    }

    CloseHandle(handle);
    return true;
#else
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
#endif
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
    return Utf8ToWide(text);
}

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
    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0) {
        return "";
    }
    return WideToUtf8(std::wstring(buffer, len));
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
    std::filesystem::path path = PathFromUtf8(expanded);
    path /= "install.manifest.json";
    return Utf8FromPath(path);
}

std::string getLocalManifestPath(const std::string& exePath) {
    if (exePath.empty()) {
        return "";
    }
    std::filesystem::path path = PathFromUtf8(exePath);
    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return "";
    }
    parent /= "install.manifest.json";
    return Utf8FromPath(parent);
}

bool createUninstallStub(const std::string& sourcePath, const std::string& targetPath) {
    struct DataLocator {
        uint32_t magic;
        uint64_t metadataOffset;
        uint64_t metadataSize;
        uint64_t dataOffset;
        uint64_t dataSize;
    };
    
    std::ifstream in(toLongPath(PathFromUtf8(sourcePath)), std::ios::binary);
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
    
    std::ofstream out(toLongPath(PathFromUtf8(targetPath)), std::ios::binary | std::ios::trunc);
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

bool isRunningAsAdmin() {
#ifdef _WIN32
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
#else
    return false;
#endif
}

bool requiresAdminForInstall(const std::string& installPath,
                             const ExtendedInstallationMetadata& metadata,
                             InstallerPathResolver& resolver) {
#ifdef _WIN32
    std::string expandedInstallPath = resolver.expandEnvironmentVariables(installPath);
    std::string programFiles = resolver.expandEnvironmentVariables("%ProgramFiles%");
    std::string programFilesX86 = resolver.expandEnvironmentVariables("%ProgramFiles(x86)%");

    if (isPathUnder(expandedInstallPath, programFiles) ||
        isPathUnder(expandedInstallPath, programFilesX86)) {
        return true;
    }

    if (metadata.installState.mode == InstallStateMode::REGISTRY ||
        metadata.installState.mode == InstallStateMode::BOTH) {
        if (registryPathRequiresAdmin(metadata.installState.registryPath)) {
            return true;
        }
    }

    for (const auto& entry : metadata.registry) {
        if (registryPathRequiresAdmin(entry.path)) {
            return true;
        }
    }

    return false;
#else
    (void)installPath;
    (void)metadata;
    (void)resolver;
    return false;
#endif
}

bool relaunchSelfAsAdmin() {
#ifdef _WIN32
    wchar_t envValue[8];
    DWORD envSize = GetEnvironmentVariableW(L"MTINSTALLER_ELEVATED", envValue, 8);
    if (envSize > 0) {
        return false;
    }
    SetEnvironmentVariableW(L"MTINSTALLER_ELEVATED", L"1");

    std::wstring exePath = toWideUtf8(getCurrentExecutablePath());
    if (exePath.empty()) {
        return false;
    }

    std::wstring args = buildRelaunchArguments();
    HINSTANCE result = ShellExecuteW(nullptr, L"runas", exePath.c_str(),
                                     args.empty() ? nullptr : args.c_str(),
                                     nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
#else
    return false;
#endif
}

uint64_t getAvailableDiskSpaceBytes(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) {
        return 0;
    }
    std::filesystem::path candidate = PathFromUtf8(path);
    std::error_code ec;
    std::filesystem::path probe = candidate;
    while (!probe.empty() && !std::filesystem::exists(probe, ec)) {
        probe = probe.parent_path();
    }
    if (probe.empty()) {
        probe = candidate.root_path();
    }
    if (probe.empty()) {
        probe = candidate;
    }
    std::wstring widePath = probe.wstring();
    ULARGE_INTEGER freeBytes = {};
    if (!GetDiskFreeSpaceExW(widePath.c_str(), &freeBytes, nullptr, nullptr)) {
        return 0;
    }
    return static_cast<uint64_t>(freeBytes.QuadPart);
#else
    if (path.empty()) {
        return 0;
    }
    std::error_code ec;
    auto info = std::filesystem::space(PathFromUtf8(path), ec);
    if (ec) {
        return 0;
    }
    return static_cast<uint64_t>(info.available);
#endif
}

bool checkDiskSpaceForInstall(const std::string& path, uint64_t requiredBytes,
                              uint64_t& availableBytes) {
    availableBytes = getAvailableDiskSpaceBytes(path);
    return availableBytes >= requiredBytes;
}

bool checkMinimumWindowsVersion(uint16_t minMajor, uint16_t minMinor, uint32_t minBuild,
                                uint16_t& currentMajor, uint16_t& currentMinor, uint32_t& currentBuild) {
#ifdef _WIN32
    currentMajor = 0;
    currentMinor = 0;
    currentBuild = 0;
    if (minMajor == 0 && minMinor == 0 && minBuild == 0) {
        return true;
    }

    using RtlGetVersionPtr = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return true;
    }
    auto rtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion) {
        return true;
    }
    RTL_OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0) {
        return true;
    }
    currentMajor = static_cast<uint16_t>(info.dwMajorVersion);
    currentMinor = static_cast<uint16_t>(info.dwMinorVersion);
    currentBuild = static_cast<uint32_t>(info.dwBuildNumber);

    if (currentMajor < minMajor) {
        return false;
    }
    if (currentMajor == minMajor && currentMinor < minMinor) {
        return false;
    }
    if (currentMajor == minMajor && currentMinor == minMinor &&
        currentBuild < minBuild) {
        return false;
    }
    return true;
#else
    (void)minMajor;
    (void)minMinor;
    (void)minBuild;
    currentMajor = 0;
    currentMinor = 0;
    currentBuild = 0;
    return true;
#endif
}


bool isProcessRunningByName(const std::string& exeName) {
#ifdef _WIN32
    if (exeName.empty()) {
        return false;
    }
    std::string target = exeName;
    std::transform(target.begin(), target.end(), target.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    DWORD currentPid = GetCurrentProcessId();

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == currentPid) {
                continue;
            }
            std::wstring exeWide(entry.szExeFile);
            std::string exe = wstringToUtf8(exeWide);
            std::transform(exe.begin(), exe.end(), exe.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (exe == target) {
                CloseHandle(snapshot);
                return true;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return false;
#else
    (void)exeName;
    return false;
#endif
}

bool terminateProcessByName(const std::string& exeName) {
#ifdef _WIN32
    if (exeName.empty()) {
        return false;
    }
    std::string target = exeName;
    std::transform(target.begin(), target.end(), target.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    DWORD currentPid = GetCurrentProcessId();
    bool terminatedAny = false;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == currentPid) {
                continue;
            }
            std::wstring exeWide(entry.szExeFile);
            std::string exe = wstringToUtf8(exeWide);
            std::transform(exe.begin(), exe.end(), exe.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (exe != target) {
                continue;
            }

            HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (!process) {
                continue;
            }
            if (TerminateProcess(process, 1)) {
                WaitForSingleObject(process, 5000);
                terminatedAny = true;
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return terminatedAny;
#else
    (void)exeName;
    return false;
#endif
}

std::string normalizeProcessName(const std::string& name) {
    return normalizeProcessNameInternal(name);
}

std::vector<std::string> buildKillProcessList(const std::string& appName,
                                              const std::vector<std::string>& extraProcesses) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> result;
    result.reserve(extraProcesses.size() + 1);

    auto addName = [&](const std::string& raw) {
        std::string normalized = normalizeProcessNameInternal(raw);
        if (normalized.empty()) {
            return;
        }
        if (seen.insert(normalized).second) {
            result.push_back(normalized);
        }
    };

    if (!appName.empty()) {
        addName(appName);
    }
    for (const auto& name : extraProcesses) {
        addName(name);
    }

    return result;
}

std::vector<std::string> getRunningProcessesByName(const std::vector<std::string>& exeNames) {
    std::vector<std::string> running;
    running.reserve(exeNames.size());
    for (const auto& name : exeNames) {
        if (!name.empty() && isProcessRunningByName(name)) {
            running.push_back(name);
        }
    }
    return running;
}

bool terminateProcessesByName(const std::vector<std::string>& exeNames) {
    bool any = false;
    for (const auto& name : exeNames) {
        if (!name.empty()) {
            any = terminateProcessByName(name) || any;
        }
    }
    return any;
}
} // namespace MultiThreadedInstaller
