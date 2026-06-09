#pragma once

#include <string>

namespace MultiThreadedInstaller {

/// 路径解析器：把含 %ProgramFiles%/%ProgramData%/%AppData%/%InstallDir% 等占位的路径
/// 展开为真实路径。安装/卸载各处用它统一解析落点与清理目标。
class InstallerPathResolver {
public:
    InstallerPathResolver() = default;
    ~InstallerPathResolver() = default;

    /// 展开 path 中的环境变量占位符，返回真实路径（无法展开的占位原样保留）。
    std::string expandEnvironmentVariables(const std::string& path);
};

} // namespace MultiThreadedInstaller
