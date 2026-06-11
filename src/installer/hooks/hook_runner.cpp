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
    // 保留脚本原扩展名（.bat/.cmd/.ps1），以便按类型选择正确的启动方式
    //（BuildComponentLaunchCommand 会据扩展名用 cmd / powershell 运行）；
    // 无法识别的扩展名一律按 .bat 释放。
    auto endsWithIgnoreCase = [](const std::wstring& s, const wchar_t* suffix) {
        const size_t n = wcslen(suffix);
        return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
    };
    if (!endsWithIgnoreCase(fileName, L".bat") &&
        !endsWithIgnoreCase(fileName, L".cmd") &&
        !endsWithIgnoreCase(fileName, L".ps1")) {
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

// 环境变量集合（大小写不敏感地按名查找/设置，保留首个出现的名字大小写）。
struct EnvVarList {
    std::vector<std::pair<std::wstring, std::wstring>> items;

    std::wstring* find(const std::wstring& name) {
        for (auto& kv : items) {
            if (_wcsicmp(kv.first.c_str(), name.c_str()) == 0) {
                return &kv.second;
            }
        }
        return nullptr;
    }
    bool has(const std::wstring& name) { return find(name) != nullptr; }
    void setIfMissing(const std::wstring& name, const std::wstring& value) {
        if (!has(name)) {
            items.emplace_back(name, value);
        }
    }
    void set(const std::wstring& name, const std::wstring& value) {
        if (auto* existing = find(name)) {
            *existing = value;
        } else {
            items.emplace_back(name, value);
        }
    }
};

bool PathContainsDir(const std::wstring& pathValue, const std::wstring& dir) {
    // 按 ';' 拆分后做大小写不敏感比较，避免子串误判。
    size_t start = 0;
    while (start <= pathValue.size()) {
        size_t end = pathValue.find(L';', start);
        if (end == std::wstring::npos) {
            end = pathValue.size();
        }
        std::wstring segment = pathValue.substr(start, end - start);
        while (!segment.empty() && (segment.back() == L'\\' || segment.back() == L' ')) {
            segment.pop_back();
        }
        std::wstring normalizedDir = dir;
        while (!normalizedDir.empty() && normalizedDir.back() == L'\\') {
            normalizedDir.pop_back();
        }
        if (!segment.empty() && _wcsicmp(segment.c_str(), normalizedDir.c_str()) == 0) {
            return true;
        }
        if (end == pathValue.size()) {
            break;
        }
        start = end + 1;
    }
    return false;
}

// 在当前进程环境基础上注入 INSTALL_DIR/VERSION，并加固 PowerShell/系统所需的关键变量，
// 构造子进程环境块。解决部分用户机器环境损坏（缺 SystemRoot/PATH/PSModulePath）导致
// ps1（乃至 cmd）起不来或找不到模块/cmdlet 的问题。
std::vector<wchar_t> BuildEnvironmentBlock(const std::string& installDir,
                                           const std::string& version) {
    EnvVarList env;
    LPWCH existing = GetEnvironmentStringsW();
    if (existing) {
        for (LPWCH cursor = existing; *cursor != L'\0';) {
            std::wstring entry(cursor);
            cursor += entry.size() + 1;
            const size_t eq = entry.find(L'=');
            if (eq == std::wstring::npos || eq == 0) {
                continue;  // 跳过形如 "=C:=..." 的驱动器当前目录条目与畸形项
            }
            const std::wstring name = entry.substr(0, eq);
            const std::wstring value = entry.substr(eq + 1);
            // 跳过将由我们注入的同名变量，避免重复定义。
            if (_wcsicmp(name.c_str(), L"INSTALL_DIR") == 0 ||
                _wcsicmp(name.c_str(), L"VERSION") == 0) {
                continue;
            }
            env.items.emplace_back(name, value);
        }
        FreeEnvironmentStringsW(existing);
    }

    // ── 加固关键环境变量（仅在缺失/不完整时补齐，不覆盖用户已有的有效值）──
    // SystemRoot / windir：很多系统组件与 PowerShell 都依赖它定位自身。
    std::wstring systemRoot;
    if (auto* sr = env.find(L"SystemRoot")) {
        systemRoot = *sr;
    } else if (auto* wd = env.find(L"windir")) {
        systemRoot = *wd;
    } else {
        wchar_t buffer[MAX_PATH] = {};
        const UINT n = GetWindowsDirectoryW(buffer, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            systemRoot = buffer;
        }
    }
    if (!systemRoot.empty()) {
        env.setIfMissing(L"SystemRoot", systemRoot);
        env.setIfMissing(L"windir", systemRoot);

        const std::wstring system32 = systemRoot + L"\\System32";
        const std::wstring psHome = system32 + L"\\WindowsPowerShell\\v1.0";

        // PATH：保证含 System32 与 WindowsPowerShell\v1.0（缺则补在前面）。
        std::wstring* pathValue = env.find(L"Path");
        if (!pathValue) {
            env.set(L"Path", system32 + L";" + psHome);
        } else {
            std::wstring prefix;
            if (!PathContainsDir(*pathValue, psHome)) {
                prefix = psHome + L";" + prefix;
            }
            if (!PathContainsDir(*pathValue, system32)) {
                prefix = system32 + L";" + prefix;
            }
            if (!prefix.empty()) {
                *pathValue = prefix + *pathValue;
            }
        }

        // PSModulePath：缺失则给默认模块路径，否则 powershell 找不到内置模块。
        env.setIfMissing(L"PSModulePath", psHome + L"\\Modules");
    }
    // PATHEXT：缺失时给常见默认，含 .PS1。
    env.setIfMissing(L"PATHEXT", L".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WSF;.WSH;.MSC;.PS1");

    // 注入引擎已知值。
    env.set(L"INSTALL_DIR", Utf8ToWide(installDir));
    env.set(L"VERSION", Utf8ToWide(version));

    // ── 拼装环境块（NAME=VALUE\0 ... \0）──
    std::vector<wchar_t> block;
    for (const auto& kv : env.items) {
        const std::wstring entry = kv.first + L"=" + kv.second;
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');  // 双 NUL 结束
    return block;
}

// 计算脚本日志路径：与安装器日志放在同一目录，文件名 = 脚本名去扩展名 + ".log"
//（如 pre_install.bat → pre_install.log）。安装器日志路径不可用时回退到临时目录。
std::filesystem::path ResolveScriptLogPath(const std::string& scriptName) {
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
    std::string sanitized = scriptName.empty() ? "hook" : scriptName;
    for (char& c : sanitized) {
        if (c == '\\' || c == '/' || c == ':') {
            c = '_';
        }
    }
    std::wstring stem = PathFromUtf8(sanitized).stem().wstring();
    if (stem.empty()) {
        stem = L"hook";
    }
    return dir / (stem + L".log");
}

// 创建（覆盖）脚本日志文件并返回“可被子进程继承”的写句柄；失败返回空句柄。
// 句柄设为可继承，配合 CreateProcess(bInheritHandles=TRUE) 让脚本把 stdout/stderr 写入该文件。
UniqueHandle CreateInheritableLogFile(const std::filesystem::path& path) {
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
    return UniqueHandle(handle);  // INVALID_HANDLE_VALUE 由 UniqueHandle 视为空
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

    // 把脚本的标准输出/标准错误重定向到与安装器日志同目录、以脚本名命名的日志文件
    //（如 pre_install.bat → pre_install.log）。需要把文件句柄设为可继承，并让
    // CreateProcess 以 bInheritHandles=TRUE 启动，子进程（cmd/powershell）才会写入该文件。
    const std::filesystem::path scriptLogPath = ResolveScriptLogPath(hookName);
    UniqueHandle scriptLog = CreateInheritableLogFile(scriptLogPath);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;
    BOOL inheritHandles = FALSE;
    if (scriptLog) {
        startupInfo.dwFlags |= STARTF_USESTDHANDLES;
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startupInfo.hStdOutput = scriptLog.get();
        startupInfo.hStdError = scriptLog.get();
        inheritHandles = TRUE;
        logInstallerInfo("[Hook] Script output redirected to: " + Utf8FromPath(scriptLogPath));
    } else {
        logInstallerWarning("[Hook] Failed to create script log file: " +
                            Utf8FromPath(scriptLogPath) + "; script output will not be captured.");
    }
    PROCESS_INFORMATION processInfo{};

    BOOL started = CreateProcessW(nullptr,
                                  commandLineBuffer.data(),
                                  nullptr,
                                  nullptr,
                                  inheritHandles,
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

HookOutcome RunHooks(const std::vector<HookScript>& hooks,
                     const std::string& installDir,
                     const std::string& version) {
    if (hooks.empty()) {
        return HookOutcome::NotPresent;
    }

    bool sawContinueFailure = false;
    for (size_t i = 0; i < hooks.size(); ++i) {
        const HookOutcome outcome = RunHook(hooks[i], installDir, version);
        switch (outcome) {
            case HookOutcome::FailedAbort:
                // abort 失败立即停止，后续脚本不再执行，交由调用方中止/回滚。
                logInstallerError("[Hook] Aborting remaining hooks at index " +
                                  std::to_string(i) + " due to abort-failure.");
                return HookOutcome::FailedAbort;
            case HookOutcome::FailedContinue:
                sawContinueFailure = true;  // 记录后继续后续脚本
                break;
            case HookOutcome::Success:
            case HookOutcome::NotPresent:
                break;
        }
    }
    return sawContinueFailure ? HookOutcome::FailedContinue : HookOutcome::Success;
}

} // namespace MultiThreadedInstaller
