#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

std::filesystem::path GetPackagerExecutableDirectory();
std::filesystem::path GetDefaultInstallerTemplatePath();
std::filesystem::path GetDefaultUninstallerTemplatePath();
bool LoadInstallerTemplate(const std::filesystem::path& templatePath,
                           std::vector<uint8_t>& outTemplate,
                           std::string& error);

} // namespace MultiThreadedInstaller
