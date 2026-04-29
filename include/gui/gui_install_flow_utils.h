#pragma once

#include "gui/gui_manager.h"

namespace MultiThreadedInstaller::GUIInstallFlowUtils {

std::wstring ResolveSelectedInstallPath(const InstallConfig& config,
                                        const std::wstring& selectedPath);
void UpdateInstallButtonEnabled(CButtonUI* installButton,
                                CCheckBoxUI* licenseCheckbox,
                                CEditUI* installPathEdit,
                                uint64_t requiredDiskSpace);
void UpdateDiskSpaceLabel(CLabelUI* diskSpaceLabel,
                          const std::wstring& path,
                          uint64_t requiredDiskSpace);

} // namespace MultiThreadedInstaller::GUIInstallFlowUtils
