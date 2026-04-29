#pragma once

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

bool scheduleSelfDelete();
bool scheduleSelfDeleteImmediate(const std::vector<std::string>& cleanupRoots,
                                 const std::string& manifestPath);
bool cleanupEmptyDirectoriesCmd(const std::string& root);

} // namespace MultiThreadedInstaller
