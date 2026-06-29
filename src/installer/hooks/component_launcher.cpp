#include "installer/hooks/component_launcher.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "common/win32_raii.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <system_error>
#include <utility>
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

#ifdef _WIN32
// 复制当前（安装器）进程环境，注入/覆盖 INSTALL_DIR 与 VERSION，构造子进程环境块。
// 让组件安装程序能读到安装目录与版本（与 hook 脚本一致）。
std::vector<wchar_t> BuildComponentEnvBlock(const std::string& installDir,
                                            const std::string& version) {
    std::vector<std::pair<std::wstring, std::wstring>> items;
    LPWCH existing = GetEnvironmentStringsW();
    if (existing) {
        for (LPWCH cursor = existing; *cursor != L'\0';) {
            std::wstring entry(cursor);
            cursor += entry.size() + 1;
            const size_t eq = entry.find(L'=');
            if (eq == std::wstring::npos || eq == 0) {
                continue;
            }
            const std::wstring name = entry.substr(0, eq);
            if (_wcsicmp(name.c_str(), L"INSTALL_DIR") == 0 ||
                _wcsicmp(name.c_str(), L"VERSION") == 0) {
                continue;  // 由下方注入，避免重复。
            }
            items.emplace_back(name, entry.substr(eq + 1));
        }
        FreeEnvironmentStringsW(existing);
    }
    if (!installDir.empty()) {
        items.emplace_back(L"INSTALL_DIR", Utf8ToWide(installDir));
    }
    if (!version.empty()) {
        items.emplace_back(L"VERSION", Utf8ToWide(version));
    }
    std::vector<wchar_t> block;
    for (const auto& kv : items) {
        const std::wstring e = kv.first + L"=" + kv.second;
        block.insert(block.end(), e.begin(), e.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');  // 双 NUL 结束
    return block;
}

// 创建（覆盖）组件输出日志文件，返回可继承写句柄；与安装器日志同目录、以 logBaseName 命名。
UniqueHandle CreateComponentLogFile(const std::string& logBaseName) {
    if (logBaseName.empty()) {
        return UniqueHandle(INVALID_HANDLE_VALUE);
    }
    std::filesystem::path dir;
    const std::string installerLog = getInstallerLogPath();
    if (!installerLog.empty()) {
        dir = PathFromUtf8(installerLog).parent_path();
    }
    if (dir.empty()) {
        wchar_t temp[MAX_PATH] = {};
        const DWORD n = GetTempPathW(MAX_PATH, temp);
        if (n > 0 && n <= MAX_PATH) {
            dir = std::filesystem::path(temp);
        }
    }
    std::string sanitized = logBaseName;
    for (char& c : sanitized) {
        if (c == '\\' || c == '/' || c == ':') {
            c = '_';
        }
    }
    const std::filesystem::path path = dir / (Utf8ToWide(sanitized) + L".log");
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    HANDLE handle = CreateFileW(path.c_str(),
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                &sa,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        logInstallerInfo("[Component] output redirected to: " + Utf8FromPath(path));
    }
    return UniqueHandle(handle);
}
#endif // _WIN32

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

    // 注入 INSTALL_DIR/VERSION 的子进程环境块；捕获 stdout/stderr 到组件日志文件（可选）。
    std::vector<wchar_t> environment =
        BuildComponentEnvBlock(request.injectInstallDir, request.injectVersion);
    UniqueHandle componentLog = CreateComponentLogFile(request.logBaseName);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = request.hideWindow ? SW_HIDE : SW_SHOWNORMAL;
    BOOL inheritHandles = FALSE;
    if (componentLog) {
        startupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startupInfo.hStdOutput = componentLog.get();
        startupInfo.hStdError = componentLog.get();
        inheritHandles = TRUE;
    }
    PROCESS_INFORMATION processInfo{};

    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT;
    if (request.hideWindow) {
        creationFlags |= CREATE_NO_WINDOW;
    }

    // 以安装器的管理员权限运行该组件安装程序；环境注入 INSTALL_DIR/VERSION。
    BOOL started = CreateProcessW(nullptr,
                                  commandLine.data(),
                                  nullptr,
                                  nullptr,
                                  inheritHandles,
                                  creationFlags,
                                  environment.data(),
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
