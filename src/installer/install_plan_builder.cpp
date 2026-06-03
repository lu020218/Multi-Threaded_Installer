#include "installer/install_plan_builder.h"

#include "common/utf8_utils.h"
#include "installer/installed_instance_resolver.h"
#include "installer/install_manifest_store.h"
#include "installer/installer_helpers.h"
#include "installer/path_resolver.h"
#include "installer/registry_utils.h"

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

void AppendUniqueString(std::vector<std::string>& target,
                        std::unordered_set<std::string>& seen,
                        const std::string& value) {
    if (value.empty()) {
        return;
    }
    if (seen.insert(value).second) {
        target.push_back(value);
    }
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

bool BuildComponentSelectionPlan(const ExtendedInstallationMetadata& metadata,
                                 const InstallServiceOptions& options,
                                 ComponentSelectionPlan& plan,
                                 std::string& error) {
    plan = ComponentSelectionPlan{};
    if (metadata.layoutComponents.empty()) {
        return true;
    }

    plan.hasComponents = true;

    std::unordered_map<std::string, const ComponentConfig*> index;
    index.reserve(metadata.layoutComponents.size());
    for (const auto& component : metadata.layoutComponents) {
        if (!component.id.empty()) {
            index[component.id] = &component;
        }
    }

    std::unordered_set<std::string> initialSelection;
    initialSelection.reserve(metadata.layoutComponents.size());
    for (const auto& component : metadata.layoutComponents) {
        if (component.required) {
            initialSelection.insert(component.id);
        }
    }

    if (options.installAllComponents) {
        for (const auto& component : metadata.layoutComponents) {
            initialSelection.insert(component.id);
        }
    } else if (!options.selectedComponentIds.empty()) {
        for (const auto& id : options.selectedComponentIds) {
            if (index.find(id) == index.end()) {
                error = "Unknown selected component id: " + id;
                return false;
            }
            initialSelection.insert(id);
        }
    } else {
        for (const auto& component : metadata.layoutComponents) {
            if (component.defaultSelected) {
                initialSelection.insert(component.id);
            }
        }
    }

    std::unordered_set<std::string> selected;
    selected.reserve(metadata.layoutComponents.size());
    std::function<bool(const std::string&)> includeWithDependencies =
        [&](const std::string& id) -> bool {
        if (selected.find(id) != selected.end()) {
            return true;
        }
        auto it = index.find(id);
        if (it == index.end()) {
            error = "Component dependency not found: " + id;
            return false;
        }
        selected.insert(id);
        for (const auto& dep : it->second->dependsOn) {
            if (!includeWithDependencies(dep)) {
                return false;
            }
        }
        return true;
    };

    for (const auto& id : initialSelection) {
        if (!includeWithDependencies(id)) {
            return false;
        }
    }

    enum class VisitState : uint8_t { Unvisited = 0, Visiting = 1, Visited = 2 };
    std::unordered_map<std::string, VisitState> visit;
    visit.reserve(selected.size());

    std::function<bool(const std::string&)> dfs = [&](const std::string& id) -> bool {
        auto current = visit.find(id);
        if (current != visit.end()) {
            if (current->second == VisitState::Visited) {
                return true;
            }
            if (current->second == VisitState::Visiting) {
                error = "Component dependency cycle detected at: " + id;
                return false;
            }
        }
        visit[id] = VisitState::Visiting;
        auto it = index.find(id);
        if (it == index.end()) {
            error = "Component not found: " + id;
            return false;
        }
        for (const auto& dep : it->second->dependsOn) {
            if (selected.find(dep) == selected.end()) {
                continue;
            }
            if (!dfs(dep)) {
                return false;
            }
        }
        visit[id] = VisitState::Visited;
        plan.ordered.push_back(it->second);
        return true;
    };

    for (const auto& component : metadata.layoutComponents) {
        if (selected.find(component.id) == selected.end()) {
            continue;
        }
        if (!dfs(component.id)) {
            return false;
        }
    }

    std::unordered_set<std::string> seenFolders;
    seenFolders.reserve(plan.ordered.size() * 2);

    for (const auto* component : plan.ordered) {
        if (!component) {
            continue;
        }
        if (component->source.type == ComponentSourceType::EMBEDDED) {
            for (const auto& folder : component->folders) {
                AppendUniqueString(plan.embeddedFolders, seenFolders, folder);
            }
        }
    }

    return true;
}

bool ShouldInstallEmbeddedFolder(const ComponentSelectionPlan& plan,
                                 const ExtendedFolderMapping& mapping) {
    if (!plan.hasComponents) {
        return true;
    }
    return std::find(plan.embeddedFolders.begin(),
                     plan.embeddedFolders.end(),
                     mapping.folderId) != plan.embeddedFolders.end();
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
    std::string normalizedLanguage = languageCode;
    std::replace(normalizedLanguage.begin(), normalizedLanguage.end(), '-', '_');
    if (!normalizedLanguage.empty()) {
        auto exact = metadata.desktopShortcutLocalizedNames.find(normalizedLanguage);
        if (exact != metadata.desktopShortcutLocalizedNames.end() && !exact->second.empty()) {
            return exact->second;
        }

        std::string lowered = normalizedLanguage;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const auto& item : metadata.desktopShortcutLocalizedNames) {
            std::string key = item.first;
            std::replace(key.begin(), key.end(), '-', '_');
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (key == lowered && !item.second.empty()) {
                return item.second;
            }
        }
    }

    if (!metadata.desktopShortcutDefaultName.empty()) {
        return metadata.desktopShortcutDefaultName;
    }
    return metadata.appName;
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
    plan.effectiveAppId = resolveEffectiveAppId(metadata.appId, metadata.appName);
    plan.effectiveDirectoryName = metadata.appDirectoryName;
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

    if (!BuildComponentSelectionPlan(metadata, options, plan.componentPlan, error)) {
        return false;
    }

    plan.effectiveRegistry.clear();
    for (const auto& group : metadata.installerRegistryWrite) {
        for (const auto& pair : group.values) {
            RegistryEntry entry;
            entry.path = group.path;
            entry.key = pair.second.key.empty() ? pair.first : pair.second.key;
            entry.value = pair.second.value;
            entry.type = pair.second.type;
            plan.effectiveRegistry.push_back(std::move(entry));
        }
    }
    plan.effectiveKillProcesses = metadata.installKillProcesses;
    plan.effectiveAutoStartup = options.overrideAutoStartup
                                    ? options.autoStartupEnabled
                                    : metadata.installAutoStartup;
    plan.effectiveDesktopIcons = options.overrideDesktopIcons
                                     ? options.desktopIconsEnabled
                                     : metadata.installDesktopIcon;

    for (const auto& mapping : metadata.extendedPayloadMappings) {
        if (!ShouldInstallEmbeddedFolder(plan.componentPlan, mapping)) {
            continue;
        }
        plan.selectedEmbeddedFolders.push_back(mapping.folderId);
        plan.totalInstallBytes += mapping.originalSize;
    }

    return true;
}

} // namespace MultiThreadedInstaller
