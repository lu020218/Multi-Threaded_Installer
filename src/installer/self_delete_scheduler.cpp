#include "installer/uninstall_manager.h"

#include "installer/installer_helpers.h"
#include "common/utf8_utils.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <vector>

namespace MultiThreadedInstaller {

bool scheduleSelfDelete() {
#ifdef _WIN32
    std::string exePath = getCurrentExecutablePath();
    if (exePath.empty()) {
        return false;
    }
    std::wstring wide = Utf8ToWide(exePath);
    if (wide.empty()) {
        return false;
    }
    return MoveFileExW(wide.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != 0;
#else
    return false;
#endif
}

bool scheduleSelfDeleteImmediate(const std::vector<std::string>& cleanupRoots,
                                 const std::string& manifestPath) {
#ifdef _WIN32
    std::string exePath = getCurrentExecutablePath();
    if (exePath.empty()) {
        return false;
    }

    std::wstring exePathW = Utf8ToWide(exePath);
    if (exePathW.empty()) {
        return false;
    }

    wchar_t tempPath[MAX_PATH] = {};
    DWORD len = GetTempPathW(MAX_PATH, tempPath);
    if (len == 0 || len >= MAX_PATH) {
        return false;
    }

    wchar_t tempFile[MAX_PATH] = {};
    if (GetTempFileNameW(tempPath, L"un", 0, tempFile) == 0) {
        return false;
    }

    std::wstring helperPath = std::wstring(tempFile) + L".exe";
    DeleteFileW(tempFile);
    if (!CopyFileW(exePathW.c_str(), helperPath.c_str(), FALSE)) {
        return false;
    }

    auto quoteArg = [](const std::wstring& value) {
        std::wstring quoted = L"\"";
        for (wchar_t ch : value) {
            if (ch == L'"') {
                quoted += L'\\';
            }
            quoted += ch;
        }
        quoted += L"\"";
        return quoted;
    };

    std::wstring cmd = quoteArg(helperPath) +
                       L" --cleanup-self --cleanup-parent-pid " +
                       std::to_wstring(GetCurrentProcessId()) +
                       L" --cleanup-exe " + quoteArg(exePathW);
    if (!manifestPath.empty()) {
        std::wstring manifestW = Utf8ToWide(manifestPath);
        if (!manifestW.empty()) {
            cmd += L" --cleanup-manifest " + quoteArg(manifestW);
        }
    }
    for (const auto& root : cleanupRoots) {
        if (root.empty()) {
            continue;
        }
        std::wstring rootW = Utf8ToWide(root);
        if (!rootW.empty()) {
            cmd += L" --cleanup-root " + quoteArg(rootW);
        }
    }

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');
    BOOL ok = CreateProcessW(helperPath.c_str(),
                             cmdLine.data(),
                             nullptr,
                             nullptr,
                             FALSE,
                             CREATE_NO_WINDOW,
                             nullptr,
                             tempPath,
                             &si,
                             &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        DeleteFileW(helperPath.c_str());
    }
    return ok == TRUE;
#else
    (void)cleanupRoots;
    (void)manifestPath;
    return false;
#endif
}

bool cleanupEmptyDirectoriesCmd(const std::string& root) {
#ifdef _WIN32
    if (root.empty()) {
        return false;
    }
    std::wstring rootW = Utf8ToWide(root);
    if (rootW.empty()) {
        return false;
    }

    std::wstring cmd = L"cmd.exe /c \"";
    cmd += L"del /f /q /a \"" + rootW + L"\\\\desktop.ini\" /s >nul 2>&1 & ";
    cmd += L"del /f /q /a \"" + rootW + L"\\\\thumbs.db\" /s >nul 2>&1 & ";
    cmd += L"for /f \\\"delims=\\\" %%d in ('dir /ad /b /s \\\"" + rootW +
           L"\\\" ^| sort /r') do rmdir \\\"%%d\\\" 2>nul & ";
    cmd += L"rmdir \\\"" + rootW + L"\\\" 2>nul\"";

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');
    BOOL ok = CreateProcessW(nullptr,
                             cmdLine.data(),
                             nullptr,
                             nullptr,
                             FALSE,
                             CREATE_NO_WINDOW,
                             nullptr,
                             nullptr,
                             &si,
                             &pi);
    if (!ok) {
        return false;
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, 30000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    (void)root;
    return false;
#endif
}

}  // namespace MultiThreadedInstaller
