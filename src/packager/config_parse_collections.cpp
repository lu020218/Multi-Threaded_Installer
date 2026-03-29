#include "packager/config_parse_collections.h"

#include "packager/config_value_reader.h"

namespace MultiThreadedInstaller {

bool ParseRegistryEntryArray(const nlohmann::json& arrayValue,
                             const std::string& fieldLabel,
                             std::vector<RegistryEntry>& out,
                             std::string& lastError) {
    if (!arrayValue.is_array()) {
        lastError = "Invalid field '" + fieldLabel + "': expected array";
        return false;
    }
    out.clear();
    for (const auto& entry : arrayValue) {
        if (!entry.is_object()) {
            lastError = "Invalid field '" + fieldLabel + "[]': expected object";
            return false;
        }

        RegistryEntry reg;
        if (entry.contains("path") && !JsonValueToString(entry["path"], reg.path)) {
            lastError = "Invalid field '" + fieldLabel + "[].path': expected string";
            return false;
        }
        if (entry.contains("key") && !JsonValueToString(entry["key"], reg.key)) {
            lastError = "Invalid field '" + fieldLabel + "[].key': expected string";
            return false;
        }
        if (entry.contains("value")) {
            if (entry["value"].is_number_integer() || entry["value"].is_number_unsigned()) {
                reg.type = RegistryValueType::DWORD;
                reg.value = std::to_string(entry["value"].get<uint32_t>());
            } else if (!JsonValueToString(entry["value"], reg.value)) {
                lastError = "Invalid field '" + fieldLabel + "[].value': expected string or integer";
                return false;
            }
        }
        if (entry.contains("type")) {
            std::string type;
            if (!JsonValueToString(entry["type"], type)) {
                lastError = "Invalid field '" + fieldLabel + "[].type': expected string";
                return false;
            }
            type = ToLowerCopy(type);
            if (type == "dword") {
                reg.type = RegistryValueType::DWORD;
            } else if (type == "expand" || type == "expand_string") {
                reg.type = RegistryValueType::EXPAND_STRING;
            } else {
                reg.type = RegistryValueType::STRING;
            }
        }
        out.push_back(std::move(reg));
    }
    return true;
}

bool ParseCleanupRuleArray(const nlohmann::json& arrayValue,
                           const std::string& fieldLabel,
                           std::vector<UninstallCleanupRule>& out,
                           std::string& lastError) {
    if (!arrayValue.is_array()) {
        lastError = "Invalid field '" + fieldLabel + "': expected array";
        return false;
    }
    out.clear();
    for (const auto& item : arrayValue) {
        if (!item.is_object()) {
            lastError = "Invalid field '" + fieldLabel + "[]': expected object";
            return false;
        }
        UninstallCleanupRule rule;
        if (!item.contains("path") ||
            !JsonValueToString(item["path"], rule.path) ||
            rule.path.empty()) {
            lastError = "Invalid field '" + fieldLabel + "[].path': expected non-empty string";
            return false;
        }
        if (item.contains("recursive") &&
            !JsonValueToBool(item["recursive"], rule.recursive)) {
            lastError = "Invalid field '" + fieldLabel + "[].recursive': expected boolean";
            return false;
        }
        if (item.contains("onlyIfEmpty") &&
            !JsonValueToBool(item["onlyIfEmpty"], rule.onlyIfEmpty)) {
            lastError = "Invalid field '" + fieldLabel + "[].onlyIfEmpty': expected boolean";
            return false;
        }
        out.push_back(std::move(rule));
    }
    return true;
}

}  // namespace MultiThreadedInstaller
