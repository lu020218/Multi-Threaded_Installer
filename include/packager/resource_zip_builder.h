#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

bool BuildResourceZip(const std::filesystem::path& resourceDir,
                      std::vector<uint8_t>& outZip,
                      std::string& error);

} // namespace MultiThreadedInstaller
