#pragma once

#include <json.hpp>
#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MultiThreadedInstaller {

std::string ToLowerCopy(std::string value);

bool JsonValueToString(const nlohmann::json& value, std::string& out);
bool JsonValueToBool(const nlohmann::json& value, bool& out);
bool JsonValueToUInt64(const nlohmann::json& value, uint64_t& out);
bool JsonValueToInt(const nlohmann::json& value, int& out);
bool JsonArrayToStringList(const nlohmann::json& arrayValue, std::vector<std::string>& outList);
bool JsonObjectToStringMap(
    const nlohmann::json& objectValue,
    std::unordered_map<std::string, std::string>& outMap);

bool IsStructuredConfigSchema(const nlohmann::json& root);
nlohmann::json NormalizeStructuredConfigSchema(const nlohmann::json& root);
nlohmann::json ParseYamlScalar(const std::string& scalar);
nlohmann::json YamlNodeToJson(const YAML::Node& node);

}  // namespace MultiThreadedInstaller
