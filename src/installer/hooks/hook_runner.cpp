#include "installer/hooks/hook_runner.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "common/win32_raii.h"
#include "installer/hooks/component_launcher.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace MultiThreadedInstaller {
namespace {

constexpr uint8_t kOnFailureContinue = 1;  // HookScript::onFailure: 0=abort, 1=continue

HookOutcome FailOutcome(const HookScript& hook) {
    return hook.onFailure == kOnFailureContinue ? HookOutcome::FailedContinue
                                                : HookOutcome::FailedAbort;
}

#ifdef _WIN32

// 把内嵌脚本字节释放到临时目录，返回释放后的 .bat 路径（失败返回空）。
std::filesystem::path ReleaseHookScript(const HookScript& hook) {
    wchar_t tempDir[MAX_PATH] = {};
    DWORD n = GetTempPathW(MAX_PATH, tempDir);
    if (n == 0 || n > MAX_PATH) {
        return {};
    }

    std::wstring fileName = L"mti_hook_";
    fileName += std::to_wstring(GetCurrentProcessId());
    fileName += L"_";
    // 取配置的脚本名（仅日志用名字）作为可读后缀，去掉路径分隔符。
    std::string stem = hook.scriptName.empty() ? "hook" : hook.scriptName;
    for (char& c : stem) {
        if (c == '\\' || c == '/' || c == ':') {
            c = '_';
        }
    }
    fileName += Utf8ToWide(stem);
    if (fileName.size() < 4 ||
        _wcsicmp(fileName.c_str() + fileName.size() - 4, L".bat") != 0) {
        fileName += L".bat";
    }

    std::filesystem::path scriptPath = std::filesystem::path(tempDir) / fileName;
    std::ofstream out(scriptPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return {};
    }
    if (!hook.content.empty()) {
        out.write(reinterpret_cast<const char*>(hook.content.data()),
                  static_cast<std::streamsize>(hook.content.size()));
    }
    out.close();
    if (!out) {
        return {};
    }
    return scriptPath;
}

// 在当前进程环境基础上注入 INSTALL_DIR/VERSION，构造子进程环境块。
std::vector<wchar_t> BuildEnvironmentBlock(const std::string& installDir,
                                           const std::string& version) {
    std::vector<wchar_t> block;
    LPWCH existing = GetEnvironmentStringsW();
    if (existing) {
        for (LPWCH cursor = existing; *cursor != L'\0';) {
            std::wstring entry(cursor);
            cursor += entry.size() + 1;
            // 跳过将由我们注入的同名变量，避免重复定义。
            if (_wcsnicmp(entry.c_str(), L"INSTALL_DIR=", 12) == 0 ||
                _wcsnicmp(entry.c_str(), L"VERSION=", 8) == 0) {
                continue;
            }
            block.insert(block.end(), entry.begin(), entry.end());
            block.push_back(L'\0');
        }
        FreeEnvironmentStringsW(existing);
    }

    auto appendVar = [&](const std::wstring& name, const std::wstring& value) {
        std::wstring entry = name + L"=" + value;
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    };
    appendVar(L"INSTALL_DIR", Utf8ToWide(installDir));
    appendVar(L"VERSION", Utf8ToWide(version));

    block.push_back(L'\0');  // 双 NUL 结束
    return block;
}

#endif // _WIN32

} // namespace

HookOutcome RunHook(const HookScript& hook,
                    const std::string& installDir,
                    const std::string& version) {
    if (!hook.present) {
        return HookOutcome::NotPresent;
    }

#ifdef _WIN32
    const std::string hookName = hook.scriptName.empty() ? "hook" : hook.scriptName;

    std::filesystem::path scriptPath = ReleaseHookScript(hook);
    if (scriptPath.empty()) {
        logInstallerError("[Hook] Failed to release hook script: " + hookName);
        return FailOutcome(hook);
    }
    // 退出时清理临时脚本。
    struct ScopedRemove {
        std::filesystem::path path;
        ~ScopedRemove() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } scopedRemove{scriptPath};

    const ComponentLaunchCommand launchCommand =
        BuildComponentLaunchCommand(scriptPath, hook.args);
    std::vector<wchar_t> commandLineBuffer(launchCommand.commandLine.begin(),
                                           launchCommand.commandLine.end());
    commandLineBuffer.push_back(L'\0');

    std::vector<wchar_t> environment = BuildEnvironmentBlock(installDir, version);

    // preInstall 在解压前运行，此时安装目录可能尚不存在；仅当目录存在时才作为
    // 工作目录传给 CreateProcess（否则 CreateProcessW 会因 lpCurrentDirectory 无效而失败）。
    std::wstring workingDirectory;
    {
        std::error_code ec;
        if (!installDir.empty() &&
            std::filesystem::is_directory(std::filesystem::path(Utf8ToWide(installDir)), ec)) {
            workingDirectory = Utf8ToWide(installDir);
        }
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION processInfo{};

    BOOL started = CreateProcessW(nullptr,
                                  commandLineBuffer.data(),
                                  nullptr,
                                  nullptr,
                                  FALSE,
                                  CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                  environment.data(),
                                  workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                  &startupInfo,
                                  &processInfo);
    if (!started) {
        logInstallerError("[Hook] Failed to start hook process: " + hookName +
                          " (GetLastError=" + std::to_string(GetLastError()) + ")");
        return FailOutcome(hook);
    }
    UniqueHandle process(processInfo.hProcess);
    UniqueHandle thread(processInfo.hThread);
    thread.reset();  // 不需要线程句柄，立即释放。

    const DWORD timeoutMs = hook.timeoutSec == 0 ? INFINITE : hook.timeoutSec * 1000;
    const DWORD waitResult = WaitForSingleObject(process.get(), timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), 2000);
        logInstallerError("[Hook] Hook timed out after " + std::to_string(hook.timeoutSec) +
                          "s: " + hookName);
        return FailOutcome(hook);
    }
    if (waitResult != WAIT_OBJECT_0) {
        logInstallerError("[Hook] Failed while waiting for hook: " + hookName);
        return FailOutcome(hook);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.get(), &exitCode);

    if (exitCode == 0) {
        logInstallerInfo("[Hook] Hook succeeded: " + hookName);
        return HookOutcome::Success;
    }
    logInstallerError("[Hook] Hook failed with exit code " + std::to_string(exitCode) +
                      ": " + hookName);
    return FailOutcome(hook);
#else
    (void)installDir;
    (void)version;
    logInstallerError("[Hook] Hooks are supported on Windows only.");
    return FailOutcome(hook);
#endif
}

} // namespace MultiThreadedInstaller
