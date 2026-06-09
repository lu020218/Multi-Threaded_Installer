#pragma once

#include "installer/app/console_interface.h"
#include "installer/platform/path_resolver.h"
#include "common/archive_types.h"

#include <functional>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/// 卸载进度信息。
struct UninstallProgressInfo {
    float progress = 0.0f;     ///< 进度 [0..1]。
    std::string currentItem;   ///< 当前处理项（文件/键名等）。
};

using UninstallProgressCallback = std::function<void(const UninstallProgressInfo&)>;  ///< 卸载进度回调。

/// 卸载上下文：定位到要卸载的实例（清单路径/安装目录/身份）及是否可走兜底清理。
struct UninstallContext {
    std::string manifestPath;        ///< install.manifest.json 路径。
    std::string installDir;          ///< 安装目录。
    std::string appId;               ///< 应用标识（=产品名）。
    std::string appName;             ///< 产品名。
    bool manifestReadable = false;   ///< manifest 是否可读（决定走清单驱动卸载还是兜底）。
    bool fallbackAllowed = false;    ///< manifest 缺失时是否允许安全目录兜底清理。
    std::string detectSource;        ///< 实例检出来源（日志用）。
    std::string errorMessage;        ///< 解析失败原因。
};

/// 解析卸载上下文：依次尝试显式清单路径 → exe 同目录清单 → 注册表探测 → 安全目录兜底。
/// @return manifest 可读时返回 true 并填充 context；否则按 fallbackAllowed/errorMessage 指示。
bool ResolveUninstallContext(const ExtendedInstallationMetadata* metadata,
                             InstallerPathResolver& resolver,
                             const std::string& explicitManifestPath,
                             UninstallContext& context);
/// 按已解析的上下文执行卸载（清单可读走清单驱动，否则走安全目录兜底）。
bool ExecuteUninstallFromContext(const UninstallContext& context,
                                 const ExtendedInstallationMetadata* metadata,
                                 InstallerPathResolver& resolver,
                                 CliSupport& console,
                                 const UninstallProgressCallback& progressCallback = {},
                                 const std::function<bool()>& cancellationCallback = {});
/// 按 manifest 执行卸载（结束进程 → 撤销快捷方式/启动项/卸载入口/注册表 → 删文件 → 清状态 → 自删）。
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console);
/// 同上，带进度/取消回调。
bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console,
                           const UninstallProgressCallback& progressCallback,
                           const std::function<bool()>& cancellationCallback = {});

} // namespace MultiThreadedInstaller
