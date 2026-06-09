#pragma once

#include "gui/core/gui_manager.h"
#include "installer/app/console_interface.h"

#include <Windows.h>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// installer.exe / uninstaller.exe 的共用启动支撑：解析命令行决定运行模式，再分派到
// GUI 或静默的安装/卸载流程。

/// 当前可执行文件身份。
enum class LaunchBinary {
    Installer,    ///< installer.exe。
    Uninstaller,  ///< uninstaller.exe。
};

/// 运行模式（由 binary + 是否 --silent 决定）。
enum class LaunchMode {
    InstallGui,       ///< 图形安装。
    InstallSilent,    ///< 静默安装。
    UninstallGui,     ///< 图形卸载。
    UninstallSilent,  ///< 静默卸载。
};

/// 启动上下文：解析命令行后得到的运行模式与参数。
struct LaunchContext {
    LaunchBinary binary = LaunchBinary::Installer;  ///< 当前 exe 身份。
    LaunchMode mode = LaunchMode::InstallGui;       ///< 运行模式。
    CliSupport::InstallerArgs args;                 ///< 解析出的参数。
    std::vector<std::string> utf8Args;              ///< 原始命令行参数（UTF-8）。
};

/// 解析命令行，构造启动上下文。
LaunchContext BuildLaunchContextFromCommandLine(LaunchBinary binary);
/// 按启动上下文运行（建窗/静默执行/处理 --help 与自清理助手），返回进程退出码。
int RunLaunchContext(HINSTANCE hInstance, const LaunchContext& context);

} // namespace MultiThreadedInstaller
