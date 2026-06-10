#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// 模板定位/加载：找到与 packager.exe 同目录的 installer/uninstaller 模板 exe 并读入内存。

std::filesystem::path GetPackagerExecutableDirectory();      ///< packager.exe 所在目录。
std::filesystem::path GetDefaultInstallerTemplatePath();     ///< 默认 installer 模板路径。
std::filesystem::path GetDefaultUninstallerTemplatePath();   ///< 默认 uninstaller 模板路径。
/// 读取模板 exe 字节到 outTemplate。失败返回 false + error。
bool LoadInstallerTemplate(const std::filesystem::path& templatePath,
                           std::vector<uint8_t>& outTemplate,
                           std::string& error);

} // namespace MultiThreadedInstaller
