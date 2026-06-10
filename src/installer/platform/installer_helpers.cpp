#include "installer/platform/installer_helpers.h"
#include "common/engine_defaults.h"
#include "installer/platform/file_system_operator.h"
#include "installer/state/registry_utils.h"
#include "installer/platform/shortcut_startup_utils.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
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


} // namespace

std::string appendPathLeafIfMissing(const std::string& basePath, const std::string& expectedLeaf) {
    const std::string trimmedLeaf = trimAscii(expectedLeaf);
    if (basePath.empty() || trimmedLeaf.empty()) {
        return basePath;
    }

    std::filesystem::path normalized = PathFromUtf8(basePath).lexically_normal();
    std::filesystem::path leafProbe = normalized;
    while (!leafProbe.empty() && leafProbe.filename().empty() && leafProbe.has_parent_path()) {
        leafProbe = leafProbe.parent_path();
    }

    std::string currentLeaf = Utf8FromPath(leafProbe.filename());
    std::transform(currentLeaf.begin(), currentLeaf.end(), currentLeaf.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string loweredExpected = trimmedLeaf;
    std::transform(loweredExpected.begin(), loweredExpected.end(), loweredExpected.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (currentLeaf == loweredExpected) {
        return Utf8FromPath(normalized);
    }
    return Utf8FromPath(normalized / PathFromUtf8(trimmedLeaf));
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

bool isCancellationText(const std::string& message) {
    std::string lowered = message;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered.find("cancelled") != std::string::npos ||
           lowered.find("canceled") != std::string::npos;
}

bool readFileBytesAt(std::ifstream& file, uint64_t offset, void* out, size_t size) {
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
        return false;
    }
    file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
    return file.good() && file.gcount() == static_cast<std::streamsize>(size);
}

uint64_t resolveLogicalPeEnd(std::ifstream& file, uint64_t fileSize) {
#ifdef _WIN32
    if (fileSize < sizeof(IMAGE_DOS_HEADER)) {
        return fileSize;
    }

    IMAGE_DOS_HEADER dosHeader{};
    if (!readFileBytesAt(file, 0, &dosHeader, sizeof(dosHeader)) ||
        dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        return fileSize;
    }

    uint64_t ntOffset = static_cast<uint64_t>(dosHeader.e_lfanew);
    uint32_t peSignature = 0;
    if (ntOffset + sizeof(peSignature) + sizeof(IMAGE_FILE_HEADER) > fileSize ||
        !readFileBytesAt(file, ntOffset, &peSignature, sizeof(peSignature)) ||
        peSignature != IMAGE_NT_SIGNATURE) {
        return fileSize;
    }

    IMAGE_FILE_HEADER fileHeader{};
    if (!readFileBytesAt(file,
                         ntOffset + sizeof(peSignature),
                         &fileHeader,
                         sizeof(fileHeader))) {
        return fileSize;
    }

    uint64_t optionalOffset = ntOffset + sizeof(peSignature) + sizeof(IMAGE_FILE_HEADER);
    if (optionalOffset + fileHeader.SizeOfOptionalHeader > fileSize ||
        fileHeader.SizeOfOptionalHeader < sizeof(uint16_t)) {
        return fileSize;
    }

    std::vector<uint8_t> optionalHeader(fileHeader.SizeOfOptionalHeader);
    if (!readFileBytesAt(file, optionalOffset, optionalHeader.data(), optionalHeader.size())) {
        return fileSize;
    }

    uint16_t optionalMagic = 0;
    std::memcpy(&optionalMagic, optionalHeader.data(), sizeof(optionalMagic));

    size_t directoryOffset = 0;
    if (optionalMagic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        directoryOffset = offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
    } else if (optionalMagic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        directoryOffset = offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory);
    } else {
        return fileSize;
    }

    size_t securityDirOffset =
        directoryOffset + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(IMAGE_DATA_DIRECTORY);
    if (securityDirOffset + sizeof(IMAGE_DATA_DIRECTORY) > optionalHeader.size()) {
        return fileSize;
    }

    IMAGE_DATA_DIRECTORY securityDir{};
    std::memcpy(&securityDir,
                optionalHeader.data() + securityDirOffset,
                sizeof(securityDir));

    uint64_t certificateOffset = static_cast<uint64_t>(securityDir.VirtualAddress);
    uint64_t certificateSize = static_cast<uint64_t>(securityDir.Size);
    if (certificateOffset == 0 || certificateSize == 0) {
        return fileSize;
    }

    if (certificateOffset >= fileSize ||
        certificateOffset + certificateSize > fileSize) {
        return fileSize;
    }

    if (certificateOffset + certificateSize != fileSize) {
        return fileSize;
    }

    return certificateOffset;
#else
    (void)file;
    return fileSize;
#endif
}

namespace {

bool isValidEmbeddedDataLocator(const EmbeddedDataLocatorRecord& locator,
                                uint64_t trailerEnd) {
    if (locator.magic != Constants::MAGIC_NUMBER) {
        return false;
    }

    const uint64_t locatorSize =
        static_cast<uint64_t>(sizeof(EmbeddedDataLocatorRecord) + sizeof(uint32_t));
    if (trailerEnd < locatorSize) {
        return false;
    }

    const uint64_t locatorOffset = trailerEnd - locatorSize;
    if (locator.metadataOffset >= locatorOffset ||
        locator.metadataOffset + locator.metadataSize > locatorOffset) {
        return false;
    }

    if (locator.dataOffset > locatorOffset ||
        locator.dataOffset + locator.dataSize > locatorOffset) {
        return false;
    }

    if (locator.dataOffset < locator.metadataOffset + locator.metadataSize) {
        return false;
    }

    return true;
}

bool tryReadEmbeddedDataLocatorAt(std::ifstream& file,
                                  uint64_t trailerEnd,
                                  EmbeddedDataLocatorRecord& locator) {
    const uint64_t locatorSize =
        static_cast<uint64_t>(sizeof(EmbeddedDataLocatorRecord) + sizeof(uint32_t));
    if (trailerEnd < locatorSize) {
        return false;
    }

    uint32_t endMagic = 0;
    if (!readFileBytesAt(file,
                         trailerEnd - sizeof(endMagic),
                         &endMagic,
                         sizeof(endMagic)) ||
        endMagic != Constants::MAGIC_NUMBER) {
        return false;
    }

    if (!readFileBytesAt(file,
                         trailerEnd - locatorSize,
                         &locator,
                         sizeof(locator))) {
        return false;
    }

    return isValidEmbeddedDataLocator(locator, trailerEnd);
}

bool scanEmbeddedDataLocatorNearEnd(std::ifstream& file,
                                    uint64_t candidateEnd,
                                    uint64_t& trailerEnd,
                                    EmbeddedDataLocatorRecord& locator) {
    const uint64_t locatorSize =
        static_cast<uint64_t>(sizeof(EmbeddedDataLocatorRecord) + sizeof(uint32_t));
    if (candidateEnd < locatorSize) {
        return false;
    }

    if (tryReadEmbeddedDataLocatorAt(file, candidateEnd, locator)) {
        trailerEnd = candidateEnd;
        return true;
    }

    constexpr uint64_t kTrailerScanWindow = 1024 * 1024;
    const uint64_t scanStart =
        candidateEnd > kTrailerScanWindow ? (candidateEnd - kTrailerScanWindow) : 0;
    const uint64_t scanSize = candidateEnd - scanStart;
    if (scanSize < locatorSize) {
        return false;
    }

    std::vector<uint8_t> tail(scanSize);
    if (!readFileBytesAt(file, scanStart, tail.data(), static_cast<size_t>(scanSize))) {
        return false;
    }

    const uint32_t magic = Constants::MAGIC_NUMBER;
    for (size_t pos = tail.size() - sizeof(uint32_t); ; --pos) {
        uint32_t candidateMagic = 0;
        std::memcpy(&candidateMagic, tail.data() + pos, sizeof(candidateMagic));
        if (candidateMagic == magic) {
            const uint64_t candidateTrailerEnd =
                scanStart + static_cast<uint64_t>(pos) + sizeof(uint32_t);
            if (tryReadEmbeddedDataLocatorAt(file, candidateTrailerEnd, locator)) {
                trailerEnd = candidateTrailerEnd;
                return true;
            }
        }
        if (pos == 0) {
            break;
        }
    }

    return false;
}

} // namespace

bool findEmbeddedDataLocator(std::ifstream& file,
                             uint64_t fileSize,
                             uint64_t& trailerEnd,
                             EmbeddedDataLocatorRecord& locator) {
    trailerEnd = 0;

    const uint64_t logicalEnd = resolveLogicalPeEnd(file, fileSize);
    if (scanEmbeddedDataLocatorNearEnd(file, logicalEnd, trailerEnd, locator)) {
        return true;
    }

    if (logicalEnd != fileSize &&
        scanEmbeddedDataLocatorNearEnd(file, fileSize, trailerEnd, locator)) {
        return true;
    }

    return false;
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
    // 引擎写死要求管理员（需求 §5：requireAdmin 默认 true，统一写 HKLM 与系统卸载入口）。
    (void)installPath;
    (void)metadata;
    (void)resolver;
    return EngineDefaults::kRequireAdmin;
}

bool relaunchSelfAsAdmin() {
#ifdef _WIN32
    wchar_t envValue[8];
    DWORD envSize = GetEnvironmentVariableW(L"MTINSTALLER_ELEVATED", envValue, 8);
    if (envSize > 0) {
        return false;
    }
    SetEnvironmentVariableW(L"MTINSTALLER_ELEVATED", L"1");

    std::wstring exePath = Utf8ToWide(getCurrentExecutablePath());
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

bool relaunchSelfAsAdminWithArguments(const std::vector<std::wstring>& extraArgs) {
#ifdef _WIN32
    wchar_t envValue[8];
    DWORD envSize = GetEnvironmentVariableW(L"MTINSTALLER_ELEVATED", envValue, 8);
    if (envSize > 0) {
        return false;
    }
    SetEnvironmentVariableW(L"MTINSTALLER_ELEVATED", L"1");

    std::wstring exePath = Utf8ToWide(getCurrentExecutablePath());
    if (exePath.empty()) {
        return false;
    }

    std::wstring args = buildRelaunchArguments();
    for (const auto& arg : extraArgs) {
        if (!args.empty()) {
            args += L" ";
        }
        args += quoteArgument(arg);
    }
    HINSTANCE result = ShellExecuteW(nullptr, L"runas", exePath.c_str(),
                                     args.empty() ? nullptr : args.c_str(),
                                     nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
#else
    (void)extraArgs;
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
            std::string exe = WideToUtf8(exeWide);
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

    auto formatWin32Error = [](DWORD errorCode) {
        LPWSTR buffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_IGNORE_INSERTS;
        DWORD len = FormatMessageW(flags,
                                   nullptr,
                                   errorCode,
                                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   reinterpret_cast<LPWSTR>(&buffer),
                                   0,
                                   nullptr);
        std::string message = "code=" + std::to_string(errorCode);
        if (len > 0 && buffer) {
            std::wstring text(buffer, len);
            while (!text.empty() &&
                   (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
                text.pop_back();
            }
            if (!text.empty()) {
                message += " message=" + WideToUtf8(text);
            }
        }
        if (buffer) {
            LocalFree(buffer);
        }
        return message;
    };

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
            std::string exe = WideToUtf8(exeWide);
            std::transform(exe.begin(), exe.end(), exe.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (exe != target) {
                continue;
            }

            HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (!process) {
                const DWORD openError = GetLastError();
                logInstallerWarning("[PROC] Failed to open process for termination name=" + exeName +
                                    " pid=" + std::to_string(entry.th32ProcessID) +
                                    " error=" + formatWin32Error(openError));
                continue;
            }
            if (TerminateProcess(process, 1)) {
                const DWORD waitResult = WaitForSingleObject(process, 5000);
                if (waitResult == WAIT_OBJECT_0) {
                    logInstallerInfo("[PROC] Terminated process name=" + exeName +
                                     " pid=" + std::to_string(entry.th32ProcessID));
                    terminatedAny = true;
                } else if (waitResult == WAIT_TIMEOUT) {
                    logInstallerWarning("[PROC] TerminateProcess returned success but wait timed out name=" +
                                        exeName +
                                        " pid=" + std::to_string(entry.th32ProcessID));
                    terminatedAny = true;
                } else {
                    logInstallerWarning("[PROC] TerminateProcess returned success but wait failed name=" +
                                        exeName +
                                        " pid=" + std::to_string(entry.th32ProcessID) +
                                        " error=" + formatWin32Error(GetLastError()));
                    terminatedAny = true;
                }
            } else {
                const DWORD terminateError = GetLastError();
                logInstallerWarning("[PROC] Failed to terminate process name=" + exeName +
                                    " pid=" + std::to_string(entry.th32ProcessID) +
                                    " error=" + formatWin32Error(terminateError));
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
            logInstallerInfo("[PROC] Attempting to terminate process by name=" + name);
            any = terminateProcessByName(name) || any;
        }
    }
    return any;
}
} // namespace MultiThreadedInstaller
