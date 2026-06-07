#pragma once

#include "gui/core/gui_manager.h"

namespace MultiThreadedInstaller::GUITextPresenter {

void BindStaticAppTexts(DuiLib::CPaintManagerUI& manager, const InstallConfig& config);
void RefreshVersionTexts(DuiLib::CPaintManagerUI& manager, const InstallConfig& config);
bool ApplyLanguage(DuiLib::CPaintManagerUI& manager, InstallConfig& config, const std::wstring& code);

} // namespace MultiThreadedInstaller::GUITextPresenter
