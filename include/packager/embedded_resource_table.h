#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

bool HasEmbeddedResourceTable(const std::vector<uint8_t>& installerTemplate);
bool AppendEmbeddedFileEntry(std::vector<uint8_t>& installerTemplate,
                             const std::string& name,
                             const std::filesystem::path& filePath);
bool AppendEmbeddedRawEntry(std::vector<uint8_t>& installerTemplate,
                            const std::string& name,
                            const std::vector<uint8_t>& data);
void AppendEmbeddedResourceMagic(std::vector<uint8_t>& installerTemplate);

} // namespace MultiThreadedInstaller
