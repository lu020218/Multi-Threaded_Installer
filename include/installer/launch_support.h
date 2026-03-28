#pragma once

#include "gui/gui_manager.h"
#include "installer/console_interface.h"

#include <Windows.h>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

enum class LaunchBinary {
    Installer,
    Uninstaller,
};

enum class LaunchMode {
    InstallGui,
    InstallSilent,
    RepairGui,
    RepairSilent,
    UninstallGui,
    UninstallSilent,
    CleanupSelf,
};

struct LaunchContext {
    LaunchBinary binary = LaunchBinary::Installer;
    LaunchMode mode = LaunchMode::InstallGui;
    CliSupport::InstallerArgs args;
    std::vector<std::string> utf8Args;
};

LaunchContext BuildLaunchContextFromCommandLine(LaunchBinary binary);
int RunLaunchContext(HINSTANCE hInstance, const LaunchContext& context);

} // namespace MultiThreadedInstaller
