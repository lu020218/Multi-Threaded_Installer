#pragma once

#include "common/archive_types.h"
#include "installer/state/uninstall_record.h"

#include <json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace MultiThreadedInstaller {

struct ComponentExecutionRecord {
    std::string componentId;
    std::string sourceType;
    std::string uninstallCommand;
    std::string workingDirectory;
    bool wait = true;
    uint32_t timeoutSec = 900;
};

struct PreviousInstallOptions {
    bool autoStartup = false;
    bool desktopIcon = false;
    bool installAllComponents = false;
    std::string languageCode;
    std::vector<std::string> selectedComponentIds;
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
                   const std::vector<ComponentExecutionRecord>& componentActions = {},
                   const std::vector<std::string>& selectedComponentIds = {},
                   bool installAllComponents = false,
                   const std::string& appPublisher = {},
                   const InstalledFileFingerprintMap& fileFingerprints = {});
bool readManifest(const std::string& manifestPath, nlohmann::json& outManifest);
bool loadPreviousInstallOptions(const std::string& manifestPath,
                                PreviousInstallOptions& options,
                                std::string& error);
// Loads per-file fingerprints recorded by a previous install (if present),
// keyed by normalizePathForCompare(absolute path). Returns false when the
// manifest is unreadable or carries no fingerprints.
bool loadPreviousInstallFileFingerprints(const std::string& manifestPath,
                                         InstalledFileFingerprintMap& out);

} // namespace MultiThreadedInstaller
