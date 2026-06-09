#pragma once

#include "gui/core/gui_manager.h"

// 文案呈现：把产品名/版本等写入界面控件，并支持切换界面语言。
namespace MultiThreadedInstaller::GUITextPresenter {

/// 绑定与产品相关的静态文案（产品名、标题等）到界面控件。
void BindStaticAppTexts(DuiLib::CPaintManagerUI& manager, const InstallConfig& config);
/// 刷新版本相关文案显示。
void RefreshVersionTexts(DuiLib::CPaintManagerUI& manager, const InstallConfig& config);
/// 切换界面语言：加载对应语言资源并重刷文案，更新 config.languageCode。成功返回 true。
bool ApplyLanguage(DuiLib::CPaintManagerUI& manager, InstallConfig& config, const std::wstring& code);

} // namespace MultiThreadedInstaller::GUITextPresenter
