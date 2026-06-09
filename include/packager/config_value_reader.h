#pragma once

#include <json.hpp>
#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MultiThreadedInstaller {

// 配置读取辅助：YAML → JSON 归一，以及从 JSON 安全取各类型值（容忍类型差异，失败返回 false）。

std::string ToLowerCopy(std::string value);  ///< 返回小写副本。

bool JsonValueToString(const nlohmann::json& value, std::string& out);  ///< 取字符串。
bool JsonValueToBool(const nlohmann::json& value, bool& out);           ///< 取布尔（容忍 "true"/1 等）。
bool JsonValueToUInt64(const nlohmann::json& value, uint64_t& out);     ///< 取无符号 64 位整数。
bool JsonValueToInt(const nlohmann::json& value, int& out);            ///< 取整数。
bool JsonArrayToStringList(const nlohmann::json& arrayValue, std::vector<std::string>& outList);  ///< 取字符串数组。
bool JsonObjectToStringMap(
    const nlohmann::json& objectValue,
    std::unordered_map<std::string, std::string>& outMap);  ///< 取字符串→字符串映射。

nlohmann::json ParseYamlScalar(const std::string& scalar);     ///< 解析 YAML 标量为合适的 JSON 类型。
nlohmann::json YamlNodeToJson(const YAML::Node& node);         ///< 递归把 YAML 节点转为 JSON。

}  // namespace MultiThreadedInstaller
