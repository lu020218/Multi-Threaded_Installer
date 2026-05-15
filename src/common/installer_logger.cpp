#include "common/installer_logger.h"

#include "common/utf8_utils.h"
#ifndef SPDLOG_LEVEL_NAMES
#define SPDLOG_LEVEL_NAMES { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF" }
#endif
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <ctime>
#include <exception>
#include <cstdlib>
#include <iostream>
#include <atomic>
#include <mutex>
#include <memory>
#include <set>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <DbgHelp.h>
#endif

namespace MultiThreadedInstaller {

namespace {

std::mutex g_loggerMutex;
std::shared_ptr<spdlog::logger> g_logger;
std::string g_logPath;
bool g_crashHandlersRegistered = false;
std::atomic_flag g_crashHandling = ATOMIC_FLAG_INIT;
std::wstring g_crashDumpDirWide;
std::wstring g_crashLogPathWide;

constexpr size_t kMaxInstallerLogFiles = 5;
constexpr int kInstallerLogRetentionDays = 3;

spdlog::level::level_enum toSpdlogLevel(InstallerLogLevel level) {
    switch (level) {
        case InstallerLogLevel::Info:
            return spdlog::level::info;
        case InstallerLogLevel::Warning:
            return spdlog::level::warn;
        case InstallerLogLevel::Error:
            return spdlog::level::err;
        case InstallerLogLevel::Debug:
            return spdlog::level::debug;
        default:
            return spdlog::level::info;
    }
}

spdlog::level::level_enum parseConfiguredLogLevel() {
#ifdef _WIN32
    wchar_t levelBuf[64] = {};
    DWORD len = GetEnvironmentVariableW(L"MTINSTALLER_LOG_LEVEL",
                                        levelBuf,
                                        static_cast<DWORD>(sizeof(levelBuf) / sizeof(levelBuf[0])));
    if (len == 0 || len >= (sizeof(levelBuf) / sizeof(levelBuf[0]))) {
        return spdlog::level::info;
    }
    std::wstring level(levelBuf, len);
    std::transform(level.begin(), level.end(), level.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    if (level == L"debug") {
        return spdlog::level::debug;
    }
    if (level == L"warn" || level == L"warning") {
        return spdlog::level::warn;
    }
    if (level == L"error" || level == L"err") {
        return spdlog::level::err;
    }
#endif
    return spdlog::level::info;
}

#ifdef _WIN32
bool isManagedInstallerLogFile(const std::filesystem::path& path) {
    const std::wstring name = path.filename().wstring();
    if (name.rfind(L"MTInstaller_", 0) != 0) {
        return false;
    }
    if (path.extension() != L".log") {
        return false;
    }
    return name.find(L".crash.") == std::wstring::npos;
}

bool sameFilesystemPath(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec)) {
        return true;
    }
    const auto normalizedA = std::filesystem::absolute(a, ec).lexically_normal().wstring();
    ec.clear();
    const auto normalizedB = std::filesystem::absolute(b, ec).lexically_normal().wstring();
    return _wcsicmp(normalizedA.c_str(), normalizedB.c_str()) == 0;
}

size_t pruneOldInstallerLogs(const std::filesystem::path& logDir,
                             const std::filesystem::path& currentLogPath) {
    if (logDir.empty()) {
        return 0;
    }
    std::error_code ec;
    if (!std::filesystem::exists(logDir, ec) || !std::filesystem::is_directory(logDir, ec)) {
        return 0;
    }

    struct LogEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type writeTime;
    };

    std::vector<LogEntry> logs;
    for (std::filesystem::directory_iterator it(logDir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto path = it->path();
        if (!isManagedInstallerLogFile(path) || sameFilesystemPath(path, currentLogPath)) {
            continue;
        }
        std::error_code timeEc;
        const auto writeTime = std::filesystem::last_write_time(path, timeEc);
        if (timeEc) {
            continue;
        }
        logs.push_back({path, writeTime});
    }

    const auto now = std::filesystem::file_time_type::clock::now();
    const auto retentionCutoff = now - std::chrono::hours(24 * kInstallerLogRetentionDays);
    std::vector<std::filesystem::path> toDelete;
    std::set<std::wstring> selected;
    auto selectForDelete = [&](const std::filesystem::path& path) {
        const std::wstring key = path.lexically_normal().wstring();
        if (selected.insert(key).second) {
            toDelete.push_back(path);
        }
    };

    for (const auto& entry : logs) {
        if (entry.writeTime < retentionCutoff) {
            selectForDelete(entry.path);
        }
    }

    std::sort(logs.begin(), logs.end(), [](const LogEntry& a, const LogEntry& b) {
        return a.writeTime > b.writeTime;
    });
    if (logs.size() + 1 > kMaxInstallerLogFiles) {
        const size_t keepOldCount = kMaxInstallerLogFiles - 1;
        for (size_t i = keepOldCount; i < logs.size(); ++i) {
            selectForDelete(logs[i].path);
        }
    }

    size_t deleted = 0;
    for (const auto& path : toDelete) {
        std::error_code removeEc;
        if (std::filesystem::remove(path, removeEc) && !removeEc) {
            ++deleted;
        }
    }
    return deleted;
}

std::string getCrashLogPath() {
    if (!g_logPath.empty()) {
        return g_logPath + ".crash.log";
    }
    return "MTInstaller_crash.log";
}

std::filesystem::path getCrashDumpDirectory() {
    std::filesystem::path baseDir;
    if (!g_logPath.empty()) {
        baseDir = PathFromUtf8(g_logPath).parent_path();
    }
    if (baseDir.empty()) {
        wchar_t localAppData[MAX_PATH] = {};
        DWORD localLen = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
        if (localLen > 0 && localLen < MAX_PATH) {
            baseDir = std::filesystem::path(localAppData) / L"MTInstaller";
        }
    }
    if (baseDir.empty()) {
        baseDir = std::filesystem::temp_directory_path() / L"MTInstaller";
    }
    std::filesystem::path dumpDir = baseDir / L"CrashDumps";
    std::error_code ec;
    std::filesystem::create_directories(dumpDir, ec);
    return dumpDir;
}

std::wstring buildCrashDumpPathWide(DWORD exceptionCode) {
    std::wstring dumpDir = g_crashDumpDirWide;
    if (dumpDir.empty()) {
        std::filesystem::path fallback = getCrashDumpDirectory();
        dumpDir = fallback.wstring();
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t fileName[256] = {};
    swprintf_s(fileName,
               L"MTInstaller_%04d%02d%02d_%02d%02d%02d_%lu_%lu_%08lX.dmp",
               st.wYear,
               st.wMonth,
               st.wDay,
               st.wHour,
               st.wMinute,
               st.wSecond,
               static_cast<unsigned long>(GetCurrentProcessId()),
               static_cast<unsigned long>(GetCurrentThreadId()),
               static_cast<unsigned long>(exceptionCode));
    if (!dumpDir.empty() && dumpDir.back() != L'\\' && dumpDir.back() != L'/') {
        dumpDir.push_back(L'\\');
    }
    dumpDir += fileName;
    return dumpDir;
}

bool writeMiniDump(EXCEPTION_POINTERS* info,
                   DWORD exceptionCode,
                   std::string& dumpPath,
                   DWORD& lastError) {
    std::wstring dumpPathW = buildCrashDumpPathWide(exceptionCode);
    dumpPath = WideToUtf8(dumpPathW);

    HANDLE dumpFile = CreateFileW(dumpPathW.c_str(),
                                  GENERIC_WRITE,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    if (dumpFile == INVALID_HANDLE_VALUE) {
        lastError = GetLastError();
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
    MINIDUMP_EXCEPTION_INFORMATION* exceptionInfoPtr = nullptr;
    if (info) {
        exceptionInfo.ThreadId = GetCurrentThreadId();
        exceptionInfo.ExceptionPointers = info;
        exceptionInfo.ClientPointers = FALSE;
        exceptionInfoPtr = &exceptionInfo;
    }

    MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);

    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(),
                                GetCurrentProcessId(),
                                dumpFile,
                                dumpType,
                                exceptionInfoPtr,
                                nullptr,
                                nullptr);
    if (!ok) {
        ok = MiniDumpWriteDump(GetCurrentProcess(),
                               GetCurrentProcessId(),
                               dumpFile,
                               MiniDumpNormal,
                               exceptionInfoPtr,
                               nullptr,
                               nullptr);
    }
    lastError = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(dumpFile);
    return ok == TRUE;
}

void writeCrashLogLine(const char* reason,
                       DWORD code,
                       const std::string& dumpPath,
                       DWORD dumpError,
                       bool dumpWritten) {
    std::wstring pathW = g_crashLogPathWide;
    if (pathW.empty()) {
        const std::string path = getCrashLogPath();
        pathW = Utf8ToWide(path);
    }
    if (pathW.empty()) {
        return;
    }
    HANDLE file = CreateFileW(pathW.c_str(),
                              FILE_APPEND_DATA,
                              FILE_SHARE_READ,
                              nullptr,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buffer[1024] = {};
    const char* dumpState = dumpWritten ? "written" : "failed";
    const char* dumpPathRaw = dumpPath.empty() ? "" : dumpPath.c_str();
    int count = std::snprintf(buffer, sizeof(buffer),
                              "[%04d-%02d-%02d %02d:%02d:%02d] Crash: %s (code=0x%08lx) dump=%s path=%s error=%lu\r\n",
                              st.wYear, st.wMonth, st.wDay,
                              st.wHour, st.wMinute, st.wSecond,
                              reason ? reason : "unknown",
                              static_cast<unsigned long>(code),
                              dumpState,
                              dumpPathRaw,
                              static_cast<unsigned long>(dumpError));
    if (count > 0) {
        DWORD written = 0;
        WriteFile(file, buffer, static_cast<DWORD>(count), &written, nullptr);
    }
    CloseHandle(file);
}

LONG WINAPI InstallerUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
    if (g_crashHandling.test_and_set()) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    std::string dumpPath;
    DWORD dumpError = ERROR_SUCCESS;
    bool dumpWritten = writeMiniDump(info, code, dumpPath, dumpError);
    writeCrashLogLine("Unhandled exception", code, dumpPath, dumpError, dumpWritten);
    flushInstallerLogging();
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallerTerminateHandler() {
    if (!g_crashHandling.test_and_set()) {
        std::string dumpPath;
        DWORD dumpError = ERROR_SUCCESS;
        bool dumpWritten = writeMiniDump(nullptr, 0, dumpPath, dumpError);
        writeCrashLogLine("std::terminate", 0, dumpPath, dumpError, dumpWritten);
    }
    flushInstallerLogging();
    std::abort();
}
#endif

} // namespace

void initializeInstallerLogging() {
#ifdef _WIN32
    {
        std::lock_guard<std::mutex> lock(g_loggerMutex);
        if (g_logger) {
            return;
        }
    }
    std::filesystem::path logPath;
    bool createdNewLogPath = false;
    try {
        wchar_t explicitLogPath[1024] = {};
        DWORD explicitLen = GetEnvironmentVariableW(L"MTINSTALLER_LOG_PATH",
                                                    explicitLogPath,
                                                    static_cast<DWORD>(sizeof(explicitLogPath) /
                                                                       sizeof(explicitLogPath[0])));
        if (explicitLen > 0 && explicitLen < (sizeof(explicitLogPath) / sizeof(explicitLogPath[0]))) {
            logPath = explicitLogPath;
            createdNewLogPath = false;
        }
    } catch (...) {
        logPath.clear();
    }
    try {
        if (!logPath.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(logPath.parent_path(), ec);
        } else {
        wchar_t appNameBuf[256] = {};
        DWORD appNameCap = static_cast<DWORD>(sizeof(appNameBuf) / sizeof(appNameBuf[0]));
        DWORD len = GetEnvironmentVariableW(L"MTINSTALLER_APPNAME", appNameBuf, appNameCap);
        std::wstring name = (len > 0 && len < appNameCap) ? appNameBuf : L"Installer";
        std::wstring sanitized = name;
        for (wchar_t& c : sanitized) {
            if (c == L'\\' || c == L'/' || c == L':' || c == L'*' ||
                c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') {
                c = L'_';
            }
        }
        wchar_t localAppData[MAX_PATH] = {};
        DWORD localLen = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
        if (localLen > 0 && localLen < MAX_PATH) {
            std::filesystem::path logDir = std::filesystem::path(localAppData) / L"MTInstaller";
            std::error_code ec;
            std::filesystem::create_directories(logDir, ec);
            SYSTEMTIME st{};
            GetLocalTime(&st);
            wchar_t fileName[256] = {};
            swprintf_s(fileName,
                       L"MTInstaller_%s_%04d%02d%02d_%02d%02d%02d_%lu.log",
                       sanitized.c_str(),
                       st.wYear,
                       st.wMonth,
                       st.wDay,
                       st.wHour,
                       st.wMinute,
                       st.wSecond,
                       static_cast<unsigned long>(GetCurrentProcessId()));
            logPath = logDir / fileName;
            createdNewLogPath = true;
        } else {
            logPath = std::filesystem::temp_directory_path() /
                       (L"MTInstaller_" + sanitized + L".log");
            createdNewLogPath = true;
        }
        }
    } catch (...) {
        logPath = std::filesystem::path(L"MTInstaller_Installer.log");
        createdNewLogPath = true;
    }
    std::shared_ptr<spdlog::logger> logger;
    try {
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.wstring(), false);
        logger = std::make_shared<spdlog::logger>("installer", std::move(sink));
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [tid=%t] %v");
        logger->set_level(parseConfiguredLogLevel());
        logger->flush_on(spdlog::level::warn);
    } catch (...) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_loggerMutex);
        if (g_logger) {
            return;
        }
        g_logger = logger;
        g_logPath = Utf8FromPath(logPath);
        g_crashLogPathWide = Utf8ToWide(g_logPath + ".crash.log");
        g_crashDumpDirWide = getCrashDumpDirectory().wstring();
    }

    SetEnvironmentVariableW(L"MTINSTALLER_LOG_PATH", logPath.wstring().c_str());

    logger->info("Installer log started. Log: {}", g_logPath);
    logger->info("Crash dumps enabled. Directory: {}", Utf8FromPath(getCrashDumpDirectory()));
    if (createdNewLogPath) {
        const size_t deleted = pruneOldInstallerLogs(logPath.parent_path(), logPath);
        if (deleted > 0) {
            logger->info("Pruned old installer logs: deleted={}", deleted);
        }
    }

    if (!g_crashHandlersRegistered) {
        SetUnhandledExceptionFilter(InstallerUnhandledExceptionFilter);
        std::set_terminate(InstallerTerminateHandler);
        g_crashHandlersRegistered = true;
    }
#else
    writeInstallerLog(InstallerLogLevel::Info, "Installer log started.");
#endif
}

void flushInstallerLogging() {
#ifdef _WIN32
    std::shared_ptr<spdlog::logger> logger;
    {
        std::lock_guard<std::mutex> lock(g_loggerMutex);
        logger = g_logger;
    }
    if (logger) {
        logger->flush();
    }
#endif
}

std::string getInstallerLogPath() {
    return g_logPath;
}

void writeInstallerLog(InstallerLogLevel level, const std::string& message) {
#ifdef _WIN32
    if (message.empty()) {
        return;
    }
    std::shared_ptr<spdlog::logger> logger;
    {
        std::lock_guard<std::mutex> lock(g_loggerMutex);
        logger = g_logger;
    }
    if (logger) {
        logger->log(toSpdlogLevel(level), "{}", message);
    }
#else
    (void)level;
    (void)message;
#endif
}

void logInstallerInfo(const std::string& message) {
    writeInstallerLog(InstallerLogLevel::Info, message);
}

void logInstallerWarning(const std::string& message) {
    writeInstallerLog(InstallerLogLevel::Warning, message);
}

void logInstallerError(const std::string& message) {
    writeInstallerLog(InstallerLogLevel::Error, message);
}

void logInstallerDebug(const std::string& message) {
    writeInstallerLog(InstallerLogLevel::Debug, message);
}

} // namespace MultiThreadedInstaller
