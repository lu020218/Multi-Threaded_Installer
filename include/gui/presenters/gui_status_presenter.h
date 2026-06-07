#pragma once

#include "gui/core/gui_manager.h"

namespace MultiThreadedInstaller::GUIStatusPresenter {

void UpdateProgressDisplay(DuiLib::CPaintManagerUI& manager,
                           const std::wstring& progressPrefix,
                           const std::wstring& progressFolder,
                           float percentage);
void ShowInstallCompletion(DuiLib::CPaintManagerUI& manager,
                           const InstallConfig& config,
                           const CompletionMessageData& data);
void ShowUninstallCompletion(DuiLib::CPaintManagerUI& manager, const CompletionMessageData& data);

} // namespace MultiThreadedInstaller::GUIStatusPresenter
