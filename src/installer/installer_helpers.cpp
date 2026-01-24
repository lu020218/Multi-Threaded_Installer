#include "installer/installer_helpers.h"
#include "installer/file_system_operator.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <streambuf>
#include <memory>
#include <iostream>

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
#ifdef _WIN32
    if (value.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
#else
    (void)value;
    return {};
#endif
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
    std::filesystem::path candidate(path);
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
    std::wstring widePath = toWideUtf8(probe.string());
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
    auto info = std::filesystem::space(std::filesystem::path(path), ec);
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

namespace {

class TimestampedBuffer : public std::streambuf {
public:
    explicit TimestampedBuffer(FILE* fileHandle)
        : file(fileHandle), atLineStart(true) {}

protected:
    int overflow(int ch) override {
        if (ch == EOF || !file) {
            return ch;
        }
        if (atLineStart) {
            writeTimestamp();
            atLineStart = false;
        }
        if (ch == '\n') {
            atLineStart = true;
        }
        fputc(ch, file);
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize count) override {
        if (!file || !s || count <= 0) {
            return 0;
        }
        std::streamsize written = 0;
        for (std::streamsize i = 0; i < count; ++i) {
            if (atLineStart) {
                writeTimestamp();
                atLineStart = false;
            }
            char ch = s[i];
            fputc(ch, file);
            if (ch == '\n') {
                atLineStart = true;
            }
            ++written;
        }
        return written;
    }

private:
    void writeTimestamp() {
        if (!file) {
            return;
        }
#ifdef _WIN32
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(file, "[%04d-%02d-%02d %02d:%02d:%02d] ",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
#else
        std::time_t now = std::time(nullptr);
        std::tm localTime{};
        localtime_s(&localTime, &now);
        fprintf(file, "[%04d-%02d-%02d %02d:%02d:%02d] ",
                localTime.tm_year + 1900,
                localTime.tm_mon + 1,
                localTime.tm_mday,
                localTime.tm_hour,
                localTime.tm_min,
                localTime.tm_sec);
#endif
    }

    FILE* file;
    bool atLineStart;
};

std::unique_ptr<TimestampedBuffer> g_logBuffer;
FILE* g_logFile = nullptr;

} // namespace

void initializeInstallerLogging() {
#ifdef _WIN32
    if (g_logBuffer) {
        return;
    }
    std::filesystem::path logPath;
    try {
        char appNameBuf[256] = {};
        DWORD len = GetEnvironmentVariableA("MTINSTALLER_APPNAME", appNameBuf, sizeof(appNameBuf));
        std::string name = (len > 0 && len < sizeof(appNameBuf)) ? appNameBuf : "Installer";
        std::string sanitized = name;
        for (char& c : sanitized) {
            if (c == '\\' || c == '/' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
        }
        logPath = std::filesystem::temp_directory_path() /
                  ("MTInstaller_" + sanitized + ".log");
    } catch (...) {
        logPath = "MTInstaller_Installer.log";
    }
    FILE* fp = nullptr;
    fopen_s(&fp, logPath.string().c_str(), "w");
    if (!fp) {
        return;
    }
    g_logFile = fp;
    g_logBuffer = std::make_unique<TimestampedBuffer>(g_logFile);
    std::cout.rdbuf(g_logBuffer.get());
    std::cerr.rdbuf(g_logBuffer.get());
    std::cout << "Installer log started. Log: " << logPath.string() << std::endl;
#else
    std::cout << "Installer log started." << std::endl;
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

} // namespace MultiThreadedInstaller
