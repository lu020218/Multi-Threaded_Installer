#pragma once

#include "packager/version_info_updater.h"

#include <filesystem>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// Build resources.zip from resourceDir and inject it — plus the uninstaller binary
// (which itself carries RES_ZIP) — into the installer template exe as native PE
// resources of type "BINARY" (names "RES_ZIP" / "UNINSTALLER_EXE").
//
// Must be called on the temp template FILE before any data overlay (metadata/payload)
// is appended: UpdateResource rewrites the PE resource section and does not tolerate a
// trailing overlay. The runtime reads these back via FindResourceA(name, "BINARY").
bool EmbedInstallerPeResources(const std::filesystem::path& installerTemplateExe,
                               const std::filesystem::path& resourceDir,
                               const std::filesystem::path& uninstallerTemplateExe,
                               std::string& error);

// 单会话编排：把「清单执行级别 + 图标(可选) + 版本资源 + RES_ZIP + 卸载器」全部在同一个
// 带重试的资源更新会话里写入安装器模板 exe——只重写一次 exe（原先 4~5 次），大幅降低杀软
// 间歇性锁文件导致的打包失败概率。iconPath 为空则跳过图标；图标/版本失败仅计入 warnings
// （非致命），清单/RES_ZIP/卸载器失败则整体失败。必须在追加 overlay 之前调用。
bool EmbedAllInstallerPeResources(const std::filesystem::path& installerTemplateExe,
                                  bool requireAdmin,
                                  const std::string& iconPath,
                                  const VersionInfoData& versionInfo,
                                  const std::filesystem::path& resourceDir,
                                  const std::filesystem::path& uninstallerTemplateExe,
                                  std::string& error,
                                  std::vector<std::string>& warnings);

} // namespace MultiThreadedInstaller
