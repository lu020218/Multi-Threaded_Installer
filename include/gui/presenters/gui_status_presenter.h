#pragma once

#include "gui/core/gui_manager.h"

// 状态呈现：把进度/完成结果渲染到进度页与完成页控件。
namespace MultiThreadedInstaller::GUIStatusPresenter {

/// 更新进度页显示（前缀 + 当前文件夹 + 百分比）。
void UpdateProgressDisplay(DuiLib::CPaintManagerUI& manager,
                           const std::wstring& progressPrefix,
                           const std::wstring& progressFolder,
                           float percentage);
/// 渲染安装完成页（成功/失败、是否需重启、运行/打开网页选项）。
void ShowInstallCompletion(DuiLib::CPaintManagerUI& manager,
                           const InstallConfig& config,
                           const CompletionMessageData& data);
/// 渲染卸载完成页。
void ShowUninstallCompletion(DuiLib::CPaintManagerUI& manager, const CompletionMessageData& data);

} // namespace MultiThreadedInstaller::GUIStatusPresenter
