#pragma once

#include "gui/gui_manager.h"

namespace MultiThreadedInstaller::GUIInstallActions {

struct InstallStartRequest {
    std::wstring installPath;
    bool autoRun = false;
    bool desktopIcons = false;
    std::wstring languageCode;
};

bool TryBuildInstallStartRequest(HWND hWnd,
                                 CPaintManagerUI& manager,
                                 CEditUI* installPathEdit,
                                 const InstallConfig& config,
                                 InstallStartRequest& request);
void RunPostInstallActions(HWND hWnd,
                           CPaintManagerUI& manager,
                           CEditUI* installPathEdit,
                           const InstallConfig& config);

} // namespace MultiThreadedInstaller::GUIInstallActions
