#pragma once

#include <filesystem>
#include <string>

namespace MultiThreadedInstaller {

// 进程启动命令构造：按可执行文件类型（exe/bat/ps1/msi）拼出正确的命令行与运行方式。
// 现主要供 hook_runner 启动 pre/post bat 复用。

/// 启动器类型（按目标文件扩展名识别）。
enum class ComponentLauncherType {
    Direct,      ///< 直接运行 .exe。
    Batch,       ///< 经 cmd /c 运行 .bat/.cmd。
    PowerShell,  ///< 经 powershell 运行 .ps1。
    Msi          ///< 经 msiexec 运行 .msi。
};

/// 构造好的启动命令。
struct ComponentLaunchCommand {
    ComponentLauncherType type = ComponentLauncherType::Direct;  ///< 识别出的类型。
    std::wstring commandLine;          ///< 传给 CreateProcessW 的完整命令行。
    bool hideByDefault = false;        ///< 是否默认隐藏窗口（如 bat/控制台脚本）。
    std::string startFailureMessage = "Failed to start component process.";  ///< 启动失败提示。
};

/// 根据 executablePath 的类型与 args 构造启动命令（正确处理引号/cmd/powershell/msiexec 包装）。
ComponentLaunchCommand BuildComponentLaunchCommand(const std::filesystem::path& executablePath,
                                                   const std::string& args);

// ── 组件安装程序运行 ──────────────────────────────────────────────────────
// 运行随产品一起安装的「组件安装程序」（如 VC++ 运行库、驱动安装包等第三方安装器），
// 支持 exe 安装程序，或 bat/cmd/ps1/msi 脚本。供安装流程在解压完成后调用。

/// 组件安装请求。
struct ComponentInstallRequest {
    std::filesystem::path executablePath;    ///< 组件安装程序路径：exe，或 bat/cmd/ps1/msi。
    std::string args;                        ///< 传给安装程序的参数（如静默开关 /S、/quiet 等）。
    std::filesystem::path workingDirectory;  ///< 工作目录；留空则用 executablePath 所在目录。
    unsigned int timeoutSec = 0;             ///< 超时秒数，0 = 无限等待。
    bool hideWindow = true;                  ///< 是否隐藏子进程窗口。
    unsigned long successExitCode = 0;       ///< 视为成功的退出码（默认 0）。
};

/// 组件安装程序运行结果。
struct ComponentInstallResult {
    bool started = false;                    ///< 进程是否成功启动。
    bool timedOut = false;                   ///< 是否因超时被强制结束。
    bool success = false;                    ///< 退出码 == successExitCode 视为成功。
    unsigned long exitCode = 0xFFFFFFFFul;   ///< 子进程退出码（未启动/超时时无意义）。
    std::string message;                     ///< 供日志/界面展示的简要说明。
};

/// 运行一个组件安装程序并等待其结束（可超时）：
///   按扩展名识别 exe/bat/cmd/ps1/msi → 拼命令行 → CreateProcess（继承安装器管理员权限）→
///   等待最长 timeoutSec（超时则结束进程）。退出码 == request.successExitCode 视为成功。
/// best-effort：失败不抛异常，调用方据返回值决定中止/记日志/继续。Windows 专用。
ComponentInstallResult RunComponentInstaller(const ComponentInstallRequest& request);

} // namespace MultiThreadedInstaller
