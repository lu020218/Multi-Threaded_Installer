#include "installer/install_manifest_store.h"

#include "installer/file_system_operator.h"
#include "installer/installer_helpers.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"

#include <filesystem>
#include <fstream>

namespace MultiThreadedInstaller {

using json = nlohmann::json;

#ifdef _WIN32
namespace {

bool IsValidUtf8(const std::string& text) {
    size_t i = 0;
    const size_t len = text.size();
    while (i < len) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c <= 0x7F) {
            ++i;
        } else if ((c >> 5) == 0x6) {
            if (i + 1 >= len) return false;
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            if ((c1 >> 6) != 0x2) return false;
            i += 2;
        } else if ((c >> 4) == 0xE) {
            if (i + 2 >= len) return false;
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2) return false;
            i += 3;
        } else if ((c >> 3) == 0x1E) {
            if (i + 3 >= len) return false;
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            unsigned char c3 = static_cast<unsigned char>(text[i + 3]);
            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2 || (c3 >> 6) != 0x2) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

std::string EnsureUtf8(const std::string& text) {
    if (text.empty()) {
        return text;
    }
    if (IsValidUtf8(text)) {
        return text;
    }
    std::string utf8 = AcpToUtf8(text);
    return utf8.empty() ? text : utf8;
}

}  // namespace
#else
namespace {
std::string EnsureUtf8(const std::string& text) { return text; }
}  // namespace
#endif

namespace {

std::vector<std::string> EnsureUtf8List(const std::vector<std::string>& values) {
    std::vector<std::string> safeValues;
    safeValues.reserve(values.size());
    for (const auto& value : values) {
        safeValues.push_back(EnsureUtf8(value));
    }
    return safeValues;
}

json InstallStateValueToManifestJson(const InstallStateValueConfig& value) {
    return {
        {"key", EnsureUtf8(value.key)},
        {"name", EnsureUtf8(value.name)},
        {"value", EnsureUtf8(value.value)},
        {"type", static_cast<int>(value.type)}
    };
}

json InstallStateValuesToManifestJson(const std::unordered_map<std::string, InstallStateValueConfig>& values) {
    json out = json::object();
    for (const auto& pair : values) {
        out[EnsureUtf8(pair.first)] = InstallStateValueToManifestJson(pair.second);
    }
    return out;
}

json DetectPolicyToManifestJson(const InstalledInstanceDetectConfig& detect) {
    json legacy = json::array();
    for (const auto& item : detect.legacy) {
        legacy.push_back({
            {"id", EnsureUtf8(item.id)},
            {"path", EnsureUtf8(item.path)},
            {"installDirValue", EnsureUtf8(item.installDirValue)}
        });
    }
    return {
        {"primary", {
            {"registry", EnsureUtf8(detect.primary.registry)},
            {"value", EnsureUtf8(detect.primary.value)}
        }},
        {"legacy", legacy}
    };
}

json InstallStateToManifestJson(const InstallStateConfig& installState) {
    json state;
    state["registries"] = json::array();
    for (const auto& store : installState.registries) {
        state["registries"].push_back({
            {"id", EnsureUtf8(store.id)},
            {"path", EnsureUtf8(store.path)},
            {"values", InstallStateValuesToManifestJson(store.values)}
        });
    }
    state["files"] = json::array();
    for (const auto& store : installState.files) {
        state["files"].push_back({
            {"id", EnsureUtf8(store.id)},
            {"path", EnsureUtf8(store.path)},
            {"format", EnsureUtf8(store.format)},
            {"values", InstallStateValuesToManifestJson(store.values)}
        });
    }
    state["detect"] = DetectPolicyToManifestJson(installState.detect);
    return state;
}

json UninstallEntryScopeToJson(UninstallEntryScope scope) {
    return static_cast<int>(scope);
}

json RegistryEntryToManifestJson(const RegistryEntry& entry) {
    return {
        {"path", EnsureUtf8(entry.path)},
        {"key", EnsureUtf8(entry.key)},
        {"value", EnsureUtf8(entry.value)},
        {"type", static_cast<int>(entry.type)}
    };
}

json NamedEntriesToManifestJson(const std::vector<NamedCleanupEntry>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        if (!entry.name.empty()) {
            out.push_back({{"name", EnsureUtf8(entry.name)}});
        }
    }
    return out;
}

json UninstallEntriesToManifestJson(const std::vector<UninstallEntryCleanup>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        if (!entry.name.empty()) {
            out.push_back({
                {"name", EnsureUtf8(entry.name)},
                {"scope", UninstallEntryScopeToJson(entry.scope)}
            });
        }
    }
    return out;
}

json SystemUninstallCleanupItemsToManifestJson(const std::vector<SystemUninstallEntryCleanupItem>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        if (!entry.displayName.empty()) {
            out.push_back({
                {"displayName", EnsureUtf8(entry.displayName)},
                {"scope", UninstallEntryScopeToJson(entry.scope)}
            });
        }
    }
    return out;
}

json SystemUninstallCleanupToManifestJson(const SystemUninstallEntryCleanupConfig& cleanup) {
    return {
        {"displayName", EnsureUtf8(cleanup.displayName)},
        {"scope", UninstallEntryScopeToJson(cleanup.scope)},
        {"legacyEntries", SystemUninstallCleanupItemsToManifestJson(cleanup.legacyEntries)}
    };
}

json CleanupRulesToManifestJson(const std::vector<UninstallCleanupRule>& rules) {
    json out = json::array();
    for (const auto& rule : rules) {
        if (!rule.path.empty()) {
            out.push_back({
                {"path", EnsureUtf8(rule.path)},
                {"recursive", rule.recursive},
                {"onlyIfEmpty", rule.onlyIfEmpty}
            });
        }
    }
    return out;
}

json ComponentActionsToManifestJson(const std::vector<ComponentExecutionRecord>& actions) {
    json out = json::array();
    for (const auto& action : actions) {
        if (action.uninstallCommand.empty()) {
            continue;
        }
        out.push_back({
            {"componentId", EnsureUtf8(action.componentId)},
            {"sourceType", EnsureUtf8(action.sourceType)},
            {"uninstallCommand", EnsureUtf8(action.uninstallCommand)},
            {"workingDirectory", EnsureUtf8(action.workingDirectory)},
            {"wait", action.wait},
            {"timeoutSec", action.timeoutSec}
        });
    }
    return out;
}

}  // namespace

bool writeManifest(const std::string& manifestPath,
                   const std::string& appId,
                   const std::string& displayName,
                   const std::string& appVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& cleanupRoots,
                   const UninstallCleanupConfig& actualCleanupSnapshot,
                   const std::vector<std::string>& filePaths,
                   const std::vector<std::string>& uninstallerKillBeforeUninstall,
                   bool installAutoStartup,
                   bool installDesktopIcon,
                   const std::string& desktopShortcutDisplayName,
                   const InstallStateConfig& installState,
                   const std::string& installStateCleanupMode,
                   const std::string& uninstallPath,
                   const std::string& languageCode,
                   const std::vector<ComponentExecutionRecord>& componentActions,
                   const std::vector<std::string>& selectedComponentIds,
                   bool installAllComponents,
                   const std::string& appPublisher,
                   const std::string& appWebsite,
                   const UninstallerCleanupConfigV3& uninstallerCleanup,
                   const SystemUninstallEntryConfig& systemUninstallEntry,
                   const InstalledFileFingerprintMap& fileFingerprints) {
    if (manifestPath.empty()) {
        return false;
    }

    try {
        json root;
        root["version"] = "1.0";
        root["schemaVersion"] = 3;
        root["manifestVersion"] = 2;
        root["appId"] = EnsureUtf8(appId);
        root["displayName"] = EnsureUtf8(displayName);
        root["appName"] = EnsureUtf8(displayName);
        root["appVersion"] = EnsureUtf8(appVersion);
        root["installDir"] = EnsureUtf8(installDir);
        root["uninstallPath"] = EnsureUtf8(uninstallPath);
        root["cleanupRoots"] = EnsureUtf8List(cleanupRoots);
        root["app"] = {
            {"id", EnsureUtf8(appId)},
            {"name", EnsureUtf8(displayName)},
            {"version", EnsureUtf8(appVersion)},
            {"publisher", EnsureUtf8(appPublisher)},
            {"website", EnsureUtf8(appWebsite)}
        };
        json systemUninstallEntrySnapshot;
        systemUninstallEntrySnapshot["writtenEntries"] = json::array();
        for (const auto& entry : actualCleanupSnapshot.uninstallEntries) {
            json item = {
                {"name", EnsureUtf8(entry.name)},
                {"scope", static_cast<int>(entry.scope)}
            };
            systemUninstallEntrySnapshot["writtenEntries"].push_back(std::move(item));
        }
        root["systemUninstallEntry"] = std::move(systemUninstallEntrySnapshot);

        std::vector<std::string> safeFiles;
        safeFiles.reserve(filePaths.size());
        for (const auto& path : filePaths) {
            safeFiles.push_back(EnsureUtf8(path));
        }
        root["files"] = safeFiles;

        // Per-file fingerprints enable the next upgrade's zero-read skip path.
        // Stored alongside (not replacing) the plain files[] list so existing
        // readers that only consume files[] keep working.
        if (!fileFingerprints.empty()) {
            json fingerprintArray = json::array();
            for (const auto& path : safeFiles) {
                const auto it = fileFingerprints.find(normalizePathForCompare(path));
                if (it == fileFingerprints.end()) {
                    continue;
                }
                fingerprintArray.push_back({
                    {"path", path},
                    {"size", it->second.size},
                    {"contentHash", it->second.contentHash},
                });
            }
            if (!fingerprintArray.empty()) {
                root["fileFingerprints"] = std::move(fingerprintArray);
            }
        }

        root["installAutoStartup"] = installAutoStartup;
        root["installDesktopIcon"] = installDesktopIcon;
        root["installAllComponents"] = installAllComponents;
        root["selectedComponentIds"] = EnsureUtf8List(selectedComponentIds);
        root["desktopShortcutDisplayName"] = EnsureUtf8(desktopShortcutDisplayName);
        root["language"] = EnsureUtf8(languageCode);

        std::vector<std::string> safeInstallKill;
        safeInstallKill.reserve(uninstallerKillBeforeUninstall.size());
        for (const auto& name : uninstallerKillBeforeUninstall) {
            safeInstallKill.push_back(EnsureUtf8(name));
        }
        root["killProcesses"] = safeInstallKill;

        json actions = ComponentActionsToManifestJson(componentActions);
        root["componentActions"] = actions;

        root["installState"] = InstallStateToManifestJson(installState);
        root["installer"] = {
            {"installState", InstallStateToManifestJson(installState)},
            {"systemUninstallEntry", {
                {"scope", UninstallEntryScopeToJson(systemUninstallEntry.scope)},
                {"displayName", EnsureUtf8(systemUninstallEntry.displayName)},
                {"publisher", EnsureUtf8(systemUninstallEntry.publisher)}
            }},
            {"defaults", {
                {"autoStartup", installAutoStartup},
                {"desktopShortcut", installDesktopIcon}
            }},
            {"payload", {
                {"files", safeFiles},
                {"roots", EnsureUtf8List(cleanupRoots)}
            }},
            {"components", {
                {"selectedComponentIds", EnsureUtf8List(selectedComponentIds)},
                {"installAllComponents", installAllComponents},
                {"actions", actions}
            }}
        };

        json cleanupV3;
        cleanupV3["installedFiles"] = EnsureUtf8(
            uninstallerCleanup.installedFiles.empty() ? "manifest" : uninstallerCleanup.installedFiles);
        cleanupV3["missingManifestFallback"] = EnsureUtf8(
            uninstallerCleanup.missingManifestFallback.empty() ? "safeDirectoryFallback"
                                                               : uninstallerCleanup.missingManifestFallback);
        cleanupV3["installState"] = EnsureUtf8(installStateCleanupMode.empty() ? "delete" : installStateCleanupMode);
        cleanupV3["autoStartup"] = EnsureUtf8(
            uninstallerCleanup.autoStartup.empty() ? "auto" : uninstallerCleanup.autoStartup);
        cleanupV3["desktopShortcut"] = EnsureUtf8(
            uninstallerCleanup.desktopShortcut.empty() ? "auto" : uninstallerCleanup.desktopShortcut);
        cleanupV3["systemUninstallEntry"] =
            SystemUninstallCleanupToManifestJson(uninstallerCleanup.systemUninstallEntry);
        cleanupV3["legacy"] = {
            {"desktopShortcutNames", EnsureUtf8List(uninstallerCleanup.legacy.desktopShortcutNames)},
            {"startupNames", EnsureUtf8List(uninstallerCleanup.legacy.startupNames)}
        };
        cleanupV3["registry"] = {
            {"deleteKeys", EnsureUtf8List(uninstallerCleanup.registry.deleteKeys)},
            {"deleteValues", json::array()}
        };
        for (const auto& entry : uninstallerCleanup.registry.deleteValues) {
            cleanupV3["registry"]["deleteValues"].push_back(RegistryEntryToManifestJson(entry));
        }
        cleanupV3["paths"] = CleanupRulesToManifestJson(uninstallerCleanup.paths);
        cleanupV3["actual"] = {
            {"autoStartup", NamedEntriesToManifestJson(actualCleanupSnapshot.startup)},
            {"desktopShortcut", NamedEntriesToManifestJson(actualCleanupSnapshot.shortcuts)},
            {"systemUninstallEntry", UninstallEntriesToManifestJson(actualCleanupSnapshot.uninstallEntries)}
        };
        root["uninstaller"] = {
            {"killBeforeUninstall", EnsureUtf8List(uninstallerKillBeforeUninstall)},
            {"cleanup", cleanupV3},
            {"components", {
                {"actions", actions}
            }}
        };

        std::filesystem::path path = PathFromUtf8(manifestPath);
        std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            FileSystemOperator fs;
            if (!fs.createDirectoryRecursive(Utf8FromPath(parent))) {
                return false;
            }
        }

        std::ofstream out(toLongPath(path), std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        std::string payload = root.dump(2, ' ', false, json::error_handler_t::replace);
        out.write(payload.c_str(), static_cast<std::streamsize>(payload.size()));
        return static_cast<bool>(out);
    } catch (const std::exception& e) {
        logInstallerError(std::string("[UNINSTALL] Failed to write manifest: ") + e.what());
        return false;
    }
}

bool readManifest(const std::string& manifestPath, json& outManifest) {
    if (manifestPath.empty()) {
        return false;
    }
    std::ifstream in(toLongPath(PathFromUtf8(manifestPath)), std::ios::binary);
    if (!in) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty()) {
        return false;
    }
    outManifest = json::parse(content, nullptr, false);
    return !outManifest.is_discarded();
}

bool loadPreviousInstallOptions(const std::string& manifestPath,
                                PreviousInstallOptions& options,
                                std::string& error) {
    options = PreviousInstallOptions{};
    error.clear();

    json manifest;
    if (!readManifest(manifestPath, manifest)) {
        error = "Failed to read previous install manifest";
        return false;
    }
    if (!manifest.contains("installAutoStartup") || !manifest["installAutoStartup"].is_boolean() ||
        !manifest.contains("installDesktopIcon") || !manifest["installDesktopIcon"].is_boolean() ||
        !manifest.contains("language") || !manifest["language"].is_string() ||
        !manifest.contains("installAllComponents") || !manifest["installAllComponents"].is_boolean() ||
        !manifest.contains("selectedComponentIds") || !manifest["selectedComponentIds"].is_array()) {
        error = "Previous install manifest does not contain complete install options";
        return false;
    }

    options.autoStartup = manifest.value("installAutoStartup", false);
    options.desktopIcon = manifest.value("installDesktopIcon", false);
    options.installAllComponents = manifest.value("installAllComponents", false);
    options.languageCode = manifest.value("language", "");
    options.selectedComponentIds.clear();
    for (const auto& item : manifest["selectedComponentIds"]) {
        if (!item.is_string()) {
            error = "Previous install manifest contains invalid selectedComponentIds";
            return false;
        }
        options.selectedComponentIds.push_back(item.get<std::string>());
    }
    return true;
}

bool loadPreviousInstallFileFingerprints(const std::string& manifestPath,
                                         InstalledFileFingerprintMap& out) {
    out.clear();

    json manifest;
    if (!readManifest(manifestPath, manifest)) {
        return false;
    }
    if (!manifest.contains("fileFingerprints") || !manifest["fileFingerprints"].is_array()) {
        return false;
    }
    for (const auto& item : manifest["fileFingerprints"]) {
        if (!item.is_object()) {
            continue;
        }
        const std::string path = item.value("path", std::string{});
        if (path.empty()) {
            continue;
        }
        const std::string key = normalizePathForCompare(path);
        if (key.empty()) {
            continue;
        }
        InstalledFileFingerprint fingerprint;
        fingerprint.size = item.value("size", 0ULL);
        fingerprint.contentHash = item.value("contentHash", 0ULL);
        if (fingerprint.contentHash == 0) {
            continue;
        }
        out[key] = fingerprint;
    }
    return !out.empty();
}

}  // namespace MultiThreadedInstaller
