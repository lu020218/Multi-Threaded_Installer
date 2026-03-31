#pragma once

#include <UIlib.h>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

struct InstallConfig;

bool HandleRunningApplicationDialog(HWND hWnd, const std::vector<std::string>& processNames);

void RefreshLicenseText(DuiLib::CPaintManagerUI& paintManager, const InstallConfig& config);

void ApplyLicenseAgreementSelection(DuiLib::CCheckBoxUI* licenseCheckbox,
                                    bool agreed,
                                    DuiLib::CButtonUI* installButton,
                                    uint64_t requiredDiskSpace,
                                    DuiLib::CEditUI* installPathEdit);

void ShowLicensePage(DuiLib::CPaintManagerUI& paintManager,
                     DuiLib::CTabLayoutUI* tabPages,
                     DuiLib::CCheckBoxUI* licenseCheckbox,
                     const InstallConfig& config,
                     int licensePageIndex);

} // namespace MultiThreadedInstaller
