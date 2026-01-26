#pragma once

#include <string>

namespace MultiThreadedInstaller {

struct VersionInfoData {
    std::string productName;
    std::string fileVersion;
    std::string productVersion;
    std::string companyName;
    std::string fileDescription;
    std::string copyright;
    std::string originalFilename;
};

bool UpdateInstallerVersionInfo(const std::string& exePath, const VersionInfoData& info, std::string& error);

} // namespace MultiThreadedInstaller
