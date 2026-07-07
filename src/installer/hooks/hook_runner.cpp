#include "installer/hooks/hook_runner.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "common/win32_raii.h"
#include "installer/hooks/component_launcher.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
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

// 释放结果：临时工作目录 + 主脚本完整路径。dir 为空表示释放失败。
struct HookBundle {
    std::filesystem::path dir;
    std::filesystem::path mainScript;
};

// 把一段字节写到指定文件（先建父目录）。失败返回 false。
bool WriteBytesToFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    out.close();
    return static_cast<bool>(out);
}

// 把主脚本及其兄弟文件释放到一个唯一的临时子目录，使主脚本可直接 call/调用同目录脚本。
// 返回该临时目录与主脚本路径（失败返回空 dir）。
HookBundle ReleaseHookBundle(const HookScript& hook) {
    wchar_t tempBase[MAX_PATH] = {};
    DWORD n = GetTempPathW(MAX_PATH, tempBase);
    if (n == 0 || n > MAX_PATH) {
        return {};
    }

    // 唯一目录名：mti_hook_<pid>_<递增计数>，避免同一安装多个 hook 之间互相覆盖。
    static std::atomic<unsigned> counter{0};
    std::wstring dirName = L"mti_hook_";
    dirName += std::to_wstring(GetCurrentProcessId());
    dirName += L"_";
    dirName += std::to_wstring(counter.fetch_add(1) + 1);

    std::filesystem::path bundleDir = std::filesystem::path(tempBase) / dirName;
    std::error_code ec;
    std::filesystem::remove_all(bundleDir, ec);  // 清理可能的同名残留
    std::filesystem::create_directories(bundleDir, ec);
    if (ec) {
        return {};
    }

    // 主脚本文件名：保留原名（便于 %~dp0 与兄弟脚本互相引用）；去掉路径分隔符。
    std::string stem = hook.scriptName.empty() ? "hook.bat" : hook.scriptName;
    for (char& c : stem) {
        if (c == '\\' || c == '/' || c == ':') {
            c = '_';
        }
    }
    std::wstring mainFileName = Utf8ToWide(stem);
    // 保留脚本原扩展名（.bat/.cmd/.ps1），以便按类型选择正确的启动方式；无法识别一律按 .bat。
    auto endsWithIgnoreCase = [](const std::wstring& s, const wchar_t* suffix) {
        const size_t len = wcslen(suffix);
        return s.size() >= len && _wcsicmp(s.c_str() + s.size() - len, suffix) == 0;
    };
    if (!endsWithIgnoreCase(mainFileName, L".bat") &&
        !endsWithIgnoreCase(mainFileName, L".cmd") &&
        !endsWithIgnoreCase(mainFileName, L".ps1")) {
        mainFileName += L".bat";
    }

    HookBundle bundle;
    bundle.dir = bundleDir;
    bundle.mainScript = bundleDir / mainFileName;
    if (!WriteBytesToFile(bundle.mainScript, hook.content)) {
        std::filesystem::remove_all(bundleDir, ec);
        return {};
    }

    // 兄弟文件：按相对路径释放到同一临时目录（保留子目录结构）。
    for (const auto& aux : hook.auxFiles) {
        if (aux.relativePath.empty()) {
            continue;
        }
        const std::filesystem::path target = bundleDir / std::filesystem::path(aux.relativePath);
        if (!WriteBytesToFile(target, aux.content)) {
            logInstallerWarning("[Hook] Failed to release sibling file: " + aux.relativePath);
        }
    }
    return bundle;
}

// 展开 keepDir：先把 %INSTALL_DIR% / %VERSION%（引擎注入值，父进程环境里并不存在）
// 大小写不敏感地替换掉，再用 ExpandEnvironmentStringsW 展开 %ProgramData% 等系统环境变量。
std::wstring ExpandHookKeepDir(const std::string& keepDir,
                               const std::string& installDir,
                               const std::string& version) {
    auto replaceTokenCI = [](std::wstring text, const std::wstring& token,
                             const std::wstring& value) {
        std::wstring lowerToken = token;
        std::transform(lowerToken.begin(), lowerToken.end(), lowerToken.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        std::wstring lowerText = text;
        std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        size_t pos = 0;
        while ((pos = lowerText.find(lowerToken, pos)) != std::wstring::npos) {
            text.replace(pos, lowerToken.size(), value);
            lowerText.replace(pos, lowerToken.size(),
                              std::wstring(value.size(), L' '));  // 占位避免重复命中
            pos += value.size();
        }
        return text;
    };

    std::wstring expanded = Utf8ToWide(keepDir);
    expanded = replaceTokenCI(expanded, L"%INSTALL_DIR%", Utf8ToWide(installDir));
    expanded = replaceTokenCI(expanded, L"%VERSION%", Utf8ToWide(version));

    DWORD size = ExpandEnvironmentStringsW(expanded.c_str(), nullptr, 0);
    if (size == 0) {
        return expanded;
    }
    std::wstring out;
    out.resize(size);
    DWORD written = ExpandEnvironmentStringsW(expanded.c_str(), out.data(), size);
    if (written == 0) {
        return expanded;
    }
    if (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
}

// 把临时目录（主脚本+兄弟文件）整体拷贝到 keepDir。尽力而为，失败仅记日志。
void PersistHookBundle(const std::filesystem::path& bundleDir,
                       const HookScript& hook,
                       const std::string& installDir,
                       const std::string& version) {
    const std::wstring keepDirW = ExpandHookKeepDir(hook.keepDir, installDir, version);
    if (keepDirW.empty()) {
        logInstallerWarning("[Hook] keep requested but keepDir is empty; skipped.");
        return;
    }
    const std::filesystem::path keepDir(keepDirW);
    std::error_code ec;
    std::filesystem::create_directories(keepDir, ec);
    std::filesystem::copy(bundleDir, keepDir,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec) {
        logInstallerWarning("[Hook] Failed to keep script bundle to " + Utf8FromPath(keepDir) +
                            ": " + ec.message());
    } else {
        logInstallerInfo("[Hook] Kept script bundle to: " + Utf8FromPath(keepDir));
    }
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
                                           const std::string& version,
                                           const std::wstring& scriptDir) {
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
            // NoDefaultCurrentDirectoryInExePath：若被继承会禁止 cmd 在当前目录查找命令，
            // 导致主脚本 `call common.bat` 找不到同目录兄弟脚本。剔除它以恢复默认查找。
            if (_wcsicmp(name.c_str(), L"NoDefaultCurrentDirectoryInExePath") == 0) {
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

    // 把脚本临时目录加到 PATH 最前，保证主脚本 `call common.bat` 总能解析到兄弟脚本，
    // 即便目标机设置了 NoDefaultCurrentDirectoryInExePath（禁用 cwd 查找）也不受影响。
    if (!scriptDir.empty()) {
        if (auto* pathValue = env.find(L"Path")) {
            if (!PathContainsDir(*pathValue, scriptDir)) {
                *pathValue = scriptDir + L";" + *pathValue;
            }
        } else {
            env.set(L"Path", scriptDir);
        }
    }

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
                    const std::string& version,
                    HookRunStat* outStat) {
    if (!hook.present) {
        return HookOutcome::NotPresent;
    }

#ifdef _WIN32
    const std::string hookName = hook.scriptName.empty() ? "hook" : hook.scriptName;

    // 逐步填充本次执行的性能/结果明细，最终由 emit() 写回 outStat。
    HookRunStat stat;
    stat.name = hookName;
    auto emit = [&](HookOutcome oc) -> HookOutcome {
        stat.outcome = oc;
        if (outStat) {
            *outStat = stat;
        }
        return oc;
    };

    // [Perf] 释放阶段耗时（把主脚本+兄弟文件写到临时目录）。
    const auto prepareStart = std::chrono::steady_clock::now();
    HookBundle bundle = ReleaseHookBundle(hook);
    stat.prepareMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - prepareStart)
                         .count();
    if (bundle.dir.empty()) {
        logInstallerError("[Hook] Failed to release hook script: " + hookName);
        return emit(FailOutcome(hook));
    }
    // 退出时清理整个临时目录（主脚本 + 兄弟文件）。
    struct ScopedRemove {
        std::filesystem::path path;
        ~ScopedRemove() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } scopedRemove{bundle.dir};

    const ComponentLaunchCommand launchCommand =
        BuildComponentLaunchCommand(bundle.mainScript, hook.args);
    std::vector<wchar_t> commandLineBuffer(launchCommand.commandLine.begin(),
                                           launchCommand.commandLine.end());
    commandLineBuffer.push_back(L'\0');

    // 工作目录设为临时脚本目录，使主脚本可直接 `call common.bat` / `.\sub\helper.ps1`
    //（相对路径相对 cwd 解析）。安装目录仍通过 INSTALL_DIR 环境变量提供，不依赖 cwd。
    const std::wstring workingDirectory = bundle.dir.wstring();

    // 环境块额外把脚本目录加进 PATH，并剔除 NoDefaultCurrentDirectoryInExePath，
    // 双保险让兄弟脚本调用在任何目标机都能解析。
    std::vector<wchar_t> environment =
        BuildEnvironmentBlock(installDir, version, workingDirectory);

    // 执行后保留：无论脚本成功失败都把临时目录拷到 keepDir。
    auto persistIfRequested = [&]() {
        if (hook.keep) {
            PersistHookBundle(bundle.dir, hook, installDir, version);
        }
    };

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

    // 脚本类型标签（用于耗时汇总，尤其关注 ps1 的启动/环境初始化耗时）。
    const char* typeLabel =
        launchCommand.type == ComponentLauncherType::PowerShell ? "ps1"
        : launchCommand.type == ComponentLauncherType::Batch    ? "bat/cmd"
        : launchCommand.type == ComponentLauncherType::Msi      ? "msi"
                                                                : "exe";
    stat.type = typeLabel;
    const auto hookStart = std::chrono::steady_clock::now();
    auto hookElapsedMs = [&hookStart]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - hookStart)
            .count();
    };

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
        return emit(FailOutcome(hook));
    }
    UniqueHandle process(processInfo.hProcess);
    UniqueHandle thread(processInfo.hThread);
    thread.reset();  // 不需要线程句柄，立即释放。

    const DWORD timeoutMs = hook.timeoutSec == 0 ? INFINITE : hook.timeoutSec * 1000;
    const DWORD waitResult = WaitForSingleObject(process.get(), timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), 2000);
        stat.execMs = hookElapsedMs();
        logInstallerError("[Hook] Hook timed out after " + std::to_string(hook.timeoutSec) +
                          "s: " + hookName);
        persistIfRequested();
        return emit(FailOutcome(hook));
    }
    if (waitResult != WAIT_OBJECT_0) {
        stat.execMs = hookElapsedMs();
        logInstallerError("[Hook] Failed while waiting for hook: " + hookName);
        persistIfRequested();
        return emit(FailOutcome(hook));
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.get(), &exitCode);
    stat.execMs = hookElapsedMs();
    stat.exitCode = exitCode;

    // 详细耗时/规模明细统一在 [InstallFlow][TimingSummary] 汇总展示（不再分散在此逐条打印）。
    persistIfRequested();
    if (exitCode == 0) {
        logInstallerInfo("[Hook] Hook succeeded: " + hookName);
        return emit(HookOutcome::Success);
    }
    logInstallerError("[Hook] Hook failed with exit code " + std::to_string(exitCode) +
                      ": " + hookName);
    return emit(FailOutcome(hook));
#else
    (void)installDir;
    (void)version;
    (void)outStat;
    logInstallerError("[Hook] Hooks are supported on Windows only.");
    return FailOutcome(hook);
#endif
}

HookOutcome RunHooks(const std::vector<HookScript>& hooks,
                     const std::string& installDir,
                     const std::string& version,
                     std::vector<HookRunStat>* outStats) {
    if (hooks.empty()) {
        return HookOutcome::NotPresent;
    }

    bool sawContinueFailure = false;
    for (size_t i = 0; i < hooks.size(); ++i) {
        HookRunStat stat;
        const HookOutcome outcome =
            RunHook(hooks[i], installDir, version, outStats ? &stat : nullptr);
        if (outStats) {
            outStats->push_back(std::move(stat));
        }
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
