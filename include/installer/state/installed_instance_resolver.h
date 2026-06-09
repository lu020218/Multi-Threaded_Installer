#pragma once

#include "common/archive_types.h"

#include <string>

namespace MultiThreadedInstaller {

class InstallerPathResolver;

/// 已安装实例的探测结果。
struct InstalledInstanceInfo {
    bool found = false;            ///< 是否检出已安装实例。
    std::string installDir;        ///< 旧安装目录。
    std::string manifestPath;      ///< 旧 install.manifest.json 路径（可能为空）。
    std::string installedVersion;  ///< 旧版本号（自 manifest 读出）。
    std::string detectSource;      ///< 检出来源（用于日志/诊断）。
};

/// 预留接口：从安装根推断已装实例（当前实现恒返回 false）。
bool resolveInstalledInstanceFromInstallRoots(const ExtendedInstallationMetadata& metadata,
                                              InstalledInstanceInfo& instanceInfo);

/// 主探测入口：读引擎写死的产品注册表（HKLM\Software\<product>）定位已装实例并读版本。
/// @return 检出返回 true 并填充 instanceInfo；未检出返回 false，error（若提供）含原因。
bool resolveInstalledInstanceFromInstallState(const ExtendedInstallationMetadata& metadata,
                                              InstallerPathResolver& resolver,
                                              InstalledInstanceInfo& instanceInfo,
                                              std::string* error = nullptr);

/// 从产品注册表读 InstallDir（及同目录下 install.manifest.json）。供探测/升级路径复用。
bool resolveInstallDirFromInstallStateStore(const ExtendedInstallationMetadata& metadata,
                                            InstallerPathResolver& resolver,
                                            std::string& installDir,
                                            std::string& manifestPath,
                                            std::string& detectSource,
                                            std::string& error);

/// 从指定注册表路径/键读出 installDir，并探测其下的 install.manifest.json。
bool resolveInstallInfoFromRegistry(const std::string& registryPath,
                                    const std::string& registryKey,
                                    std::string& manifestPath,
                                    std::string& installDir);

} // namespace MultiThreadedInstaller
