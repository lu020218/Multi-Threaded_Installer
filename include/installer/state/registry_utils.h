#pragma once

#include "common/config_types.h"
#include "installer/state/uninstall_record.h"
#include <string>
#include <vector>

// 本头的公开 API 不暴露任何 Win32 类型；<windows.h> 留在 .cpp 内包含。

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
                                 const std::string& displayName,
                                 const std::string& version,
                                 const std::string& installDir,
                                 const std::string& uninstallExePath,
                                 bool perMachine,
                                 const std::string& publisher = "");
bool deleteUninstallRegistryEntry(const std::string& appName, bool perMachine);
bool deleteSystemUninstallEntryByDisplayName(const std::string& displayName,
                                             UninstallEntryScope scope);
bool deleteMatchingUninstallRegistryEntries(const std::string& installDir,
                                            const std::string& uninstallExePath,
                                            bool perMachine);

} // namespace MultiThreadedInstaller
