#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/// 将 GUI 资源目录（skins/images/lang/license 等）打成内存 zip 字节流，
/// 供后续作为 RES_ZIP PE 资源嵌入安装器 exe。失败返回 false + error。
bool BuildResourceZip(const std::filesystem::path& resourceDir,
                      std::vector<uint8_t>& outZip,
                      std::string& error);

} // namespace MultiThreadedInstaller
