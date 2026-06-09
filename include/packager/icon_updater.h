#pragma once

#include <string>

namespace MultiThreadedInstaller {

/// 把 iconPath 指向的 .ico 写入 exePath 的 PE 图标资源（替换安装器图标）。失败返回 false + error。
bool UpdateInstallerIcon(const std::string& exePath, const std::string& iconPath, std::string& error);

} // namespace MultiThreadedInstaller
