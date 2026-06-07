#pragma once

#include "gui/core/gui_manager.h"

namespace MultiThreadedInstaller::GUIInstallFlowUtils {

std::wstring ResolveSelectedInstallPath(const InstallConfig& config,
                                        const std::wstring& selectedPath);
void UpdateInstallButtonEnabled(DuiLib::CButtonUI* installButton,
                                DuiLib::CCheckBoxUI* licenseCheckbox,
                                DuiLib::CEditUI* installPathEdit,
                                uint64_t requiredDiskSpace);
void UpdateDiskSpaceLabel(DuiLib::CLabelUI* diskSpaceLabel,
                          const std::wstring& path,
                          uint64_t requiredDiskSpace);

} // namespace MultiThreadedInstaller::GUIInstallFlowUtils
