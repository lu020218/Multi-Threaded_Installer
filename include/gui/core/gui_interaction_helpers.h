#pragma once

#include <UIlib.h>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

struct InstallConfig;

// 从 GUIManager 抽出的若干交互小逻辑（运行中进程提示、许可页处理等）。

/// 安装前若检测到目标程序在运行，弹框提示用户关闭。返回 true 表示可继续。
bool HandleRunningApplicationDialog(HWND hWnd, const std::vector<std::string>& processNames);

/// 刷新许可协议文本控件内容（按当前语言/配置）。
void RefreshLicenseText(DuiLib::CPaintManagerUI& paintManager, const InstallConfig& config);

/// 应用"同意许可"勾选状态：据 agreed 联动启用/禁用安装按钮，并刷新磁盘空间/路径相关 UI。
void ApplyLicenseAgreementSelection(DuiLib::CCheckBoxUI* licenseCheckbox,
                                    bool agreed,
                                    DuiLib::CButtonUI* installButton,
                                    uint64_t requiredDiskSpace,
                                    DuiLib::CEditUI* installPathEdit);

/// 切换到许可协议页并初始化其控件状态。
void ShowLicensePage(DuiLib::CPaintManagerUI& paintManager,
                     DuiLib::CTabLayoutUI* tabPages,
                     DuiLib::CCheckBoxUI* licenseCheckbox,
                     const InstallConfig& config,
                     int licensePageIndex);

} // namespace MultiThreadedInstaller
