#pragma once

#include "installer/platform/path_resolver.h"

#include <string>

namespace MultiThreadedInstaller {

// 引擎运行时安装状态。落点写死：HKLM\Software\<ProductName> + %ProgramData%\<ProductName>\install-state.json。
// productName 由 appName 字段承载（重构后产品名即唯一标识）。
struct InstallStateContext {
    std::string installDir;
    std::string version;
    std::string appName;   // 产品名，用于派生注册表键与数据目录
    std::string appId;     // 与产品名一致；保留字段以兼容调用方
    std::string installSource;
    std::string state;
    std::string userName;
    std::string language;  ///< 界面/快捷方式语言（如 zh_CN）；非空时写产品注册表 Language。
};

// 写产品注册表 + install-state.json（落点由引擎按产品名拼，不来自 YAML）。
bool ApplyInstallState(const InstallStateContext& context, InstallerPathResolver& resolver);

// 卸载/失败时清理安装状态：删产品注册表键 HKLM\Software\<product> 与 install-state.json。
bool CleanupInstallState(const InstallStateContext& context, InstallerPathResolver& resolver);

std::string GetCurrentUserNameForInstallState();

} // namespace MultiThreadedInstaller
