#pragma once

#include <string>

namespace MultiThreadedInstaller {

std::string PercentDecodeUrlPath(const std::string& value);
std::string FileUrlToPath(const std::string& url);

} // namespace MultiThreadedInstaller
