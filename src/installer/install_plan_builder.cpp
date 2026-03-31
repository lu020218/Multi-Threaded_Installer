#include "installer/install_plan_builder.h"

#include "installer/installer_helpers.h"
#include "installer/path_resolver.h"
#include "installer/uninstall_manager.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <functional>
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
                                               const std::string& effectiveDirectoryName,
                                               bool installDirectoryAppendName,
                                               bool installPathExplicit,
                                               bool cleanupOldInstallRequested,
                                               bool hasPreviousInstall,
                                               const std::string& previousInstallDir,
                                               bool repairMode) {
    InstallPathDecision decision;
    decision.requestedInstallRoot = pathResolver.resolveFinalPath(
        requestedInstallPath,
        SpecialDirectoryType::INSTALL_DIRECTORY,
        effectiveDirectoryName,
        installDirectoryAppendName);
    decision.resolvedInstallRoot = decision.requestedInstallRoot;

    if (repairMode) {
        decision.mode = InstallTargetMode::Repair;
        if (!previousInstallDir.empty()) {
            decision.resolvedInstallRoot = previousInstallDir;
        }
    } else if (hasPreviousInstall &&
               !installPathExplicit &&
               !cleanupOldInstallRequested &&
               !previousInstallDir.empty()) {
        decision.mode = InstallTargetMode::UpgradeMigration;
        decision.resolvedInstallRoot = previousInstallDir;
    }

    decision.diskCheckPath =
        decision.resolvedInstallRoot.empty() ? requestedInstallPath : decision.resolvedInstallRoot;
    decision.cleanupTargetInstallRoot = decision.resolvedInstallRoot;
    if (installPathExplicit || cleanupOldInstallRequested) {
        decision.cleanupTargetInstallRoot = decision.requestedInstallRoot;
    }
    decision.shortcutCleanupTargetRoot =
        decision.requestedInstallRoot.empty() ? decision.resolvedInstallRoot : decision.requestedInstallRoot;
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

void AppendUniqueRegistry(std::vector<RegistryEntry>& target,
                          std::unordered_set<std::string>& seen,
                          const RegistryEntry& entry) {
    std::string key = entry.path;
    key.push_back('\n');
    key += entry.key;
    key.push_back('\n');
    key += entry.value;
    key.push_back('\n');
    key += std::to_string(static_cast<int>(entry.type));
    if (seen.insert(key).second) {
        target.push_back(entry);
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

std::vector<std::string> CollectLegacyDesktopShortcutCandidates(
    const ExtendedInstallationMetadata& metadata,
    const std::string& previousManifest) {
    std::vector<std::string> candidates;
    std::unordered_set<std::string> seen;
    seen.reserve(metadata.compatibilityLegacyDesktopShortcutNames.size() + 2);

    nlohmann::json manifest;
    if (readManifest(previousManifest, manifest)) {
        AppendShortcutNameCandidate(candidates, seen, manifest.value("desktopShortcutDisplayName", ""));
    }
    for (const auto& legacyName : metadata.compatibilityLegacyDesktopShortcutNames) {
        AppendShortcutNameCandidate(candidates, seen, legacyName);
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
    std::unordered_set<std::string> seenKillProcesses;
    std::unordered_set<std::string> seenRegistry;
    seenFolders.reserve(plan.ordered.size() * 2);
    seenKillProcesses.reserve(plan.ordered.size() * 2);
    seenRegistry.reserve(plan.ordered.size() * 2);

    for (const auto* component : plan.ordered) {
        if (!component) {
            continue;
        }
        if (component->source.type == ComponentSourceType::EMBEDDED) {
            for (const auto& folder : component->folders) {
                AppendUniqueString(plan.embeddedFolders, seenFolders, folder);
            }
        }
        for (const auto& name : component->killProcesses) {
            AppendUniqueString(plan.killProcesses, seenKillProcesses, name);
        }
        for (const auto& entry : component->registry) {
            AppendUniqueRegistry(plan.registryEntries, seenRegistry, entry);
        }
        plan.autoStartup = plan.autoStartup || component->autoStartup;
        plan.desktopIcons = plan.desktopIcons || component->createDesktopShortcut;
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
        case InstallTargetMode::UpgradeMigration:
            return "UpgradeMigration";
        case InstallTargetMode::Repair:
            return "Repair";
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

bool BuildInstallExecutionPlan(const ExtendedInstallationMetadata& metadata,
                               InstallerPathResolver& pathResolver,
                               const InstallServiceOptions& options,
                               InstallExecutionPlan& plan,
                               std::string& error) {
    plan = InstallExecutionPlan{};
    error.clear();

    plan.effectiveAppId = resolveEffectiveAppId(metadata.appId, metadata.appName);
    plan.effectiveDirectoryName =
        resolveEffectiveDirectoryName(metadata.appDirectoryName, metadata.appName);
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
            plan.installDirectoryAppendName = mapping.appendDirectoryName;
            break;
        }
    }

    const std::vector<std::string> identityCandidates =
        buildIdentityCandidates(metadata.appId, metadata.compatibilityLegacyAppIds, metadata.appName);
    plan.hasPreviousInstall = resolveExistingInstallInfo(identityCandidates,
                                                         pathResolver,
                                                         plan.previousManifest,
                                                         plan.previousInstallDir,
                                                         nullptr);

    plan.pathDecision = ResolveInstallPathDecision(pathResolver,
                                                   options.installPath,
                                                   plan.effectiveDirectoryName,
                                                   plan.installDirectoryAppendName,
                                                   options.installPathExplicit,
                                                   options.cleanupOldInstallRequested,
                                                   plan.hasPreviousInstall,
                                                   plan.previousInstallDir,
                                                   options.repairMode);

    const std::string normalizedPreviousInstallRoot = normalizePathForCompare(plan.previousInstallDir);
    const std::string normalizedShortcutCleanupTarget = normalizePathForCompare(
        plan.pathDecision.shortcutCleanupTargetRoot.empty()
            ? plan.pathDecision.resolvedInstallRoot
            : plan.pathDecision.shortcutCleanupTargetRoot);
    const bool shouldCleanupLegacyDesktopShortcuts =
        plan.hasPreviousInstall &&
        !normalizedPreviousInstallRoot.empty() &&
        !normalizedShortcutCleanupTarget.empty() &&
        (normalizedPreviousInstallRoot == normalizedShortcutCleanupTarget ||
         metadata.installAutoCleanOldInstall ||
         options.cleanupOldInstallRequested);
    if (shouldCleanupLegacyDesktopShortcuts) {
        plan.legacyDesktopShortcutCandidates =
            CollectLegacyDesktopShortcutCandidates(metadata, plan.previousManifest);
    }

    if (!BuildComponentSelectionPlan(metadata, options, plan.componentPlan, error)) {
        return false;
    }

    plan.effectiveRegistry = metadata.lifecycleInstallRegistry;
    std::unordered_set<std::string> registrySeen;
    registrySeen.reserve(plan.effectiveRegistry.size() + metadata.layoutComponents.size() * 2);
    for (const auto& entry : plan.effectiveRegistry) {
        std::string key = entry.path;
        key.push_back('\n');
        key += entry.key;
        key.push_back('\n');
        key += entry.value;
        key.push_back('\n');
        key += std::to_string(static_cast<int>(entry.type));
        registrySeen.insert(key);
    }

    plan.effectiveKillProcesses = metadata.installKillProcesses;
    std::unordered_set<std::string> processSeen;
    processSeen.reserve(plan.effectiveKillProcesses.size() + plan.componentPlan.killProcesses.size());
    for (const auto& process : plan.effectiveKillProcesses) {
        processSeen.insert(process);
    }
    for (const auto& process : plan.componentPlan.killProcesses) {
        AppendUniqueString(plan.effectiveKillProcesses, processSeen, process);
    }

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
