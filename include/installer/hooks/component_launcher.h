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

} // namespace MultiThreadedInstaller
