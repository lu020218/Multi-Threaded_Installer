#include "installer/pipeline/install_plan_builder.h"

#include "common/engine_defaults.h"
#include "common/utf8_utils.h"
#include "installer/state/installed_instance_resolver.h"
#include "installer/state/install_manifest_store.h"
#include "installer/platform/installer_helpers.h"
#include "installer/platform/path_resolver.h"
#include "installer/state/registry_utils.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace MultiThreadedInstaller {

namespace {

std::string TrimAsciiCopy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

InstallPathDecision ResolveInstallPathDecision(InstallerPathResolver& pathResolver,
                                               const std::string& requestedInstallPath,
                                               bool hasPreviousInstall,
                                               const std::string& previousInstallDir) {
    InstallPathDecision decision;
    decision.requestedInstallRoot = pathResolver.expandEnvironmentVariables(requestedInstallPath);
    decision.resolvedInstallRoot = decision.requestedInstallRoot;

    if (hasPreviousInstall && !previousInstallDir.empty()) {
        decision.mode = InstallTargetMode::OverwriteInstall;
        decision.cleanupTargetInstallRoot = previousInstallDir;
    }

    decision.diskCheckPath =
        decision.resolvedInstallRoot.empty() ? decision.requestedInstallRoot : decision.resolvedInstallRoot;
    if (decision.cleanupTargetInstallRoot.empty()) {
        decision.cleanupTargetInstallRoot = decision.resolvedInstallRoot;
    }
    return decision;
}

InstallPathDecision ResolveUpgradePathDecision(const std::string& previousInstallDir) {
    InstallPathDecision decision;
    decision.mode = InstallTargetMode::UpgradeInstall;
    decision.requestedInstallRoot = previousInstallDir;
    decision.resolvedInstallRoot = previousInstallDir;
    decision.diskCheckPath = previousInstallDir;
    decision.cleanupTargetInstallRoot = previousInstallDir;
    return decision;
}

void AppendShortcutNameCandidate(std::vector<std::string>& target,
                                 std::unordered_set<std::string>& seen,
                                 const std::string& value) {
    std::string trimmed = TrimAsciiCopy(value);
    if (trimmed.empty()) {
        return;
    }
    std::string lowered = trimmed;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (seen.insert(lowered).second) {
        target.push_back(std::move(trimmed));
    }
}

std::vector<std::string> CollectLegacyDesktopShortcutCandidates(const std::string& previousManifest) {
    std::vector<std::string> candidates;
    std::unordered_set<std::string> seen;
    seen.reserve(2);

    nlohmann::json manifest;
    if (readManifest(previousManifest, manifest)) {
        AppendShortcutNameCandidate(candidates, seen, manifest.value("desktopShortcutDisplayName", ""));
    }

    return candidates;
}

} // namespace

const char* InstallTargetModeName(InstallTargetMode mode) {
    switch (mode) {
        case InstallTargetMode::FreshInstall:
            return "FreshInstall";
        case InstallTargetMode::OverwriteInstall:
            return "OverwriteInstall";
        case InstallTargetMode::UpgradeInstall:
            return "UpgradeInstall";
        default:
            return "FreshInstall";
    }
}

std::string ResolveLanguageCode(const std::string& preferredLanguage) {
    if (!preferredLanguage.empty()) {
        return preferredLanguage;
    }

    LANGID langId = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(langId)) {
        case LANG_CHINESE:
            return "zh_CN";
        case LANG_ENGLISH:
            return "en_US";
        case LANG_JAPANESE:
            return "ja_JP";
        case LANG_KOREAN:
            return "ko_KR";
        case LANG_SPANISH:
            return "es_ES";
        case LANG_FRENCH:
            return "fr_FR";
        default:
            return "en_US";
    }
}

std::string ResolveDesktopShortcutDisplayName(const ExtendedInstallationMetadata& metadata,
                                              const std::string& languageCode) {
    // 快捷方式名写死为产品名（需求 §5：UI 文案/绑定下沉引擎+skin，不进 manifest）。
    (void)languageCode;
    return metadata.appProductName;
}

bool ResolveUpgradeInstallFromInstallStateDetect(const ExtendedInstallationMetadata& metadata,
                                                 InstallerPathResolver& pathResolver,
                                                 std::string& installDir,
                                                 std::string& manifestPath,
                                                 std::string& error) {
    installDir.clear();
    manifestPath.clear();
    error.clear();

    std::string detectSource;
    if (!resolveInstallDirFromInstallStateStore(metadata, pathResolver, installDir, manifestPath, detectSource, error)) {
        return false;
    }

    return true;
}

bool BuildInstallExecutionPlan(const ExtendedInstallationMetadata& metadata,
                               InstallerPathResolver& pathResolver,
                               const InstallServiceOptions& options,
                               InstallExecutionPlan& plan,
                               std::string& error) {
    plan = InstallExecutionPlan{};
    error.clear();

    InstalledInstanceInfo installedInstance;
    // 数据目录/注册表键统一用产品名，不再单列 id/directoryName（需求 §5）。
    plan.effectiveAppId = metadata.appProductName;
    plan.effectiveDirectoryName = metadata.appProductName;
    if (options.upgradeMode) {
        if (!ResolveUpgradeInstallFromInstallStateDetect(metadata,
                                                         pathResolver,
                                                         plan.previousInstallDir,
                                                         plan.previousManifest,
                                                         error)) {
            return false;
        }
        plan.hasPreviousInstall = true;
        plan.pathDecision = ResolveUpgradePathDecision(plan.previousInstallDir);
    } else {
        plan.hasPreviousInstall = resolveInstalledInstanceFromInstallState(metadata,
                                                                          pathResolver,
                                                                          installedInstance,
                                                                          nullptr);
        if (plan.hasPreviousInstall) {
            plan.previousManifest = installedInstance.manifestPath;
            plan.previousInstallDir = installedInstance.installDir;
        }

        plan.pathDecision = ResolveInstallPathDecision(pathResolver,
                                                       options.installPath,
                                                       plan.hasPreviousInstall,
                                                       plan.previousInstallDir);
    }

    plan.legacyDesktopShortcutCandidates =
        CollectLegacyDesktopShortcutCandidates(plan.previousManifest);

    // Capture the previous install's per-file fingerprints now, while its
    // manifest still exists. The cleanup phase deletes the old manifest before
    // extraction runs, so this is the only point at which the zero-read skip
    // data (Scheme A) can be read.
    if (plan.hasPreviousInstall && !plan.previousManifest.empty()) {
        auto fingerprints = std::make_shared<InstalledFileFingerprintMap>();
        if (loadPreviousInstallFileFingerprints(plan.previousManifest, *fingerprints)) {
            plan.previousInstalledFingerprints = std::move(fingerprints);
        }
    }

    // 单产品单载荷：无组件选择，始终安装全部 payload。
    plan.componentPlan = ComponentSelectionPlan{};

    // 注册表写入由引擎在 finalize 阶段统一处理（产品注册表 + 系统卸载入口），
    // 不再有 YAML 驱动的通用 registry 名单（需求 §4.6）。
    plan.effectiveRegistry.clear();
    // killProcesses 由产品名派生主进程名（需求 §5）；特殊进程随迁移处理。
    plan.effectiveKillProcesses = {metadata.appProductName + ".exe"};
    plan.effectiveAutoStartup = options.overrideAutoStartup
                                    ? options.autoStartupEnabled
                                    : EngineDefaults::kDefaultAutoStartup;
    plan.effectiveDesktopIcons = options.overrideDesktopIcons
                                     ? options.desktopIconsEnabled
                                     : EngineDefaults::kDefaultDesktopShortcut;

    for (const auto& mapping : metadata.extendedPayloadMappings) {
        plan.selectedEmbeddedFolders.push_back(mapping.folderId);
        plan.totalInstallBytes += mapping.originalSize;
    }

    return true;
}

} // namespace MultiThreadedInstaller
