#pragma once

#include <string>

namespace MultiThreadedInstaller {

enum class InstallerLogLevel {
    Info,
    Warning,
    Error,
    Debug,
};

void initializeInstallerLogging();
void flushInstallerLogging();
std::string getInstallerLogPath();
void writeInstallerLog(InstallerLogLevel level, const std::string& message);
void logInstallerInfo(const std::string& message);
void logInstallerWarning(const std::string& message);
void logInstallerError(const std::string& message);
void logInstallerDebug(const std::string& message);

} // namespace MultiThreadedInstaller
