#pragma once

#include <string>

namespace MultiThreadedInstaller {

class InstallerPathResolver {
public:
    InstallerPathResolver() = default;
    ~InstallerPathResolver() = default;

    std::string expandEnvironmentVariables(const std::string& path);
};

} // namespace MultiThreadedInstaller
