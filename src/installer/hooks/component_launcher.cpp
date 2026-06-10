#include "installer/hooks/component_launcher.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "common/win32_raii.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
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

std::wstring QuoteProcessPath(const std::wstring& value) {
    if (value.empty()) {
        return L"\"\"";
    }
    if (value.front() == L'"' && value.back() == L'"') {
        return value;
    }
    return L"\"" + value + L"\"";
}

std::wstring LowerExtension(const std::filesystem::path& executablePath) {
    std::wstring extension = executablePath.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return extension;
}

// 解析 Windows PowerShell 的绝对路径，不依赖 PATH（powershell.exe 位于
// %SystemRoot%\System32\WindowsPowerShell\v1.0，并不在 System32 根目录，
// 一旦用户 PATH 被改坏就找不到）。找不到时回退裸名 powershell.exe。
std::wstring ResolvePowerShellExePath() {
#ifdef _WIN32
    wchar_t systemDir[MAX_PATH] = {};
    const UINT len = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::filesystem::path candidate =
            std::filesystem::path(systemDir) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate.wstring();
        }
    }
#endif
    return L"powershell.exe";
}

} // namespace

ComponentLaunchCommand BuildComponentLaunchCommand(const std::filesystem::path& executablePath,
                                                   const std::string& args) {
    ComponentLaunchCommand command;
    const std::wstring executableW = executablePath.wstring();
    const std::wstring argsW = Utf8ToWide(args);
    const std::wstring extension = LowerExtension(executablePath);

    if (extension == L".bat" || extension == L".cmd") {
        command.type = ComponentLauncherType::Batch;
        command.commandLine = L"cmd.exe /c " + QuoteProcessPath(executableW);
        command.hideByDefault = true;
        command.startFailureMessage = "Failed to start batch component installer.";
    } else if (extension == L".ps1") {
        command.type = ComponentLauncherType::PowerShell;
        // 用绝对路径起 powershell（避免 PATH 损坏找不到）；-NonInteractive 防卡交互、
        // -NoProfile 不加载可能损坏的用户 profile、-ExecutionPolicy Bypass 放开脚本执行。
        command.commandLine =
            QuoteProcessPath(ResolvePowerShellExePath()) +
            L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
            QuoteProcessPath(executableW);
        command.hideByDefault = true;
        command.startFailureMessage = "Failed to start PowerShell component installer.";
    } else if (extension == L".msi") {
        command.type = ComponentLauncherType::Msi;
        command.commandLine = L"msiexec.exe /i " + QuoteProcessPath(executableW);
        command.startFailureMessage = "Failed to start MSI component installer.";
    } else {
        command.type = ComponentLauncherType::Direct;
        command.commandLine = QuoteProcessPath(executableW);
        command.startFailureMessage = "Failed to start component process.";
    }

    if (!argsW.empty()) {
        command.commandLine.append(L" ");
        command.commandLine.append(argsW);
    }
    return command;
}

ComponentInstallResult RunComponentInstaller(const ComponentInstallRequest& request) {
    ComponentInstallResult result;
    const std::string label = Utf8FromPath(request.executablePath.filename());
#ifdef _WIN32
    std::error_code existsEc;
    if (request.executablePath.empty() ||
        !std::filesystem::exists(request.executablePath, existsEc)) {
        result.message = "Component installer not found: " + Utf8FromPath(request.executablePath);
        logInstallerError("[Component] " + result.message);
        return result;
    }

    // 复用 BuildComponentLaunchCommand 按扩展名（exe/bat/cmd/ps1/msi）拼好命令行。
    const ComponentLaunchCommand launch =
        BuildComponentLaunchCommand(request.executablePath, request.args);
    std::vector<wchar_t> commandLine(launch.commandLine.begin(), launch.commandLine.end());
    commandLine.push_back(L'\0');

    // 工作目录：显式指定优先，否则用安装程序所在目录；目录无效则不传。
    std::wstring workingDir = request.workingDirectory.empty()
                                  ? request.executablePath.parent_path().wstring()
                                  : request.workingDirectory.wstring();
    {
        std::error_code ec;
        if (workingDir.empty() ||
            !std::filesystem::is_directory(std::filesystem::path(workingDir), ec)) {
            workingDir.clear();
        }
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = request.hideWindow ? SW_HIDE : SW_SHOWNORMAL;
    PROCESS_INFORMATION processInfo{};

    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT;
    if (request.hideWindow) {
        creationFlags |= CREATE_NO_WINDOW;
    }

    // 继承当前（安装器）进程的环境与管理员权限运行该组件安装程序。
    BOOL started = CreateProcessW(nullptr,
                                  commandLine.data(),
                                  nullptr,
                                  nullptr,
                                  FALSE,
                                  creationFlags,
                                  nullptr,
                                  workingDir.empty() ? nullptr : workingDir.c_str(),
                                  &startupInfo,
                                  &processInfo);
    if (!started) {
        result.message = launch.startFailureMessage +
                         " (GetLastError=" + std::to_string(GetLastError()) + ")";
        logInstallerError("[Component] " + result.message + " target=" + label);
        return result;
    }
    result.started = true;
    UniqueHandle process(processInfo.hProcess);
    UniqueHandle thread(processInfo.hThread);
    thread.reset();  // 不需要线程句柄，立即释放。

    const DWORD timeoutMs = request.timeoutSec == 0 ? INFINITE : request.timeoutSec * 1000;
    const DWORD waitResult = WaitForSingleObject(process.get(), timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), 2000);
        result.timedOut = true;
        result.message = "Component installer timed out after " +
                         std::to_string(request.timeoutSec) + "s: " + label;
        logInstallerError("[Component] " + result.message);
        return result;
    }
    if (waitResult != WAIT_OBJECT_0) {
        result.message = "Failed while waiting for component installer: " + label;
        logInstallerError("[Component] " + result.message);
        return result;
    }

    DWORD exitCode = 0xFFFFFFFFul;
    GetExitCodeProcess(process.get(), &exitCode);
    result.exitCode = exitCode;
    result.success = (exitCode == request.successExitCode);
    if (result.success) {
        result.message = "Component installer succeeded: " + label;
        logInstallerInfo("[Component] " + result.message);
    } else {
        result.message = "Component installer failed with exit code " +
                         std::to_string(exitCode) + ": " + label;
        logInstallerError("[Component] " + result.message);
    }
    return result;
#else
    result.message = "Component installation is supported on Windows only.";
    logInstallerError("[Component] " + result.message);
    return result;
#endif
}

} // namespace MultiThreadedInstaller
