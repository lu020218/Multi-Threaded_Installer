#include "common/installer_logger.h"

#include "common/utf8_utils.h"
#include <filesystem>
#include <cstdio>
#include <ctime>
#include <exception>
#include <cstdlib>
#include <streambuf>
#include <memory>
#include <iostream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

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
std::string g_logPath;
bool g_crashHandlersRegistered = false;

#ifdef _WIN32
std::string getCrashLogPath() {
    if (!g_logPath.empty()) {
        return g_logPath + ".crash.log";
    }
    return "MTInstaller_crash.log";
}

void writeCrashLogLine(const char* reason, DWORD code) {
    std::string path = getCrashLogPath();
    std::wstring pathW = Utf8ToWide(path);
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
    char buffer[512] = {};
    int count = std::snprintf(buffer, sizeof(buffer),
                              "[%04d-%02d-%02d %02d:%02d:%02d] Crash: %s (code=0x%08lx)\r\n",
                              st.wYear, st.wMonth, st.wDay,
                              st.wHour, st.wMinute, st.wSecond,
                              reason ? reason : "unknown", static_cast<unsigned long>(code));
    if (count > 0) {
        DWORD written = 0;
        WriteFile(file, buffer, static_cast<DWORD>(count), &written, nullptr);
    }
    CloseHandle(file);
}

LONG WINAPI InstallerUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    writeCrashLogLine("Unhandled exception", code);
    if (g_logFile) {
        fflush(g_logFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallerTerminateHandler() {
    writeCrashLogLine("std::terminate", 0);
    if (g_logFile) {
        fflush(g_logFile);
    }
    std::abort();
}
#endif

} // namespace

void initializeInstallerLogging() {
#ifdef _WIN32
    if (g_logBuffer) {
        return;
    }
    std::filesystem::path logPath;
    try {
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
            logPath = logDir / (L"MTInstaller_" + sanitized + L".log");
        } else {
            logPath = std::filesystem::temp_directory_path() /
                      (L"MTInstaller_" + sanitized + L".log");
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
    g_logBuffer = std::make_unique<TimestampedBuffer>(g_logFile);
    std::cout.rdbuf(g_logBuffer.get());
    std::cerr.rdbuf(g_logBuffer.get());
    std::cout << "Installer log started. Log: " << g_logPath << std::endl;

    if (!g_crashHandlersRegistered) {
        SetUnhandledExceptionFilter(InstallerUnhandledExceptionFilter);
        std::set_terminate(InstallerTerminateHandler);
        g_crashHandlersRegistered = true;
    }
#else
    std::cout << "Installer log started." << std::endl;
#endif
}

void flushInstallerLogging() {
#ifdef _WIN32
    if (g_logFile) {
        fflush(g_logFile);
    }
#endif
}

std::string getInstallerLogPath() {
    return g_logPath;
}

} // namespace MultiThreadedInstaller
