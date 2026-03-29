#pragma once

#include <UIlib.h>
#include <string>

namespace MultiThreadedInstaller {

struct InstallConfig;

std::wstring ResolveInitialInstallPath(const InstallConfig& config);

void ApplyInitialInstallPathUi(DuiLib::CPaintManagerUI& paintManager,
                               DuiLib::CEditUI* installPathEdit,
                               const std::wstring& installPath,
                               bool repairMode);

} // namespace MultiThreadedInstaller
