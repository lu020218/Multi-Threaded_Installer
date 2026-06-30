#pragma once

#include "common/archive_types.h"
#include "installer/state/uninstall_record.h"

#include <json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace MultiThreadedInstaller {

/// 从上次安装 manifest 读出的、升级时需沿用的用户选项。
struct PreviousInstallOptions {
    bool autoStartup = false;     ///< 上次的开机自启选择。
    bool desktopIcon = false;     ///< 上次的桌面快捷方式选择。
    std::string languageCode;     ///< 上次的界面语言。
};

// 写运行时安装清单 install.manifest.json：记录已装文件 + 运行时卸载账本
// （actualCleanupSnapshot），卸载时据此撤销。清理规则/系统卸载入口由引擎写死，
// 不再来自 YAML——因此签名收窄，去掉了 installState/uninstallerCleanup/systemUninstallEntry 等配置型。
bool writeManifest(const std::string& manifestPath,
                   const std::string& appId,
                   const std::string& displayName,
                   const std::string& appVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& cleanupRoots,
                   const UninstallCleanupConfig& actualCleanupSnapshot,
                   const std::vector<std::string>& filePaths,
                   const std::vector<std::string>& killProcesses,
                   bool autoStartup,
                   bool desktopIcons,
                   const std::string& desktopShortcutDisplayName,
                   const std::string& uninstallPath,
                   const std::string& languageCode,
                   const std::string& appPublisher = {},
                   const InstalledFileFingerprintMap& fileFingerprints = {},
                   const std::vector<std::string>& installedComponentIds = {});
/// 读取 install.manifest.json 为 JSON 对象。成功返回 true。
bool readManifest(const std::string& manifestPath, nlohmann::json& outManifest);
/// 从上次安装 manifest 读出 PreviousInstallOptions（升级时沿用）。缺字段时返回 false + error。
bool loadPreviousInstallOptions(const std::string& manifestPath,
                                PreviousInstallOptions& options,
                                std::string& error);
/// 读取上次安装记录的逐文件指纹（若有），键为 normalizePathForCompare(绝对路径)。
/// 用于升级时"零读跳过"未变文件（方案A）。manifest 不可读或无指纹时返回 false。
bool loadPreviousInstallFileFingerprints(const std::string& manifestPath,
                                         InstalledFileFingerprintMap& out);

} // namespace MultiThreadedInstaller
