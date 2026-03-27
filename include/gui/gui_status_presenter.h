#pragma once

#include "gui/gui_manager.h"

namespace MultiThreadedInstaller::GUIStatusPresenter {

void UpdateProgressDisplay(CPaintManagerUI& manager,
                           const std::wstring& progressFolder,
                           float percentage);
void ShowInstallCompletion(CPaintManagerUI& manager,
                           const InstallConfig& config,
                           const CompletionMessageData& data);
void ShowUninstallCompletion(CPaintManagerUI& manager, const CompletionMessageData& data);

} // namespace MultiThreadedInstaller::GUIStatusPresenter
