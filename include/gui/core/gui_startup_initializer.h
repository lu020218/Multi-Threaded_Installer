#pragma once

#include <UIlib.h>
#include <string>

namespace MultiThreadedInstaller {

struct InstallConfig;

/// 计算欢迎页初始安装路径（升级/覆盖时用旧目录，否则用默认目录）。
std::wstring ResolveInitialInstallPath(const InstallConfig& config);

/// 把初始安装路径填入路径编辑框；lockInstallPath=true 时锁定不可改（如升级固定到旧目录）。
void ApplyInitialInstallPathUi(DuiLib::CPaintManagerUI& paintManager,
                               DuiLib::CEditUI* installPathEdit,
                               const std::wstring& installPath,
                               bool lockInstallPath);

} // namespace MultiThreadedInstaller
