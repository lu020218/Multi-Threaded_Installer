#pragma once

#include "common/archive_types.h"
#include "installer/pipeline/install_service.h"
#include "installer/state/uninstall_record.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

class InstallerPathResolver;

/// 本次安装相对已装状态的目标模式。
enum class InstallTargetMode {
    FreshInstall,      ///< 全新安装。
    OverwriteInstall,  ///< 覆盖安装（检出同位置旧安装）。
    UpgradeInstall,    ///< 升级安装（沿用旧安装目录 + 触发迁移）。
};

/// 安装路径决策结果。
struct InstallPathDecision {
    InstallTargetMode mode = InstallTargetMode::FreshInstall;  ///< 目标模式。
    std::string requestedInstallRoot;       ///< 请求的安装根（含环境变量展开前/后）。
    std::string resolvedInstallRoot;        ///< 最终解析出的安装根。
    std::string diskCheckPath;              ///< 磁盘空间检查用路径。
    std::string cleanupTargetInstallRoot;   ///< 旧安装清理的目标根。
};

/// 安装执行计划：BuildInstallExecutionPlan 产出，贯穿预检/清理/解压/收尾各阶段。
struct InstallExecutionPlan {
    std::string effectiveAppId;            ///< 生效的应用标识（=产品名）。
    std::string effectiveDirectoryName;    ///< 生效的目录名（=产品名）。
    bool hasPreviousInstall = false;       ///< 是否检出旧安装。
    std::string previousManifest;          ///< 旧 install.manifest.json 路径。
    std::string previousInstallDir;        ///< 旧安装目录。
    /// 旧版本号快照（优先注册表 Version，兜底 manifest appVersion）。在旧 manifest/注册表
    /// 被清理或覆盖前抓取，供迁移 fromVersion 等后续阶段消费，消除时序耦合。
    std::string previousVersion;
    /// 在旧 manifest 被清理前抓取的逐文件指纹，供解压时"零读跳过"未变文件（方案A），可空。
    std::shared_ptr<const InstalledFileFingerprintMap> previousInstalledFingerprints;
    InstallPathDecision pathDecision;      ///< 路径决策。
    std::vector<std::string> legacyDesktopShortcutCandidates;  ///< 待清理的旧桌面快捷方式名。
    std::vector<RegistryEntry> effectiveRegistry;       ///< 生效的额外注册表写入（通常为空）。
    std::vector<std::string> effectiveKillProcesses;    ///< 由产品名派生的待杀进程名。
    bool effectiveAutoStartup = false;     ///< 生效的开机自启（合并 options 覆盖/EngineDefaults）。
    bool effectiveDesktopIcons = false;    ///< 生效的桌面快捷方式。
    std::vector<std::string> selectedEmbeddedFolders;   ///< 参与安装的 folderId（全部）。
    uint64_t totalInstallBytes = 0;        ///< 安装总字节数（进度/磁盘检查用）。
};

const char* InstallTargetModeName(InstallTargetMode mode);  ///< 模式枚举 → 可读名。
/// 解析生效语言：preferredLanguage 非空则用之，否则按系统 UI 语言推断。
std::string ResolveLanguageCode(const std::string& preferredLanguage);
/// 解析桌面快捷方式显示名（重构后写死为产品名）。
std::string ResolveDesktopShortcutDisplayName(const ExtendedInstallationMetadata& metadata,
                                              const std::string& languageCode);
/// 升级模式下，从引擎写死的产品注册表探测旧安装目录与 manifest 路径。
bool ResolveUpgradeInstallFromInstallStateDetect(const ExtendedInstallationMetadata& metadata,
                                                 InstallerPathResolver& pathResolver,
                                                 std::string& installDir,
                                                 std::string& manifestPath,
                                                 std::string& error);

/// 构建安装执行计划：探测旧安装、决定路径模式、抓旧指纹、定全装 folder 集、派生注册表/进程/
/// 开机自启等生效值。失败返回 false + error。
bool BuildInstallExecutionPlan(const ExtendedInstallationMetadata& metadata,
                               InstallerPathResolver& pathResolver,
                               const InstallServiceOptions& options,
                               InstallExecutionPlan& plan,
                               std::string& error);

} // namespace MultiThreadedInstaller
