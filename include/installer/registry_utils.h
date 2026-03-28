#pragma once

#include "common/config_types.h"
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
bool deleteRegistryPath(const std::string& path);
bool writeRegistryValue(const RegistryEntry& entry, const std::string& value, RegistryValueType type);
void applyRegistryEntries(const std::vector<RegistryEntry>& entries,
                          const std::string& installDir,
                          const std::string& configVersion,
                          const std::string& appName);
std::string sanitizeRegistryKeyName(const std::string& name);
bool readRegistryStringValue(const std::string& path, const std::string& key, std::string& value);
bool writeUninstallRegistryEntry(const std::string& appName,
                                 const std::string& version,
                                 const std::string& installDir,
                                 const std::string& uninstallExePath,
                                 bool perMachine);
bool deleteUninstallRegistryEntry(const std::string& appName, bool perMachine);
bool deleteMatchingUninstallRegistryEntries(const std::string& installDir,
                                            const std::string& uninstallExePath,
                                            bool perMachine);

} // namespace MultiThreadedInstaller
