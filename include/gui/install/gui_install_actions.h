#pragma once

#include "gui/core/gui_manager.h"

// 安装动作：从界面收集"开始安装"所需参数，以及完成后的收尾动作（运行程序/打开网页）。
namespace MultiThreadedInstaller::GUIInstallActions {

/// 一次"开始安装"请求所需参数（自界面控件收集）。
struct InstallStartRequest {
    std::wstring installPath;   ///< 安装路径。
    bool autoRun = false;       ///< 完成后是否自动运行。
    bool desktopIcons = false;  ///< 是否建桌面快捷方式。
    std::wstring languageCode;  ///< 界面语言。
};

/// 从界面收集并校验安装参数，成功时填充 request 返回 true（校验失败会向用户提示）。
bool TryBuildInstallStartRequest(HWND hWnd,
                                 DuiLib::CPaintManagerUI& manager,
                                 DuiLib::CEditUI* installPathEdit,
                                 const InstallConfig& config,
                                 InstallStartRequest& request);
/// 完成页收尾动作：按勾选执行"运行程序""打开网页"等。
void RunPostInstallActions(HWND hWnd,
                           DuiLib::CPaintManagerUI& manager,
                           DuiLib::CEditUI* installPathEdit,
                           const InstallConfig& config);

} // namespace MultiThreadedInstaller::GUIInstallActions
