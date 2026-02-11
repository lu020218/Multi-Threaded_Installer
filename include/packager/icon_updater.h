#pragma once

#include <string>

namespace MultiThreadedInstaller {

bool UpdateInstallerIcon(const std::string& exePath, const std::string& iconPath, std::string& error);

} // namespace MultiThreadedInstaller
