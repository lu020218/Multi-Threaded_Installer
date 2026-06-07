#pragma once

#include <UIlib.h>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

std::vector<std::string> BuildCurrentGuiXmlScope(const DuiLib::CTabLayoutUI* tabPages,
                                                 bool uninstallMode);

void LogCurrentPageControlImageSnapshot(DuiLib::CTabLayoutUI* tabPages,
                                        bool uninstallMode,
                                        const char* stage);

} // namespace MultiThreadedInstaller
