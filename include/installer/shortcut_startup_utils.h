#pragma once

#include <filesystem>
#include <string>

namespace MultiThreadedInstaller {

std::filesystem::path findPrimaryExecutable(const std::filesystem::path& installRoot,
                                            const std::string& appName);
bool setAutoStartup(const std::string& appName, const std::filesystem::path& exePath);
bool removeAutoStartup(const std::string& appName);
bool createDesktopShortcut(const std::string& appName, const std::filesystem::path& exePath);
bool deleteDesktopShortcut(const std::string& appName);
bool createStartMenuShortcut(const std::string& appName,
                             const std::filesystem::path& exePath,
                             const std::string& uninstallDisplayName = {});
bool deleteStartMenuShortcut(const std::string& appName);

}  // namespace MultiThreadedInstaller
