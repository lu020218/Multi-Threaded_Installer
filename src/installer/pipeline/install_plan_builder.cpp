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

// 从已解析的旧 manifest 提取待清理的桌面快捷方式名（复用调用方的同一次 DOM 解析）。
std::vector<std::string> CollectLegacyDesktopShortcutCandidates(const nlohmann::json* previousManifest) {
    std::vector<std::string> candidates;
    std::unordered_set<std::string> seen;
    seen.reserve(2);

    if (previousManifest) {
        AppendShortcutNameCandidate(candidates, seen,
                                    previousManifest->value("desktopShortcutDisplayName", ""));
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

// [旧版本-发现安装路径] 升级模式入口：必须先发现机器上已存在的旧安装目录与旧 manifest，
// 否则升级无目标可言 → 发现失败即升级失败。复用进程级探测快照（全流程只探测一次）。
bool ResolveUpgradeInstallFromInstallStateDetect(const ExtendedInstallationMetadata& metadata,
                                                 InstallerPathResolver& pathResolver,
                                                 std::string& installDir,
                                                 std::string& manifestPath,
                                                 std::string& error) {
    installDir.clear();
    manifestPath.clear();
    error.clear();

    const InstalledInstanceInfo instance = GetInstalledInstanceSnapshot(metadata, pathResolver);
    if (!instance.found) {
        error = instance.detectError.empty()
                    ? "Failed to resolve installDir from installState registry"
                    : instance.detectError;
        return false;
    }
    installDir = instance.installDir;
    manifestPath = instance.manifestPath;
    return true;
}

bool BuildInstallExecutionPlan(const ExtendedInstallationMetadata& metadata,
                               InstallerPathResolver& pathResolver,
                               const InstallServiceOptions& options,
                               InstallExecutionPlan& plan,
                               std::string& error) {
    plan = InstallExecutionPlan{};
    error.clear();

    // [旧版本-发现安装路径] 全流程唯一探测来源：进程级快照（GUI 启动/静默门控已抓取过则直接复用）。
    const InstalledInstanceInfo installedInstance =
        GetInstalledInstanceSnapshot(metadata, pathResolver);
    // 数据目录/注册表键统一用产品名，不再单列 id/directoryName（需求 §5）。
    plan.effectiveAppId = metadata.appProductName;
    plan.effectiveDirectoryName = metadata.appProductName;
    if (options.upgradeMode) {
        // 升级模式：必须已存在旧安装，否则升级失败。
        if (!installedInstance.found) {
            error = installedInstance.detectError.empty()
                        ? "Failed to resolve installDir from installState registry"
                        : installedInstance.detectError;
            return false;
        }
        plan.previousInstallDir = installedInstance.installDir;
        plan.previousManifest = installedInstance.manifestPath;
        plan.hasPreviousInstall = true;
        plan.pathDecision = ResolveUpgradePathDecision(plan.previousInstallDir);
    } else {
        // 非升级（普通/覆盖安装）：命中旧版本则记录旧目录与旧 manifest，用于覆盖安装的旧版本清理。
        plan.hasPreviousInstall = installedInstance.found;
        if (plan.hasPreviousInstall) {
            plan.previousManifest = installedInstance.manifestPath;
            plan.previousInstallDir = installedInstance.installDir;
        }

        plan.pathDecision = ResolveInstallPathDecision(pathResolver,
                                                       options.installPath,
                                                       plan.hasPreviousInstall,
                                                       plan.previousInstallDir);
    }
    // 旧版本号快照（注册表优先，manifest 兜底）：趁旧注册表/manifest 还没被本次安装
    // 清理或覆盖，先行捕获，供迁移 fromVersion 等后续阶段消费。
    plan.previousVersion = installedInstance.installedVersion;

    // 旧 manifest 只做一次全量 DOM 解析，快捷方式候选与文件指纹共用同一份解析结果
    // （此前两处各自 readManifest，全量解析两遍）。cleanup 阶段会删除旧 manifest，
    // 因此这是抓取"零读跳过"指纹数据（方案A）的唯一时机。
    nlohmann::json previousManifestJson;
    const bool hasPreviousManifestJson =
        plan.hasPreviousInstall && !plan.previousManifest.empty() &&
        readManifest(plan.previousManifest, previousManifestJson);

    plan.legacyDesktopShortcutCandidates =
        CollectLegacyDesktopShortcutCandidates(hasPreviousManifestJson ? &previousManifestJson
                                                                       : nullptr);

    if (hasPreviousManifestJson) {
        auto fingerprints = std::make_shared<InstalledFileFingerprintMap>();
        if (loadPreviousInstallFileFingerprints(previousManifestJson, *fingerprints)) {
            plan.previousInstalledFingerprints = std::move(fingerprints);
        }
    }

    // 单产品单载荷：无组件选择，始终安装全部 payload（见下方 selectedEmbeddedFolders）。
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
