#pragma once

#include "gui/gui_manager.h"

namespace MultiThreadedInstaller::GUITextPresenter {

void BindStaticAppTexts(CPaintManagerUI& manager, const InstallConfig& config);
void RefreshVersionTexts(CPaintManagerUI& manager, const InstallConfig& config);
bool ApplyLanguage(CPaintManagerUI& manager, InstallConfig& config, const std::wstring& code);

} // namespace MultiThreadedInstaller::GUITextPresenter
