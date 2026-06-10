#pragma once

#include <filesystem>
#include <string>

namespace MultiThreadedInstaller {

// 快捷方式与开机自启管理。全部 best-effort：失败返回 false 由调用方记日志。

/// 在安装根下找主可执行文件（优先匹配 <appName>.exe，回退到合理候选）。
std::filesystem::path findPrimaryExecutable(const std::filesystem::path& installRoot,
                                            const std::string& appName);
/// 设置开机自启（写当前用户 Run 注册表项，指向 exePath）。
bool setAutoStartup(const std::string& appName, const std::filesystem::path& exePath);
/// 移除开机自启项。
bool removeAutoStartup(const std::string& appName);
/// 在桌面创建指向 exePath 的快捷方式。
/// @param iconPath 可选，手动指定快捷方式图标（如安装目录下的 app.ico）；留空则用 exe 自带图标。
bool createDesktopShortcut(const std::string& appName,
                           const std::filesystem::path& exePath,
                           const std::filesystem::path& iconPath = {});
/// 删除桌面快捷方式。
bool deleteDesktopShortcut(const std::string& appName);
/// 在开始菜单创建快捷方式。@param uninstallDisplayName 可选，用于关联卸载显示名。
bool createStartMenuShortcut(const std::string& appName,
                             const std::filesystem::path& exePath,
                             const std::string& uninstallDisplayName = {});
/// 删除开始菜单快捷方式。
bool deleteStartMenuShortcut(const std::string& appName);

}  // namespace MultiThreadedInstaller
