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
    std::string installedVersion;  ///< 旧版本号（优先注册表 Version，缺失时退回 manifest appVersion）。
    std::string detectSource;      ///< 检出来源（用于日志/诊断）。
    std::string detectError;       ///< 未检出时的原因（found=false 时有效）。
};

/// 主探测入口：读引擎写死的产品注册表（HKLM\Software\<product>）定位已装实例并读版本。
/// @return 检出返回 true 并填充 instanceInfo；未检出返回 false，error（若提供）含原因。
bool resolveInstalledInstanceFromInstallState(const PackageManifest& metadata,
                                              InstallerPathResolver& resolver,
                                              InstalledInstanceInfo& instanceInfo,
                                              std::string* error = nullptr);

/// [旧版本-发现安装路径] 进程级快照：整个安装/卸载流程只做一次实际探测（注册表+目录校验+版本），
/// 之后所有调用（GUI 启动、静默门控、路径解析、安装计划、卸载定位）复用同一份结果，
/// 消除重复读取与各次探测间的状态不一致（TOCTOU）。快照在首次调用时抓取。
InstalledInstanceInfo GetInstalledInstanceSnapshot(const PackageManifest& metadata,
                                                   InstallerPathResolver& resolver);

/// 从产品注册表读 InstallDir（及同目录下 install.manifest.json）。供探测/升级路径复用。
bool resolveInstallDirFromInstallStateStore(const PackageManifest& metadata,
                                            InstallerPathResolver& resolver,
                                            std::string& installDir,
                                            std::string& manifestPath,
                                            std::string& detectSource,
                                            std::string& error);

} // namespace MultiThreadedInstaller
