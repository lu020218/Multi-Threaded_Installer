#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

bool AppendEmbeddedResources(std::vector<uint8_t>& installerTemplate,
                             const std::filesystem::path& resourceDir,
                             std::string& error);

} // namespace MultiThreadedInstaller
