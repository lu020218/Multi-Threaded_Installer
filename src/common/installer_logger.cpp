#include "common/installer_logger.h"

#include "common/utf8_utils.h"
#include <filesystem>
#include <cstdio>
#include <ctime>
#include <exception>
#include <cstdlib>
#include <iostream>
#include <atomic>
#include <mutex>

#ifdef _WIN32
#include <Windows.h>
#include <DbgHelp.h>
#endif

namespace MultiThreadedInstaller {

namespace {

std::mutex g_logWriteMutex;
FILE* g_logFile = nullptr;
std::string g_logPath;
bool g_crashHandlersRegistered = false;
std::atomic_flag g_crashHandling = ATOMIC_FLAG_INIT;
std::wstring g_crashDumpDirWide;
std::wstring g_crashLogPathWide;

const char* levelToText(InstallerLogLevel level) {
    switch (level) {
        case InstallerLogLevel::Info:
            return "INFO";
        case InstallerLogLevel::Warning:
            return "WARN";
        case InstallerLogLevel::Error:
            return "ERROR";
        case InstallerLogLevel::Debug:
            return "DEBUG";
        default:
            return "INFO";
    }
}

void writeTimestampPrefix(FILE* file) {
    if (!file) {
        return;
    }
#ifdef _WIN32
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::fprintf(file,
                 "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
                 st.wYear,
                 st.wMonth,
                 st.wDay,
                 st.wHour,
                 st.wMinute,
                 st.wSecond,
                 st.wMilliseconds);
#else
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    localtime_s(&localTime, &now);
    std::fprintf(file,
                 "[%04d-%02d-%02d %02d:%02d:%02d] ",
                 localTime.tm_year + 1900,
                 localTime.tm_mon + 1,
                 localTime.tm_mday,
                 localTime.tm_hour,
                 localTime.tm_min,
                 localTime.tm_sec);
#endif
}

#ifdef _WIN32
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
    if (g_logFile) {
        std::lock_guard<std::mutex> lock(g_logWriteMutex);
        fflush(g_logFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallerTerminateHandler() {
    if (!g_crashHandling.test_and_set()) {
        std::string dumpPath;
        DWORD dumpError = ERROR_SUCCESS;
        bool dumpWritten = writeMiniDump(nullptr, 0, dumpPath, dumpError);
        writeCrashLogLine("std::terminate", 0, dumpPath, dumpError, dumpWritten);
    }
    if (g_logFile) {
        std::lock_guard<std::mutex> lock(g_logWriteMutex);
        fflush(g_logFile);
    }
    std::abort();
}
#endif

} // namespace

void initializeInstallerLogging() {
#ifdef _WIN32
    if (g_logFile) {
        return;
    }
    std::filesystem::path logPath;
    try {
        wchar_t explicitLogPath[1024] = {};
        DWORD explicitLen = GetEnvironmentVariableW(L"MTINSTALLER_LOG_PATH",
                                                    explicitLogPath,
                                                    static_cast<DWORD>(sizeof(explicitLogPath) /
                                                                       sizeof(explicitLogPath[0])));
        if (explicitLen > 0 && explicitLen < (sizeof(explicitLogPath) / sizeof(explicitLogPath[0]))) {
            logPath = explicitLogPath;
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
        } else {
            logPath = std::filesystem::temp_directory_path() /
                       (L"MTInstaller_" + sanitized + L".log");
        }
        }
    } catch (...) {
        logPath = std::filesystem::path(L"MTInstaller_Installer.log");
    }
    FILE* fp = nullptr;
    _wfopen_s(&fp, logPath.c_str(), L"a");
    if (!fp) {
        return;
    }
    setvbuf(fp, nullptr, _IOLBF, 4096);
    g_logFile = fp;
    g_logPath = Utf8FromPath(logPath);
    g_crashLogPathWide = Utf8ToWide(g_logPath + ".crash.log");
    g_crashDumpDirWide = getCrashDumpDirectory().wstring();
    writeInstallerLog(InstallerLogLevel::Info, "Installer log started. Log: " + g_logPath);
    writeInstallerLog(InstallerLogLevel::Info,
                      "Crash dumps enabled. Directory: " + Utf8FromPath(getCrashDumpDirectory()));

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
    if (g_logFile) {
        std::lock_guard<std::mutex> lock(g_logWriteMutex);
        fflush(g_logFile);
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
    std::lock_guard<std::mutex> lock(g_logWriteMutex);
    if (g_logFile) {
        writeTimestampPrefix(g_logFile);
        std::fprintf(g_logFile, "[%s] %s\n", levelToText(level), message.c_str());
        std::fflush(g_logFile);
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
