#pragma once

#include <string>

namespace MultiThreadedInstaller {

void initializeInstallerLogging();
void flushInstallerLogging();
std::string getInstallerLogPath();

} // namespace MultiThreadedInstaller
