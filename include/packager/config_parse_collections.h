#pragma once

#include "common/config_types.h"

#include <json.hpp>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

bool ParseRegistryEntryArray(const nlohmann::json& arrayValue,
                             const std::string& fieldLabel,
                             std::vector<RegistryEntry>& out,
                             std::string& lastError);

bool ParseCleanupRuleArray(const nlohmann::json& arrayValue,
                           const std::string& fieldLabel,
                           std::vector<UninstallCleanupRule>& out,
                           std::string& lastError);

}  // namespace MultiThreadedInstaller
