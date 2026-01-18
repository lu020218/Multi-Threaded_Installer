#pragma once

#include "common/types.h"
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

bool deleteRegistryValue(const RegistryEntry& entry);
bool writeRegistryValue(const RegistryEntry& entry, const std::string& value, RegistryValueType type);
void applyRegistryEntries(const std::vector<RegistryEntry>& entries,
                          const std::string& installDir,
                          const std::string& configVersion,
                          const std::string& appName);

} // namespace MultiThreadedInstaller
