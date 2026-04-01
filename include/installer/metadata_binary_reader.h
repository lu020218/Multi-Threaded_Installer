#pragma once

#include "common/config_types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace MultiThreadedInstaller {

bool IsSupportedMetadataVersion(uint32_t version);

template <typename T>
bool ReadPod(const std::vector<uint8_t>& data, size_t& offset, T& out) {
    if (offset + sizeof(T) > data.size()) {
        return false;
    }
    std::memcpy(&out, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

bool ReadString(const std::vector<uint8_t>& data,
                size_t& offset,
                std::string& out,
                const char* label);

bool ReadStringList(const std::vector<uint8_t>& data,
                    size_t& offset,
                    std::vector<std::string>& out,
                    const char* label);

bool ReadStringMap(const std::vector<uint8_t>& data,
                   size_t& offset,
                   std::unordered_map<std::string, std::string>& out,
                   const char* label);

bool ReadRegistryList(const std::vector<uint8_t>& data,
                      size_t& offset,
                      std::vector<RegistryEntry>& out,
                      const char* label);

bool ReadComponentList(const std::vector<uint8_t>& data,
                       size_t& offset,
                       std::vector<ComponentConfig>& out);

bool ReadComponentUiConfig(const std::vector<uint8_t>& data,
                           size_t& offset,
                           UiComponentSelectionConfig& out);

bool ReadUiLinks(const std::vector<uint8_t>& data,
                 size_t& offset,
                 std::vector<UiLinkBinding>& out);

bool ReadCleanupRules(const std::vector<uint8_t>& data,
                      size_t& offset,
                      std::vector<UninstallCleanupRule>& out);

}  // namespace MultiThreadedInstaller
