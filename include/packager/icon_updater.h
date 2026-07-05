#pragma once

#include <string>

namespace MultiThreadedInstaller {

/// 把 iconPath 指向的 .ico 写入 exePath 的 PE 图标资源（替换安装器图标，带重试）。失败返回 false + error。
bool UpdateInstallerIcon(const std::string& exePath, const std::string& iconPath, std::string& error);
/// 仅在「已打开的资源更新会话句柄」上写入图标资源（不 Begin/End），供单会话编排复用。
bool ApplyInstallerIconInto(void* update, const std::string& iconPath, std::string& error);

} // namespace MultiThreadedInstaller
