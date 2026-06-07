#pragma once

#include <filesystem>
#include <string>

namespace MultiThreadedInstaller {

enum class ComponentLauncherType {
    Direct,
    Batch,
    PowerShell,
    Msi
};

struct ComponentLaunchCommand {
    ComponentLauncherType type = ComponentLauncherType::Direct;
    std::wstring commandLine;
    bool hideByDefault = false;
    std::string startFailureMessage = "Failed to start component process.";
};

ComponentLaunchCommand BuildComponentLaunchCommand(const std::filesystem::path& executablePath,
                                                   const std::string& args);

} // namespace MultiThreadedInstaller
