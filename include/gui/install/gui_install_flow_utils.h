#pragma once

#include "gui/core/gui_manager.h"

// 安装流程的界面小工具：路径解析、安装按钮可用性、磁盘空间标签更新。
namespace MultiThreadedInstaller::GUIInstallFlowUtils {

/// 解析最终安装路径：selectedPath 为空/无效时回退到 config 的默认/旧目录。
std::wstring ResolveSelectedInstallPath(const InstallConfig& config,
                                        const std::wstring& selectedPath);
/// 据"许可已勾选 + 路径有效 + 空间足够"联动启用/禁用安装按钮。
void UpdateInstallButtonEnabled(DuiLib::CButtonUI* installButton,
                                DuiLib::CCheckBoxUI* licenseCheckbox,
                                DuiLib::CEditUI* installPathEdit,
                                uint64_t requiredDiskSpace);
/// 按目标路径刷新磁盘空间标签（可用/所需，及是否充足提示）。
void UpdateDiskSpaceLabel(DuiLib::CLabelUI* diskSpaceLabel,
                          const std::wstring& path,
                          uint64_t requiredDiskSpace);

} // namespace MultiThreadedInstaller::GUIInstallFlowUtils
