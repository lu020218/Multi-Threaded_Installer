#include "installer/uninstall_manager.h"
#include "installer/install_manifest_store.h"
#include "installer/self_delete_scheduler.h"
#include "installer/install_state_utils.h"
#include "installer/install_state_store.h"
#include "installer/installer_helpers.h"
#include "installer/installed_instance_resolver.h"
#include "installer/cleanup_delete_executor.h"
#include "installer/registry_utils.h"
#include "installer/shortcut_startup_utils.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <chrono>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <set>
#include <mutex>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

using json = nlohmann::json;

namespace {

char ToLowerAsciiChar(unsigned char value) {
    return static_cast<char>(std::tolower(value));
}

} // namespace

static std::string GetManifestAppId(const json& manifest) {
    if (manifest.contains("app") && manifest["app"].is_object()) {
        std::string appId = manifest["app"].value("id", "");
        if (!appId.empty()) {
            return appId;
        }
    }
    std::string appId = manifest.value("appId", "");
    if (!appId.empty()) {
        return appId;
    }
    return manifest.value("appName", "");
}

static std::string GetManifestDisplayName(const json& manifest) {
    if (manifest.contains("app") && manifest["app"].is_object()) {
        std::string appName = manifest["app"].value("name", "");
        if (!appName.empty()) {
            return appName;
        }
    }
    std::string displayName = manifest.value("displayName", "");
    if (!displayName.empty()) {
        return displayName;
    }
    return manifest.value("appName", "");
}

static std::vector<NamedCleanupEntry> GetManifestNamedEntries(const json& node) {
    std::vector<NamedCleanupEntry> entries;
    if (!node.is_array()) {
        return entries;
    }
    for (const auto& item : node) {
        if (!item.is_object()) {
            continue;
        }
        NamedCleanupEntry entry;
        entry.name = item.value("name", "");
        if (!entry.name.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

static std::vector<RegistryEntry> GetManifestRegistryEntries(const json& node) {
    std::vector<RegistryEntry> entries;
    if (!node.is_array()) {
        return entries;
    }
    for (const auto& item : node) {
        if (!item.is_object()) {
            continue;
        }
        RegistryEntry entry;
        entry.path = item.value("path", "");
        entry.key = item.value("key", "");
        entry.value = item.value("value", "");
        entry.type = static_cast<RegistryValueType>(
            item.value("type", static_cast<int>(RegistryValueType::STRING)));
        if (!entry.path.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

static std::vector<UninstallEntryCleanup> GetManifestUninstallEntries(const json& node) {
    std::vector<UninstallEntryCleanup> entries;
    if (!node.is_array()) {
        return entries;
    }
    for (const auto& item : node) {
        if (!item.is_object()) {
            continue;
        }
        UninstallEntryCleanup entry;
        entry.name = item.value("name", "");
        entry.scope = static_cast<UninstallEntryScope>(
            item.value("scope", static_cast<int>(UninstallEntryScope::ANY)));
        if (!entry.name.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

static std::vector<UninstallEntryCleanup> GetManifestSystemUninstallCleanupEntries(const json& node) {
    std::vector<UninstallEntryCleanup> entries;
    if (!node.is_object()) {
        return entries;
    }
    if (node.contains("legacyEntries") && node["legacyEntries"].is_array()) {
        for (const auto& item : node["legacyEntries"]) {
            if (!item.is_object()) {
                continue;
            }
            UninstallEntryCleanup entry;
            entry.name = item.value("displayName", "");
            entry.scope = static_cast<UninstallEntryScope>(
                item.value("scope", static_cast<int>(UninstallEntryScope::ANY)));
            if (!entry.name.empty()) {
                entries.push_back(std::move(entry));
            }
        }
    }
    return entries;
}

static std::vector<UninstallCleanupRule> GetManifestCleanupRules(const json& node) {
    std::vector<UninstallCleanupRule> rules;
    if (!node.is_array()) {
        return rules;
    }
    for (const auto& item : node) {
        if (!item.is_object()) {
            continue;
        }
        UninstallCleanupRule rule;
        rule.path = item.value("path", "");
        rule.recursive = item.value("recursive", true);
        rule.onlyIfEmpty = item.value("onlyIfEmpty", false);
        if (!rule.path.empty()) {
            rules.push_back(std::move(rule));
        }
    }
    return rules;
}

static UninstallCleanupConfig GetManifestUninstallCleanup(const json& manifest) {
    UninstallCleanupConfig cleanup;
    if (manifest.contains("uninstaller") && manifest["uninstaller"].is_object()) {
        const auto& uninstaller = manifest["uninstaller"];
        if (uninstaller.contains("killBeforeUninstall") && uninstaller["killBeforeUninstall"].is_array()) {
            for (const auto& item : uninstaller["killBeforeUninstall"]) {
                if (item.is_string()) {
                    NamedCleanupEntry entry;
                    entry.name = item.get<std::string>();
                    cleanup.processes.push_back(std::move(entry));
                }
            }
        }
        if (uninstaller.contains("cleanup") && uninstaller["cleanup"].is_object()) {
            const auto& node = uninstaller["cleanup"];
            if (node.contains("actual") && node["actual"].is_object()) {
                const auto& actual = node["actual"];
                if (actual.contains("autoStartup")) {
                    cleanup.startup = GetManifestNamedEntries(actual["autoStartup"]);
                }
                if (actual.contains("desktopShortcut")) {
                    cleanup.shortcuts = GetManifestNamedEntries(actual["desktopShortcut"]);
                }
                if (actual.contains("systemUninstallEntry")) {
                    cleanup.uninstallEntries = GetManifestUninstallEntries(actual["systemUninstallEntry"]);
                }
            }
            if (node.contains("legacy") && node["legacy"].is_object()) {
                const auto& legacy = node["legacy"];
                for (const auto& name : legacy.value("desktopShortcutNames", std::vector<std::string>{})) {
                    NamedCleanupEntry entry;
                    entry.name = name;
                    cleanup.shortcuts.push_back(std::move(entry));
                }
                for (const auto& name : legacy.value("startupNames", std::vector<std::string>{})) {
                    NamedCleanupEntry entry;
                    entry.name = name;
                    cleanup.startup.push_back(std::move(entry));
                }
            }
            if (node.contains("systemUninstallEntry")) {
                auto configuredEntries =
                    GetManifestSystemUninstallCleanupEntries(node["systemUninstallEntry"]);
                cleanup.uninstallEntries.insert(cleanup.uninstallEntries.end(),
                                                configuredEntries.begin(),
                                                configuredEntries.end());
            }
            if (node.contains("registry") && node["registry"].is_object()) {
                const auto& registry = node["registry"];
                if (registry.contains("deleteKeys") && registry["deleteKeys"].is_array()) {
                    for (const auto& item : registry["deleteKeys"]) {
                        if (item.is_string()) {
                            RegistryEntry entry;
                            entry.path = item.get<std::string>();
                            cleanup.registry.legacyKeys.push_back(std::move(entry));
                        }
                    }
                }
                if (registry.contains("deleteValues")) {
                    auto values = GetManifestRegistryEntries(registry["deleteValues"]);
                    cleanup.registry.legacyKeys.insert(cleanup.registry.legacyKeys.end(),
                                                       values.begin(),
                                                       values.end());
                }
            }
            if (node.contains("paths")) {
                cleanup.paths = GetManifestCleanupRules(node["paths"]);
            }
            return cleanup;
        }
    }
    return cleanup;
}

static void MergeEmbeddedUninstallerCleanup(UninstallCleanupConfig& cleanup,
                                            const UninstallerCleanupConfigV3* embeddedCleanup,
                                            const std::vector<std::string>* embeddedKillBeforeUninstall) {
    std::set<std::string> processSeen;
    for (const auto& process : cleanup.processes) {
        processSeen.insert(process.name);
    }
    if (embeddedKillBeforeUninstall) {
        for (const auto& process : *embeddedKillBeforeUninstall) {
            if (!process.empty() && processSeen.insert(process).second) {
                NamedCleanupEntry entry;
                entry.name = process;
                cleanup.processes.push_back(std::move(entry));
            }
        }
    }
    if (!embeddedCleanup) {
        return;
    }

    std::set<std::string> shortcutSeen;
    for (const auto& shortcut : cleanup.shortcuts) {
        shortcutSeen.insert(shortcut.name);
    }
    for (const auto& name : embeddedCleanup->legacy.desktopShortcutNames) {
        if (!name.empty() && shortcutSeen.insert(name).second) {
            NamedCleanupEntry entry;
            entry.name = name;
            cleanup.shortcuts.push_back(std::move(entry));
        }
    }

    std::set<std::string> startupSeen;
    for (const auto& startup : cleanup.startup) {
        startupSeen.insert(startup.name);
    }
    for (const auto& name : embeddedCleanup->legacy.startupNames) {
        if (!name.empty() && startupSeen.insert(name).second) {
            NamedCleanupEntry entry;
            entry.name = name;
            cleanup.startup.push_back(std::move(entry));
        }
    }

    std::set<std::string> registrySeen;
    for (const auto& entry : cleanup.registry.legacyKeys) {
        registrySeen.insert(entry.path + "\n" + entry.key);
    }
    for (const auto& path : embeddedCleanup->registry.deleteKeys) {
        const std::string key = path + "\n";
        if (!path.empty() && registrySeen.insert(key).second) {
            RegistryEntry entry;
            entry.path = path;
            cleanup.registry.legacyKeys.push_back(std::move(entry));
        }
    }
    for (const auto& value : embeddedCleanup->registry.deleteValues) {
        const std::string key = value.path + "\n" + value.key;
        if (!value.path.empty() && !value.key.empty() && registrySeen.insert(key).second) {
            cleanup.registry.legacyKeys.push_back(value);
        }
    }

    std::set<std::string> pathSeen;
    for (const auto& rule : cleanup.paths) {
        pathSeen.insert(rule.path);
    }
    for (const auto& rule : embeddedCleanup->paths) {
        if (!rule.path.empty() && pathSeen.insert(rule.path).second) {
            cleanup.paths.push_back(rule);
        }
    }
}

static InstallStateValueConfig GetManifestInstallStateValue(const json& node) {
    InstallStateValueConfig value;
    value.key = node.value("key", "");
    value.name = node.value("name", "");
    value.value = node.value("value", "");
    value.type = static_cast<RegistryValueType>(
        node.value("type", static_cast<int>(RegistryValueType::STRING)));
    return value;
}

static std::unordered_map<std::string, InstallStateValueConfig>
GetManifestInstallStateValues(const json& node) {
    std::unordered_map<std::string, InstallStateValueConfig> values;
    if (!node.is_object()) {
        return values;
    }
    for (auto it = node.begin(); it != node.end(); ++it) {
        if (it.value().is_object()) {
            values[it.key()] = GetManifestInstallStateValue(it.value());
        }
    }
    return values;
}

static InstallStateConfig GetManifestInstallState(const json& manifest) {
    InstallStateConfig config;
    const json* stateNode = nullptr;
    if (manifest.contains("installer") && manifest["installer"].is_object() &&
        manifest["installer"].contains("installState") && manifest["installer"]["installState"].is_object()) {
        stateNode = &manifest["installer"]["installState"];
    } else if (manifest.contains("installState") && manifest["installState"].is_object()) {
        stateNode = &manifest["installState"];
    }
    if (stateNode) {
        const auto& node = *stateNode;
        if (node.contains("registries") && node["registries"].is_array()) {
            for (const auto& item : node["registries"]) {
                if (!item.is_object()) {
                    continue;
                }
                InstallStateRegistryStoreConfig store;
                store.id = item.value("id", "");
                store.path = item.value("path", "");
                store.values = GetManifestInstallStateValues(item.value("values", json::object()));
                if (!store.path.empty()) {
                    config.registries.push_back(std::move(store));
                }
            }
        }
        if (node.contains("files") && node["files"].is_array()) {
            for (const auto& item : node["files"]) {
                if (!item.is_object()) {
                    continue;
                }
                InstallStateFileStoreConfig store;
                store.id = item.value("id", "");
                store.path = item.value("path", "");
                store.format = item.value("format", "json");
                store.values = GetManifestInstallStateValues(item.value("values", json::object()));
                if (!store.path.empty()) {
                    config.files.push_back(std::move(store));
                }
            }
        }
    }
    return config;
}

static std::string GetManifestInstallStateCleanupMode(const json& manifest) {
    if (manifest.contains("uninstaller") && manifest["uninstaller"].is_object()) {
        const auto& uninstaller = manifest["uninstaller"];
        if (uninstaller.contains("cleanup") && uninstaller["cleanup"].is_object()) {
            return uninstaller["cleanup"].value("installState", "delete");
        }
    }
    return "delete";
}

static bool ManifestHasV3Snapshot(const json& manifest) {
    return manifest.contains("app") && manifest["app"].is_object() &&
           manifest.contains("installer") && manifest["installer"].is_object() &&
           manifest.contains("uninstaller") && manifest["uninstaller"].is_object();
}

static bool ValidateV3UninstallSnapshot(const json& manifest, std::string& error) {
    if (!ManifestHasV3Snapshot(manifest)) {
        error = "Uninstall manifest does not contain v3 uninstall snapshot.";
        return false;
    }
    const auto& installer = manifest["installer"];
    const auto& uninstaller = manifest["uninstaller"];
    if (!installer.contains("installState") || !installer["installState"].is_object()) {
        error = "Uninstall manifest missing installer.installState.";
        return false;
    }
    if (!installer.contains("payload") || !installer["payload"].is_object() ||
        !installer["payload"].contains("files") || !installer["payload"]["files"].is_array()) {
        error = "Uninstall manifest missing installer.payload.files.";
        return false;
    }
    if (!uninstaller.contains("cleanup") || !uninstaller["cleanup"].is_object()) {
        error = "Uninstall manifest missing uninstaller.cleanup.";
        return false;
    }
    const std::string installedFiles = uninstaller["cleanup"].value("installedFiles", "manifest");
    if (!installedFiles.empty() && installedFiles != "manifest") {
        error = "Unsupported uninstaller.cleanup.installedFiles: " + installedFiles;
        return false;
    }
    return true;
}

static InstallStateContext BuildUninstallInstallStateContext(const std::string& installDir,
                                                             const std::string& appVersion,
                                                             const std::string& appName,
                                                             const std::string& appId,
                                                             const std::string& state) {
    InstallStateContext context;
    context.installDir = installDir;
    context.version = appVersion;
    context.appName = appName;
    context.appId = appId.empty() ? appName : appId;
    context.installSource = getCurrentExecutablePath();
    context.state = state;
    context.userName = GetCurrentUserNameForInstallState();
    return context;
}

static std::string ExpandInstallDirTokenLocal(const std::string& text,
                                              const std::string& installDir) {
    if (text.empty()) {
        return text;
    }
    const std::string token = "%InstallDir%";
    std::string expanded = text;
    size_t position = 0;
    while ((position = expanded.find(token, position)) != std::string::npos) {
        expanded.replace(position, token.size(), installDir);
        position += installDir.size();
    }
    return expanded;
}

static bool IsPathUnderOrEqualLocal(const std::filesystem::path& candidate,
                                    const std::filesystem::path& root) {
    const std::string normalizedCandidate = normalizePathForCompare(Utf8FromPath(candidate.lexically_normal()));
    const std::string normalizedRoot = normalizePathForCompare(Utf8FromPath(root.lexically_normal()));
    if (normalizedCandidate.empty() || normalizedRoot.empty()) {
        return false;
    }
    if (normalizedCandidate == normalizedRoot) {
        return true;
    }
    if (normalizedCandidate.size() <= normalizedRoot.size()) {
        return false;
    }
    if (normalizedCandidate.compare(0, normalizedRoot.size(), normalizedRoot) != 0) {
        return false;
    }
    const char sep = normalizedCandidate[normalizedRoot.size()];
    return sep == '\\' || sep == '/';
}

static std::string ExpandCleanupRulePath(const UninstallCleanupRule& rule,
                                         const std::string& installDir,
                                         InstallerPathResolver& resolver) {
    std::string expanded = ExpandInstallDirTokenLocal(rule.path, installDir);
    return resolver.expandEnvironmentVariables(expanded);
}

static std::string ReadEnvironmentPathLocal(const char* name) {
#ifdef _WIN32
    DWORD required = GetEnvironmentVariableA(name, nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::string value(required, '\0');
    DWORD written = GetEnvironmentVariableA(name, value.data(), required);
    if (written == 0 || written >= required) {
        return {};
    }
    value.resize(written);
    return value;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

static bool SameNormalizedPathLocal(const std::filesystem::path& left,
                                    const std::filesystem::path& right) {
    return normalizePathForCompare(Utf8FromPath(left.lexically_normal())) ==
           normalizePathForCompare(Utf8FromPath(right.lexically_normal()));
}

static bool IsDangerousFallbackRoot(const std::filesystem::path& path) {
    if (path.empty() || !path.is_absolute()) {
        return true;
    }
    const std::filesystem::path normalized = path.lexically_normal();
    const std::filesystem::path rootPath = normalized.root_path();
    if (rootPath.empty() || normalized == rootPath || normalized.filename().empty()) {
        return true;
    }

    const std::vector<std::string> protectedExact = {
        ReadEnvironmentPathLocal("ProgramFiles"),
        ReadEnvironmentPathLocal("ProgramFiles(x86)"),
        ReadEnvironmentPathLocal("ProgramW6432"),
        ReadEnvironmentPathLocal("ProgramData"),
        ReadEnvironmentPathLocal("USERPROFILE"),
    };
    for (const auto& root : protectedExact) {
        if (!root.empty() && SameNormalizedPathLocal(normalized, PathFromUtf8(root))) {
            return true;
        }
    }

    const std::vector<std::string> protectedSubtrees = {
        ReadEnvironmentPathLocal("SystemRoot"),
        ReadEnvironmentPathLocal("WINDIR"),
    };
    for (const auto& root : protectedSubtrees) {
        if (!root.empty() && IsPathUnderOrEqualLocal(normalized, PathFromUtf8(root).lexically_normal())) {
            return true;
        }
    }

    const std::string userProfile = ReadEnvironmentPathLocal("USERPROFILE");
    if (!userProfile.empty()) {
        const std::vector<std::string> userFolders = {
            userProfile + "\\Desktop",
            userProfile + "\\Documents",
            userProfile + "\\Downloads",
        };
        for (const auto& folder : userFolders) {
            if (SameNormalizedPathLocal(normalized, PathFromUtf8(folder))) {
                return true;
            }
        }
    }

    return false;
}

static bool IsReparsePointPathLocal(const std::filesystem::path& path) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesW(toLongPath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == FILE_ATTRIBUTE_REPARSE_POINT;
#else
    std::error_code ec;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec));
#endif
}

static bool TryLoadManifestIntoContext(const std::string& manifestPath,
                                       UninstallContext& context,
                                       bool explicitPath) {
    if (manifestPath.empty()) {
        return false;
    }
    std::error_code ec;
    const std::filesystem::path path = PathFromUtf8(manifestPath);
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        if (explicitPath) {
            context.errorMessage = "Uninstall manifest missing; cannot uninstall.";
        }
        return false;
    }

    json manifest;
    if (!readManifest(manifestPath, manifest)) {
        context.manifestPath = manifestPath;
        context.manifestReadable = false;
        context.fallbackAllowed = false;
        context.errorMessage = "Uninstall manifest is corrupted or unreadable; cannot uninstall.";
        return true;
    }

    context.manifestPath = manifestPath;
    context.manifestReadable = true;
    context.manifestValidV3 = ManifestHasV3Snapshot(manifest);
    context.fallbackAllowed = false;
    context.installDir = manifest.value("installDir", "");
    context.appId = GetManifestAppId(manifest);
    context.appName = GetManifestDisplayName(manifest);
    context.errorMessage.clear();
    return true;
}

static bool TrySetFallbackContext(const std::string& installDir,
                                  const ExtendedInstallationMetadata* metadata,
                                  UninstallContext& context,
                                  const std::string& manifestPath = {}) {
    if (installDir.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::path installPath = PathFromUtf8(installDir);
    if (!std::filesystem::exists(installPath, ec) || !std::filesystem::is_directory(installPath, ec)) {
        return false;
    }
    if (IsDangerousFallbackRoot(installPath)) {
        context.errorMessage = "Unable to determine a safe install directory; uninstall failed.";
        return false;
    }

    context.installDir = Utf8FromPath(installPath);
    context.manifestPath = manifestPath;
    context.manifestReadable = false;
    context.manifestValidV3 = false;
    context.fallbackAllowed = true;
    context.fallbackPolicy = metadata ? metadata->uninstallerCleanup.missingManifestFallback
                                      : "safeDirectoryFallback";
    if (metadata) {
        context.appId = resolveEffectiveAppId(metadata->appId, metadata->appName);
        context.appName = metadata->appName;
        context.embeddedUninstallerCleanup = metadata->uninstallerCleanup;
        context.hasEmbeddedUninstallerCleanup = true;
        context.embeddedKillBeforeUninstall = metadata->uninstallerKillBeforeUninstall;
    }
    context.errorMessage = "Uninstall manifest missing; safe fallback uninstall will be used.";
    return true;
}

#ifdef _WIN32
static bool DeleteUninstallEntryByScope(const UninstallEntryCleanup& entry) {
    return deleteSystemUninstallEntryByDisplayName(entry.name, entry.scope);
}

static bool DeleteSystemUninstallCleanupItem(const SystemUninstallEntryCleanupItem& entry) {
    return deleteSystemUninstallEntryByDisplayName(entry.displayName, entry.scope);
}
#endif

#ifdef _WIN32
static bool executeShellCommandWithTimeout(const std::string& command,
                                           const std::string& workingDirectory,
                                           bool wait,
                                           uint32_t timeoutSec,
                                           const std::function<bool()>& cancellationCallback,
                                           DWORD& exitCode,
                                           std::string& error) {
    if (command.empty()) {
        error = "Component uninstall command is empty.";
        return false;
    }

    std::wstring commandLine = L"cmd.exe /c ";
    commandLine += Utf8ToWide(command);
    std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
    commandBuffer.push_back(L'\0');

    std::wstring workDirW = Utf8ToWide(workingDirectory);
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    BOOL ok = CreateProcessW(nullptr,
                             commandBuffer.data(),
                             nullptr,
                             nullptr,
                             FALSE,
                             CREATE_NO_WINDOW,
                             nullptr,
                             workDirW.empty() ? nullptr : workDirW.c_str(),
                             &si,
                             &pi);
    if (!ok) {
        error = "Failed to start component uninstall command.";
        return false;
    }

    CloseHandle(pi.hThread);
    pi.hThread = nullptr;

    if (!wait) {
        exitCode = 0;
        CloseHandle(pi.hProcess);
        return true;
    }

    const uint64_t timeoutMs = timeoutSec == 0
                                   ? std::numeric_limits<uint64_t>::max()
                                   : static_cast<uint64_t>(timeoutSec) * 1000ULL;
    uint64_t elapsedMs = 0;
    while (true) {
        if (cancellationCallback && cancellationCallback()) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            error = "Component uninstall cancelled.";
            return false;
        }

        DWORD slice = 200;
        if (timeoutMs != std::numeric_limits<uint64_t>::max()) {
            if (elapsedMs >= timeoutMs) {
                TerminateProcess(pi.hProcess, 1);
                CloseHandle(pi.hProcess);
                error = "Component uninstall timed out.";
                return false;
            }
            uint64_t remaining = timeoutMs - elapsedMs;
            if (remaining < slice) {
                slice = static_cast<DWORD>(remaining);
            }
        }

        DWORD waitResult = WaitForSingleObject(pi.hProcess, slice);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult != WAIT_TIMEOUT) {
            CloseHandle(pi.hProcess);
            error = "Failed while waiting for component uninstall command.";
            return false;
        }
        elapsedMs += slice;
    }

    if (!GetExitCodeProcess(pi.hProcess, &exitCode)) {
        CloseHandle(pi.hProcess);
        error = "Failed to read component uninstall exit code.";
        return false;
    }
    CloseHandle(pi.hProcess);
    return true;
}
#endif

struct UninstallCleanupPolicy {
    uint32_t itemStaleTimeoutMs = 30000;
    uint32_t totalTimeoutMs = 0;
    uint32_t heartbeatIntervalMs = 1000;
    uint32_t heartbeatEveryItems = 100;
    uint32_t slowItemLogMs = 3000;
    uint32_t workerConcurrency = 0;
    bool allowPartialSuccess = true;
};

struct UninstallCleanupResult {
    bool success = true;
    bool partial = false;
    bool timedOut = false;
    uint64_t deletedCount = 0;
    uint64_t failedCount = 0;
    uint64_t skippedCount = 0;
    std::string timedOutPath;
    std::string message;
};

struct UninstallCleanupTask {
    bool fallbackMode = false;
    std::string installDir;
    std::string manifestPath;
    std::string uninstallPath;
    std::string currentExePath;
    std::string heartbeatPath;
    std::string resultPath;
    std::vector<std::string> files;
    std::vector<std::string> cleanupRoots;
    std::vector<UninstallCleanupRule> cleanupPaths;
    UninstallCleanupPolicy policy;
};

uint64_t UninstallNowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool WriteUninstallJsonBestEffort(const std::filesystem::path& path, const json& value) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(toLongPath(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << value.dump();
    return out.good();
}

bool ReadUninstallJsonBestEffort(const std::filesystem::path& path, json& value) {
    std::ifstream in(toLongPath(path), std::ios::binary);
    if (!in) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty()) {
        return false;
    }
    value = json::parse(content, nullptr, false);
    return !value.is_discarded();
}

std::filesystem::path BuildUninstallCleanupTempPath(const char* suffix) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "MTInstaller";
    std::ostringstream name;
    name << "uninstall_cleanup_";
#ifdef _WIN32
    name << GetCurrentProcessId();
#else
    name << std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
    name << "_" << UninstallNowMs() << suffix;
    return dir / PathFromUtf8(name.str());
}

struct UninstallCleanupExecutionState {
    UninstallCleanupTask task;
    UninstallCleanupResult result;
    std::mutex mutex;
    std::string currentPath;
    std::string currentAction;
    uint64_t currentStartedMs = 0;
    uint64_t lastHeartbeatMs = 0;
    uint64_t processedSinceHeartbeat = 0;
    uint64_t processedCount = 0;
    uint32_t workerConcurrency = 1;
    uint32_t activeWorkers = 0;
    uint64_t lastCompletedAtMs = 0;
    std::string slowestCurrentItem;
    std::string slowestCurrentThreadId;
    uint64_t slowestCurrentStartedMs = 0;
    std::map<std::string, uint64_t> activeItems;
    std::map<std::string, std::string> activeItemThreadIds;
};

void EmitUninstallHeartbeat(UninstallCleanupExecutionState& state, bool force) {
    if (state.task.heartbeatPath.empty()) {
        return;
    }
    const uint64_t now = UninstallNowMs();
    const bool intervalDue = state.lastHeartbeatMs == 0 ||
        now - state.lastHeartbeatMs >= state.task.policy.heartbeatIntervalMs;
    const bool countDue = state.task.policy.heartbeatEveryItems > 0 &&
        state.processedSinceHeartbeat >= state.task.policy.heartbeatEveryItems;
    if (!force && !intervalDue && !countDue) {
        return;
    }
    json heartbeat = {
        {"timestampMs", now},
        {"currentPath", state.currentPath},
        {"currentAction", state.currentAction},
        {"currentStartedMs", state.currentStartedMs},
        {"deletedCount", state.result.deletedCount},
        {"failedCount", state.result.failedCount},
        {"skippedCount", state.result.skippedCount},
        {"processedCount", state.processedCount},
        {"workerConcurrency", state.workerConcurrency},
        {"activeWorkers", state.activeWorkers},
        {"slowestCurrentItem", state.slowestCurrentItem},
        {"slowestCurrentThreadId", state.slowestCurrentThreadId},
        {"slowestCurrentStartedMs", state.slowestCurrentStartedMs},
        {"lastCompletedAt", state.lastCompletedAtMs},
    };
    WriteUninstallJsonBestEffort(PathFromUtf8(state.task.heartbeatPath), heartbeat);
    state.lastHeartbeatMs = now;
    state.processedSinceHeartbeat = 0;
}

void SetUninstallCurrentItem(UninstallCleanupExecutionState& state,
                             const std::filesystem::path& path,
                             const std::string& action,
                             uint64_t startedMs,
                             const std::string& threadId = {}) {
    state.currentPath = Utf8FromPath(path);
    state.currentAction = action;
    state.currentStartedMs = startedMs;
    ++state.activeWorkers;
    state.activeItems[state.currentPath] = startedMs;
    state.activeItemThreadIds[state.currentPath] = threadId;
    if (state.slowestCurrentItem.empty() ||
        startedMs < state.slowestCurrentStartedMs) {
        state.slowestCurrentItem = state.currentPath;
        state.slowestCurrentThreadId = threadId;
        state.slowestCurrentStartedMs = startedMs;
    }
    // Do not force heartbeat here. File deletion is the hot path for large
    // node_modules-like trees; heartbeat must stay time/count-throttled.
    EmitUninstallHeartbeat(state, false);
}

void MarkUninstallCurrentItemCompleted(UninstallCleanupExecutionState& state,
                                       const std::filesystem::path& path) {
    const std::string pathText = Utf8FromPath(path);
    auto it = state.activeItems.find(pathText);
    if (it != state.activeItems.end()) {
        state.activeItems.erase(it);
    }
    state.activeItemThreadIds.erase(pathText);
    if (state.activeWorkers > 0) {
        --state.activeWorkers;
    }
    state.lastCompletedAtMs = UninstallNowMs();

    state.slowestCurrentItem.clear();
    state.slowestCurrentThreadId.clear();
    state.slowestCurrentStartedMs = 0;
    for (const auto& item : state.activeItems) {
        if (state.slowestCurrentItem.empty() ||
            item.second < state.slowestCurrentStartedMs) {
            state.slowestCurrentItem = item.first;
            auto threadIt = state.activeItemThreadIds.find(item.first);
            state.slowestCurrentThreadId =
                threadIt == state.activeItemThreadIds.end() ? std::string() : threadIt->second;
            state.slowestCurrentStartedMs = item.second;
        }
    }
}

void MarkUninstallSkipped(UninstallCleanupExecutionState& state) {
    ++state.result.skippedCount;
    ++state.processedSinceHeartbeat;
    EmitUninstallHeartbeat(state, false);
}

void DeleteUninstallSinglePath(UninstallCleanupExecutionState& state,
                               const std::filesystem::path& path,
                               const std::string& action) {
    if (path.empty()) {
        MarkUninstallSkipped(state);
        return;
    }
    if (IsReparsePointPathLocal(path)) {
        ++state.result.skippedCount;
        ++state.processedSinceHeartbeat;
        logInstallerWarning("[Uninstall][Cleanup] skipped reparse point path=" + Utf8FromPath(path));
        EmitUninstallHeartbeat(state, false);
        return;
    }

    const uint64_t started = UninstallNowMs();
    SetUninstallCurrentItem(state, path, action, started);

    std::error_code ec;
    const bool removed = std::filesystem::remove(toLongPath(path), ec);
    const uint64_t elapsed = UninstallNowMs() - started;
    if (ec) {
        ++state.result.failedCount;
        logInstallerWarning("[Uninstall][Cleanup] delete failed path=" + Utf8FromPath(path) +
                            " error=" + ec.message());
    } else if (!removed) {
        ++state.result.skippedCount;
    } else {
        ++state.result.deletedCount;
        if (elapsed >= state.task.policy.slowItemLogMs) {
            logInstallerWarning("[Uninstall][Cleanup] slow delete path=" + Utf8FromPath(path) +
                                " elapsedMs=" + std::to_string(elapsed));
        }
    }
    MarkUninstallCurrentItemCompleted(state, path);
    ++state.processedSinceHeartbeat;
    EmitUninstallHeartbeat(state, false);
}

void DeleteUninstallDirectoryIfEmptyByRemove(UninstallCleanupExecutionState& state,
                                             const std::filesystem::path& path,
                                             const std::string& action) {
    if (path.empty()) {
        MarkUninstallSkipped(state);
        return;
    }
    if (IsReparsePointPathLocal(path)) {
        ++state.result.skippedCount;
        ++state.processedSinceHeartbeat;
        logInstallerWarning("[Uninstall][Cleanup] skipped reparse point path=" + Utf8FromPath(path));
        EmitUninstallHeartbeat(state, false);
        return;
    }

    const uint64_t started = UninstallNowMs();
    SetUninstallCurrentItem(state, path, action, started);
    std::error_code ec;
    const bool removed = std::filesystem::remove(toLongPath(path), ec);
    const uint64_t elapsed = UninstallNowMs() - started;
    if (!ec && removed) {
        ++state.result.deletedCount;
        if (elapsed >= state.task.policy.slowItemLogMs) {
            logInstallerWarning("[Uninstall][Cleanup] slow delete path=" + Utf8FromPath(path) +
                                " elapsedMs=" + std::to_string(elapsed));
        }
    } else if (!ec ||
               ec == std::make_error_code(std::errc::directory_not_empty) ||
               ec == std::make_error_code(std::errc::file_exists)) {
        // Non-empty directories are expected while walking parent chains.
    } else {
        ++state.result.failedCount;
        logInstallerWarning("[Uninstall][Cleanup] delete failed path=" + Utf8FromPath(path) +
                            " error=" + ec.message());
    }
    ++state.processedSinceHeartbeat;
    EmitUninstallHeartbeat(state, false);
}

void DeleteUninstallDirectorySegmented(UninstallCleanupExecutionState& state,
                                       const std::filesystem::path& root,
                                       bool removeRoot,
                                       bool onlyIfEmpty) {
    std::error_code existsEc;
    if (!std::filesystem::exists(root, existsEc)) {
        MarkUninstallSkipped(state);
        return;
    }
    if (IsReparsePointPathLocal(root)) {
        MarkUninstallSkipped(state);
        return;
    }
    if (onlyIfEmpty && !std::filesystem::is_empty(root, existsEc)) {
        MarkUninstallSkipped(state);
        return;
    }
    std::vector<std::filesystem::path> dirs;
    std::vector<std::filesystem::path> batch;
    batch.reserve(512);
    CleanupDeleteCallbacks callbacks;
    callbacks.onItemStarted = [&state](const std::filesystem::path& path,
                                       const std::string& action,
                                       uint64_t started,
                                       const std::string& threadId) {
        std::lock_guard<std::mutex> lock(state.mutex);
        SetUninstallCurrentItem(state, path, action, started, threadId);
    };
    callbacks.onItemFinished = [&state](const std::filesystem::path& path,
                                        const std::error_code& ec,
                                        bool removed,
                                        uint64_t elapsed,
                                        const std::string& threadId) {
        (void)threadId;
        std::lock_guard<std::mutex> lock(state.mutex);
        if (ec) {
            ++state.result.failedCount;
            logInstallerWarning("[Uninstall][Cleanup] delete failed path=" +
                                Utf8FromPath(path) + " error=" + ec.message());
        } else if (!removed) {
            ++state.result.skippedCount;
        } else {
            ++state.result.deletedCount;
            if (elapsed >= state.task.policy.slowItemLogMs) {
                logInstallerWarning("[Uninstall][Cleanup] slow delete path=" +
                                    Utf8FromPath(path) + " elapsedMs=" +
                                    std::to_string(elapsed));
            }
        }
        MarkUninstallCurrentItemCompleted(state, path);
        ++state.processedCount;
        ++state.processedSinceHeartbeat;
        EmitUninstallHeartbeat(state, false);
    };
    callbacks.onReparsePointSkipped = [&state](const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(state.mutex);
        ++state.result.skippedCount;
        ++state.processedSinceHeartbeat;
        logInstallerWarning("[Uninstall][Cleanup] skipped reparse point path=" + Utf8FromPath(path));
        EmitUninstallHeartbeat(state, false);
    };
    callbacks.onEmptyPathSkipped = [&state]() {
        std::lock_guard<std::mutex> lock(state.mutex);
        MarkUninstallSkipped(state);
    };
    CleanupDeleteExecutor executor(CleanupDeleteWorkload::Uninstall, std::move(callbacks));
    state.workerConcurrency = executor.workerConcurrency();
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    std::error_code iterEc;
    for (std::filesystem::recursive_directory_iterator it(toLongPath(root), options, iterEc), end;
         !iterEc && it != end;
         it.increment(iterEc)) {
        const auto path = it->path();
        if (IsReparsePointPathLocal(path)) {
            it.disable_recursion_pending();
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                ++state.result.skippedCount;
                ++state.processedSinceHeartbeat;
                EmitUninstallHeartbeat(state, false);
            }
            continue;
        }
        std::error_code typeEc;
        if (it->is_directory(typeEc)) {
            dirs.push_back(path);
        } else {
            batch.push_back(path);
            if (batch.size() >= 512) {
                executor.submit(batch);
            }
        }
    }
    executor.submit(batch);
    executor.finish();
    if (iterEc) {
        ++state.result.failedCount;
        logInstallerWarning("[Uninstall][Cleanup] directory scan failed path=" + Utf8FromPath(root) +
                            " error=" + iterEc.message());
    }
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
        return a.native().size() > b.native().size();
    });
    for (const auto& dir : dirs) {
        DeleteUninstallDirectoryIfEmptyByRemove(state, dir, "delete_empty_dir");
    }
    if (removeRoot) {
        DeleteUninstallDirectoryIfEmptyByRemove(state, root, "delete_root_dir");
    }
}

void DeleteUninstallPathSegmented(UninstallCleanupExecutionState& state,
                                  const std::filesystem::path& path,
                                  bool recursive,
                                  bool onlyIfEmpty,
                                  bool removeRoot) {
    std::error_code existsEc;
    if (!std::filesystem::exists(path, existsEc)) {
        MarkUninstallSkipped(state);
        return;
    }
    if (std::filesystem::is_directory(path, existsEc)) {
        if (recursive) {
            DeleteUninstallDirectorySegmented(state, path, removeRoot, onlyIfEmpty);
        } else if (!onlyIfEmpty || std::filesystem::is_empty(path, existsEc)) {
            DeleteUninstallSinglePath(state, path, "delete_dir");
        } else {
            MarkUninstallSkipped(state);
        }
    } else {
        DeleteUninstallSinglePath(state, path, "delete_file");
    }
}

std::vector<std::filesystem::path> CollectUninstallAffectedParentDirs(
    const std::vector<std::filesystem::path>& files,
    const std::filesystem::path& root) {
    std::vector<std::filesystem::path> dirs;
    std::set<std::string> seen;
    const auto normalizedRoot = root.lexically_normal();
    for (const auto& file : files) {
        std::filesystem::path dir = file.lexically_normal().parent_path();
        while (!dir.empty() && IsPathUnderOrEqualLocal(dir, normalizedRoot)) {
            const std::string key = normalizePathForCompare(Utf8FromPath(dir.lexically_normal()));
            if (!key.empty() && seen.insert(key).second) {
                dirs.push_back(dir);
            }
            if (SameNormalizedPathLocal(dir, normalizedRoot)) {
                break;
            }
            dir = dir.parent_path();
        }
    }
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
        return a.native().size() > b.native().size();
    });
    return dirs;
}

void DeleteUninstallAffectedEmptyDirs(UninstallCleanupExecutionState& state,
                                      const std::vector<std::filesystem::path>& files,
                                      const std::vector<std::string>& cleanupRoots,
                                      const std::string& currentExePath) {
    const std::filesystem::path currentExe = PathFromUtf8(currentExePath).lexically_normal();
    for (const auto& rootUtf8 : cleanupRoots) {
        const std::filesystem::path root = PathFromUtf8(rootUtf8).lexically_normal();
        if (root.empty()) {
            continue;
        }
        for (const auto& dir : CollectUninstallAffectedParentDirs(files, root)) {
            DeleteUninstallDirectoryIfEmptyByRemove(state, dir, "delete_affected_empty_dir");
        }
        const bool currentExeInsideRoot =
            !currentExePath.empty() && IsPathUnderOrEqualLocal(currentExe, root);
        if (!currentExeInsideRoot) {
            DeleteUninstallDirectoryIfEmptyByRemove(state, root, "delete_cleanup_root");
        }
    }
}

bool IsMtiPendingDirectoryNameLocal(const std::filesystem::path& path) {
    const std::string name = Utf8FromPath(path.filename());
    return name.rfind(".mti_delete_pending_", 0) == 0;
}

std::filesystem::path MakeUninstallPendingSiblingPath(const std::filesystem::path& child) {
    std::ostringstream name;
    name << ".mti_delete_pending_" << Utf8FromPath(child.filename()) << "_";
#ifdef _WIN32
    name << GetCurrentProcessId();
#else
    name << std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
    name << "_" << UninstallNowMs();
    return child.parent_path() / PathFromUtf8(name.str());
}

std::vector<std::filesystem::path> IsolateInstallRootChildDirectories(
    UninstallCleanupExecutionState& state,
    const std::filesystem::path& installRoot) {
    std::vector<std::filesystem::path> pendingDirs;
    std::error_code ec;
    if (installRoot.empty() || !std::filesystem::exists(installRoot, ec) ||
        !std::filesystem::is_directory(installRoot, ec)) {
        return pendingDirs;
    }

    std::vector<std::filesystem::path> children;
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::directory_iterator it(toLongPath(installRoot), options, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        std::error_code typeEc;
        if (!it->is_directory(typeEc) || IsReparsePointPathLocal(it->path())) {
            continue;
        }
        if (IsMtiPendingDirectoryNameLocal(it->path())) {
            pendingDirs.push_back(it->path());
        } else {
            children.push_back(it->path());
        }
    }

    for (const auto& child : children) {
        std::filesystem::path pending = MakeUninstallPendingSiblingPath(child);
        std::error_code renameEc;
        std::filesystem::rename(toLongPath(child), toLongPath(pending), renameEc);
        if (renameEc) {
            ++state.result.failedCount;
            logInstallerWarning("[Uninstall][Cleanup] isolate rename failed source=" +
                                Utf8FromPath(child) + " error=" + renameEc.message());
            continue;
        }
        ++state.result.deletedCount;
        ++state.processedCount;
        pendingDirs.push_back(std::move(pending));
    }
    return pendingDirs;
}

bool IsPathUnderAnyRootLocal(const std::filesystem::path& path,
                             const std::vector<std::filesystem::path>& roots) {
    for (const auto& root : roots) {
        if (IsPathUnderOrEqualLocal(path, root)) {
            return true;
        }
    }
    return false;
}

UninstallCleanupResult ExecuteUninstallCleanupTask(const UninstallCleanupTask& task) {
    UninstallCleanupExecutionState state;
    state.task = task;
    EmitUninstallHeartbeat(state, true);
    const std::string currentExeNorm = normalizePathForCompare(task.currentExePath);
    const auto installRoot = PathFromUtf8(task.installDir).lexically_normal();
    std::vector<std::filesystem::path> pendingDirs;
    if (!installRoot.empty() && !IsDangerousFallbackRoot(installRoot)) {
        pendingDirs = IsolateInstallRootChildDirectories(state, installRoot);
    }
    std::vector<std::filesystem::path> isolatedOriginalRoots;
    isolatedOriginalRoots.reserve(pendingDirs.size());
    for (const auto& pending : pendingDirs) {
        std::string name = Utf8FromPath(pending.filename());
        const std::string prefix = ".mti_delete_pending_";
        if (name.rfind(prefix, 0) == 0) {
            name = name.substr(prefix.size());
            const size_t pidSep = name.rfind('_');
            if (pidSep != std::string::npos) {
                name = name.substr(0, pidSep);
                const size_t timeSep = name.rfind('_');
                if (timeSep != std::string::npos) {
                    name = name.substr(0, timeSep);
                }
            }
            if (!name.empty()) {
                isolatedOriginalRoots.push_back((installRoot / PathFromUtf8(name)).lexically_normal());
            }
        }
    }
    if (task.fallbackMode) {
        if (IsDangerousFallbackRoot(installRoot)) {
            state.result.success = false;
            state.result.message = "Unsafe fallback uninstall root";
            return state.result;
        }
        std::error_code iterEc;
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::directory_iterator it(toLongPath(installRoot), options, iterEc), end;
             !iterEc && it != end;
             it.increment(iterEc)) {
            std::error_code typeEc;
            if (!it->is_directory(typeEc)) {
                DeleteUninstallSinglePath(state, it->path(), "delete_root_file");
            }
        }
    } else {
        std::vector<std::filesystem::path> deletedCandidates;
        std::set<std::string> seen;
        auto addFile = [&](const std::string& raw) {
            if (raw.empty()) {
                return;
            }
            std::filesystem::path path = PathFromUtf8(raw);
            if (!path.is_absolute() && !task.installDir.empty()) {
                path = PathFromUtf8(task.installDir) / path;
            }
            path = path.lexically_normal();
            if (IsPathUnderAnyRootLocal(path, isolatedOriginalRoots)) {
                return;
            }
            const std::string key = normalizePathForCompare(Utf8FromPath(path));
            if (!key.empty() && seen.insert(key).second) {
                deletedCandidates.push_back(path);
            }
        };
        for (const auto& file : task.files) {
            addFile(file);
        }
        addFile(task.uninstallPath);
        addFile(task.manifestPath);
        for (const auto& path : deletedCandidates) {
            if (normalizePathForCompare(Utf8FromPath(path)) == currentExeNorm) {
                ++state.result.skippedCount;
                continue;
            }
            DeleteUninstallSinglePath(state, path, "delete_manifest_file");
        }
        DeleteUninstallAffectedEmptyDirs(state, deletedCandidates, task.cleanupRoots, task.currentExePath);
    }
    for (const auto& pending : pendingDirs) {
        DeleteUninstallDirectorySegmented(state, pending, true, false);
    }
    if (!installRoot.empty()) {
        const bool currentExeInsideRoot =
            !task.currentExePath.empty() &&
            IsPathUnderOrEqualLocal(PathFromUtf8(task.currentExePath).lexically_normal(), installRoot);
        if (!currentExeInsideRoot) {
            DeleteUninstallDirectoryIfEmptyByRemove(state, installRoot, "delete_install_root");
        }
    }
    for (const auto& rule : task.cleanupPaths) {
        if (rule.path.empty()) {
            MarkUninstallSkipped(state);
            continue;
        }
        const auto cleanupPath = PathFromUtf8(rule.path).lexically_normal();
        if (IsDangerousFallbackRoot(cleanupPath)) {
            ++state.result.skippedCount;
            logInstallerWarning("[Uninstall][Cleanup] skipped unsafe cleanup path=" + Utf8FromPath(cleanupPath));
            continue;
        }
        DeleteUninstallPathSegmented(state, cleanupPath, rule.recursive, rule.onlyIfEmpty, true);
    }
    state.result.partial = state.result.failedCount > 0 || state.result.timedOut;
    state.result.success = state.task.policy.allowPartialSuccess || state.result.failedCount == 0;
    EmitUninstallHeartbeat(state, true);
    return state.result;
}

std::wstring QuoteUninstallArg(const std::wstring& value) {
    std::wstring quoted = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') {
            quoted += L'\\';
        }
        quoted += ch;
    }
    quoted += L"\"";
    return quoted;
}

UninstallCleanupResult RunUninstallCleanupWithWatchdog(
    UninstallCleanupTask task,
    const UninstallProgressCallback& progressCallback,
    const std::function<bool()>& cancellationCallback) {
    UninstallCleanupResult fallback;
#ifdef _WIN32
    const auto heartbeatPath = BuildUninstallCleanupTempPath(".heartbeat.json");
    task.heartbeatPath = Utf8FromPath(heartbeatPath);
    task.resultPath.clear();
    auto cleanupFuture = std::async(std::launch::async, [task = std::move(task)]() {
        return ExecuteUninstallCleanupTask(task);
    });
    const uint64_t startedMs = UninstallNowMs();
    uint64_t lastHeartbeatTimestamp = startedMs;
    std::string lastPath;
    std::string slowestPath;
    std::string slowestThreadId;
    uint32_t activeWorkers = 0;
    uint64_t processedCount = 0;
    uint64_t lastCompletedAt = 0;
    bool timedOut = false;
    bool cancelled = false;
    while (true) {
        if (cancellationCallback && cancellationCallback()) {
            fallback.success = false;
            fallback.message = "Uninstall cleanup cancelled";
            cancelled = true;
            break;
        }
        if (cleanupFuture.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready) {
            break;
        }
        json heartbeat;
        if (ReadUninstallJsonBestEffort(heartbeatPath, heartbeat)) {
            lastHeartbeatTimestamp = heartbeat.value("timestampMs", lastHeartbeatTimestamp);
            lastPath = heartbeat.value("currentPath", lastPath);
            slowestPath = heartbeat.value("slowestCurrentItem", slowestPath);
            slowestThreadId = heartbeat.value("slowestCurrentThreadId", slowestThreadId);
            activeWorkers = heartbeat.value("activeWorkers", activeWorkers);
            processedCount = heartbeat.value("processedCount", processedCount);
            lastCompletedAt = heartbeat.value("lastCompletedAt", lastCompletedAt);
            fallback.deletedCount = heartbeat.value("deletedCount", fallback.deletedCount);
            fallback.failedCount = heartbeat.value("failedCount", fallback.failedCount);
            fallback.skippedCount = heartbeat.value("skippedCount", fallback.skippedCount);
            if (progressCallback) {
                UninstallProgressInfo info;
                info.currentItem = lastPath.empty() ? "Cleaning files" : lastPath;
                info.progress = 0.5f;
                progressCallback(info);
            }
        }
        const uint64_t now = UninstallNowMs();
        const bool totalTimedOut = task.policy.totalTimeoutMs > 0 &&
            now - startedMs >= task.policy.totalTimeoutMs;
        const bool heartbeatStale = now > lastHeartbeatTimestamp &&
            now - lastHeartbeatTimestamp >= task.policy.itemStaleTimeoutMs;
        if (totalTimedOut || heartbeatStale) {
            fallback.success = task.policy.allowPartialSuccess;
            fallback.partial = true;
            fallback.timedOut = true;
            fallback.timedOutPath = slowestPath.empty() ? lastPath : slowestPath;
            fallback.message = totalTimedOut ? "Uninstall cleanup total timeout"
                                             : "Uninstall cleanup heartbeat stale";
            logInstallerWarning("[Uninstall][Cleanup] worker timeout message=" + fallback.message +
                                " currentPath=" + lastPath +
                                " slowestCurrentItem=" + slowestPath +
                                " slowestCurrentThreadId=" + slowestThreadId +
                                " activeWorkers=" + std::to_string(activeWorkers) +
                                " processedCount=" + std::to_string(processedCount) +
                                " lastCompletedAt=" + std::to_string(lastCompletedAt));
            timedOut = true;
            break;
        }
    }
    UninstallCleanupResult result = fallback;
    if (!timedOut && !cancelled) {
        result = cleanupFuture.get();
    } else {
        // Uninstall must not continue deleting in the background after the UI
        // reports completion, so wait for the in-process cleanup thread to end.
        cleanupFuture.wait();
    }
    std::error_code ec;
    std::filesystem::remove(heartbeatPath, ec);
    return result;
#else
    (void)progressCallback;
    (void)cancellationCallback;
    return ExecuteUninstallCleanupTask(task);
#endif
}

bool ResolveUninstallContext(const ExtendedInstallationMetadata* metadata,
                             InstallerPathResolver& resolver,
                             const std::string& explicitManifestPath,
                             UninstallContext& context) {
    context = UninstallContext{};
    if (metadata) {
        context.embeddedUninstallerCleanup = metadata->uninstallerCleanup;
        context.hasEmbeddedUninstallerCleanup = true;
        context.embeddedKillBeforeUninstall = metadata->uninstallerKillBeforeUninstall;
    }

    if (TryLoadManifestIntoContext(explicitManifestPath, context, true)) {
        return context.manifestReadable;
    }
    if (!context.errorMessage.empty() && !context.fallbackAllowed) {
        return false;
    }

    const std::string exePath = getCurrentExecutablePath();
    const std::string localManifest = getLocalManifestPath(exePath);
    if (TryLoadManifestIntoContext(localManifest, context, false)) {
        return context.manifestReadable;
    }
    if (!context.errorMessage.empty() && !context.fallbackAllowed) {
        return false;
    }

    if (metadata) {
        InstalledInstanceInfo installedInstance;
        std::string detectError;
        if (resolveInstalledInstanceFromInstallState(*metadata, resolver, installedInstance, &detectError)) {
            context.detectSource = installedInstance.detectSource;
            if (TryLoadManifestIntoContext(installedInstance.manifestPath, context, false)) {
                return context.manifestReadable;
            }
            const std::string policy = metadata->uninstallerCleanup.missingManifestFallback.empty()
                                           ? "safeDirectoryFallback"
                                           : metadata->uninstallerCleanup.missingManifestFallback;
            std::string normalizedPolicy = policy;
            std::transform(normalizedPolicy.begin(), normalizedPolicy.end(), normalizedPolicy.begin(),
                           [](unsigned char c) { return ToLowerAsciiChar(c); });
            if ((normalizedPolicy == "safedirectoryfallback" ||
                 normalizedPolicy == "none" ||
                 normalizedPolicy == "disabled") &&
                TrySetFallbackContext(installedInstance.installDir,
                                      metadata,
                                      context,
                                      installedInstance.manifestPath)) {
                return true;
            }
            context.fallbackPolicy = policy;
            context.errorMessage = normalizedPolicy == "fail"
                                       ? "Uninstall manifest missing; fallback is disabled by policy."
                                       : "Uninstall manifest missing; no directory cleanup fallback will be used.";
            return false;
        }
    }

    if (!exePath.empty()) {
        const std::filesystem::path installDir = PathFromUtf8(exePath).parent_path();
        if (TrySetFallbackContext(Utf8FromPath(installDir), metadata, context, localManifest)) {
            return true;
        }
    }

    if (context.errorMessage.empty()) {
        context.errorMessage = "Uninstall manifest missing; cannot uninstall.";
    }
    return false;
}

static bool ExecuteFallbackUninstall(const UninstallContext& context,
                                     const ExtendedInstallationMetadata* metadata,
                                     InstallerPathResolver& resolver,
                                     CliSupport& console,
                                     const UninstallProgressCallback& progressCallback,
                                     const std::function<bool()>& cancellationCallback) {
    (void)resolver;
    if (!context.fallbackAllowed || context.installDir.empty()) {
        console.showError(context.errorMessage.empty() ? "Unable to determine install directory; uninstall failed." : context.errorMessage);
        return false;
    }
    std::filesystem::path installDir = PathFromUtf8(context.installDir);
    if (IsDangerousFallbackRoot(installDir)) {
        console.showError("Unable to determine a safe install directory; uninstall failed.");
        return false;
    }

    console.showWarning("Manifest missing; running safe fallback uninstall for: " + context.installDir);
    std::string fallbackPolicy = context.fallbackPolicy.empty()
                                     ? "safeDirectoryFallback"
                                     : context.fallbackPolicy;
    std::transform(fallbackPolicy.begin(), fallbackPolicy.end(), fallbackPolicy.begin(),
                   [](unsigned char c) { return ToLowerAsciiChar(c); });

    const std::string displayName =
        !context.appName.empty() ? context.appName : (metadata ? metadata->appName : std::string());
    if (!displayName.empty()) {
        removeAutoStartup(displayName);
        deleteDesktopShortcut(displayName);
        deleteStartMenuShortcut(displayName);
    }
#ifdef _WIN32
    if (context.hasEmbeddedUninstallerCleanup) {
        const auto& systemEntry = context.embeddedUninstallerCleanup.systemUninstallEntry;
        if (!systemEntry.displayName.empty()) {
            deleteSystemUninstallEntryByDisplayName(systemEntry.displayName, systemEntry.scope);
        }
        for (const auto& legacyEntry : systemEntry.legacyEntries) {
            DeleteSystemUninstallCleanupItem(legacyEntry);
        }
    }
#endif
    if (context.hasEmbeddedUninstallerCleanup) {
        for (const auto& value : context.embeddedUninstallerCleanup.registry.deleteValues) {
            if (!deleteRegistryValue(value)) {
                console.showWarning("Fallback uninstall failed to remove registry value: " +
                                    value.path + "\\" + value.key);
            }
        }
        for (const auto& path : context.embeddedUninstallerCleanup.registry.deleteKeys) {
            if (!deleteRegistryPath(path)) {
                console.showWarning("Fallback uninstall failed to remove registry path: " + path);
            }
        }
    }

    if (fallbackPolicy == "safedirectoryfallback") {
        UninstallCleanupTask cleanupTask;
        cleanupTask.fallbackMode = true;
        cleanupTask.installDir = context.installDir;
        cleanupTask.manifestPath = context.manifestPath;
        cleanupTask.currentExePath = getCurrentExecutablePath();
        UninstallCleanupResult cleanupResult =
            RunUninstallCleanupWithWatchdog(cleanupTask, progressCallback, cancellationCallback);
        if (!cleanupResult.success || cleanupResult.partial) {
            console.showWarning("Fallback uninstall file cleanup completed with warnings: deleted=" +
                                std::to_string(cleanupResult.deletedCount) +
                                " failed=" + std::to_string(cleanupResult.failedCount) +
                                " skipped=" + std::to_string(cleanupResult.skippedCount) +
                                " timedOut=" + std::string(cleanupResult.timedOut ? "true" : "false"));
        }
    } else {
        console.showWarning("Fallback uninstall skipped directory cleanup by policy: " + fallbackPolicy);
    }

    std::filesystem::path exePath = PathFromUtf8(getCurrentExecutablePath());
    std::string exeName = Utf8FromPath(exePath.filename());
    std::transform(exeName.begin(), exeName.end(), exeName.begin(),
                   [](unsigned char c) { return ToLowerAsciiChar(c); });
    if (exeName == "uninstall.exe") {
        scheduleSelfDeleteImmediate({context.installDir}, context.manifestPath);
    }

    console.showInfo("Fallback uninstall completed");
    return true;
}

bool ExecuteUninstallFromContext(const UninstallContext& context,
                                 const ExtendedInstallationMetadata* metadata,
                                 InstallerPathResolver& resolver,
                                 CliSupport& console,
                                 const UninstallProgressCallback& progressCallback,
                                 const std::function<bool()>& cancellationCallback) {
    if (context.manifestReadable && !context.manifestPath.empty()) {
        return uninstallFromManifest(context.manifestPath,
                                     context.hasEmbeddedUninstallerCleanup ? &context.embeddedUninstallerCleanup : nullptr,
                                     &context.embeddedKillBeforeUninstall,
                                     resolver,
                                     console,
                                     progressCallback,
                                     cancellationCallback);
    }
    return ExecuteFallbackUninstall(context,
                                    metadata,
                                    resolver,
                                    console,
                                    progressCallback,
                                    cancellationCallback);
}

bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console) {
    return uninstallFromManifest(manifestPath, resolver, console, {}, {});
}

bool uninstallFromManifest(const std::string& manifestPath,
                           InstallerPathResolver& resolver,
                           CliSupport& console,
                           const UninstallProgressCallback& progressCallback,
                           const std::function<bool()>& cancellationCallback) {
    return uninstallFromManifest(manifestPath,
                                 nullptr,
                                 nullptr,
                                 resolver,
                                 console,
                                 progressCallback,
                                 cancellationCallback);
}

bool uninstallFromManifest(const std::string& manifestPath,
                           const UninstallerCleanupConfigV3* embeddedCleanup,
                           const std::vector<std::string>* embeddedKillBeforeUninstall,
                           InstallerPathResolver& resolver,
                           CliSupport& console,
                           const UninstallProgressCallback& progressCallback,
                           const std::function<bool()>& cancellationCallback) {
    auto isCancelled = [&]() {
        return cancellationCallback && cancellationCallback();
    };

    size_t totalUnits = 0;
    size_t completedUnits = 0;
    float reportedProgress = 0.0f;

    auto emitProgress = [&](const std::string& item) {
        if (!progressCallback) {
            return;
        }
        float progress = 1.0f;
        if (totalUnits > 0) {
            progress = static_cast<float>(completedUnits) / static_cast<float>(totalUnits);
        }
        if (progress < reportedProgress) {
            progress = reportedProgress;
        }
        if (progress > 1.0f) {
            progress = 1.0f;
        }
        reportedProgress = progress;

        UninstallProgressInfo info;
        info.progress = progress;
        info.currentItem = item;
        progressCallback(info);
    };

    auto addWorkUnits = [&](size_t units) {
        totalUnits += units;
    };

    auto completeWorkUnit = [&](const std::string& item) {
        if (completedUnits < totalUnits) {
            ++completedUnits;
        }
        emitProgress(item);
    };

    json manifest;
    if (!readManifest(manifestPath, manifest)) {
        console.showError("Failed to read manifest: " + manifestPath);
        return false;
    }
    std::string snapshotError;
    const bool hasV3Snapshot = ManifestHasV3Snapshot(manifest);
    if (!hasV3Snapshot) {
        console.showError("Uninstall manifest does not contain v3 uninstall snapshot.");
        return false;
    }
    if (!ValidateV3UninstallSnapshot(manifest, snapshotError)) {
        console.showError(snapshotError);
        return false;
    }
    console.showInfo("Loaded manifest: " + manifestPath);

    std::string appId = GetManifestAppId(manifest);
    std::string displayName = GetManifestDisplayName(manifest);
    std::string installDir = manifest.value("installDir", "");
    bool removedUninstall = false;
    std::string uninstallPath = manifest.value("uninstallPath", "");
    std::vector<std::string> installKillProcesses;
    if (hasV3Snapshot &&
        manifest["uninstaller"].contains("killBeforeUninstall") &&
        manifest["uninstaller"]["killBeforeUninstall"].is_array()) {
        for (const auto& item : manifest["uninstaller"]["killBeforeUninstall"]) {
            if (item.is_string()) {
                installKillProcesses.push_back(item.get<std::string>());
            }
        }
    } else if (manifest.contains("killProcesses")) {
        const auto& kill = manifest["killProcesses"];
        if (kill.is_array()) {
            for (const auto& item : kill) {
                if (item.is_string()) {
                    installKillProcesses.push_back(item.get<std::string>());
                }
            }
        } else if (kill.is_string()) {
            installKillProcesses.push_back(kill.get<std::string>());
        }
    }

    InstallStateConfig installState = GetManifestInstallState(manifest);
    const std::string installStateCleanupMode = GetManifestInstallStateCleanupMode(manifest);
    UninstallCleanupConfig uninstallCleanup = GetManifestUninstallCleanup(manifest);
    MergeEmbeddedUninstallerCleanup(uninstallCleanup, embeddedCleanup, embeddedKillBeforeUninstall);

    std::vector<ComponentExecutionRecord> componentActions;
    const json* componentActionNode = nullptr;
    if (hasV3Snapshot &&
        manifest["uninstaller"].contains("components") &&
        manifest["uninstaller"]["components"].is_object() &&
        manifest["uninstaller"]["components"].contains("actions") &&
        manifest["uninstaller"]["components"]["actions"].is_array()) {
        componentActionNode = &manifest["uninstaller"]["components"]["actions"];
    } else if (manifest.contains("componentActions") && manifest["componentActions"].is_array()) {
        componentActionNode = &manifest["componentActions"];
    }
    if (componentActionNode) {
        for (const auto& item : *componentActionNode) {
            if (!item.is_object()) {
                continue;
            }
            ComponentExecutionRecord record;
            record.componentId = item.value("componentId", "");
            record.sourceType = item.value("sourceType", "");
            record.uninstallCommand = item.value("uninstallCommand", "");
            record.workingDirectory = item.value("workingDirectory", "");
            record.wait = item.value("wait", true);
            record.timeoutSec = item.value("timeoutSec", static_cast<uint32_t>(900));
            if (!record.uninstallCommand.empty()) {
                componentActions.push_back(std::move(record));
            }
        }
    }

    std::vector<RegistryEntry> manifestRegistryEntries;

    std::vector<std::string> files;
    const json* filesNode = nullptr;
    if (hasV3Snapshot &&
        manifest["installer"].contains("payload") &&
        manifest["installer"]["payload"].is_object() &&
        manifest["installer"]["payload"].contains("files") &&
        manifest["installer"]["payload"]["files"].is_array()) {
        filesNode = &manifest["installer"]["payload"]["files"];
    } else if (manifest.contains("files") && manifest["files"].is_array()) {
        filesNode = &manifest["files"];
    }
    if (filesNode) {
        for (const auto& item : *filesNode) {
            if (item.is_string()) {
                files.push_back(item.get<std::string>());
            }
        }
    }
    console.showInfo("Manifest files: " + std::to_string(files.size()));
    std::vector<std::string> cleanupRoots;
    const json* rootsNode = nullptr;
    if (hasV3Snapshot &&
        manifest["installer"].contains("payload") &&
        manifest["installer"]["payload"].is_object() &&
        manifest["installer"]["payload"].contains("roots") &&
        manifest["installer"]["payload"]["roots"].is_array()) {
        rootsNode = &manifest["installer"]["payload"]["roots"];
    } else if (manifest.contains("cleanupRoots") && manifest["cleanupRoots"].is_array()) {
        rootsNode = &manifest["cleanupRoots"];
    }
    if (rootsNode) {
        for (const auto& item : *rootsNode) {
            if (item.is_string()) {
                cleanupRoots.push_back(item.get<std::string>());
            }
        }
    }
    if (cleanupRoots.empty() && !displayName.empty()) {
        std::string appLower = displayName;
        std::transform(appLower.begin(), appLower.end(), appLower.begin(),
                       [](unsigned char c) { return ToLowerAsciiChar(c); });
        for (const auto& file : files) {
            std::filesystem::path path = PathFromUtf8(file);
            for (const auto& part : path) {
                std::string partStr = Utf8FromPath(part);
                std::string partLower = partStr;
                std::transform(partLower.begin(), partLower.end(), partLower.begin(),
                               [](unsigned char c) { return ToLowerAsciiChar(c); });
                if (partLower == appLower) {
                    std::filesystem::path root;
                    for (const auto& build : path) {
                        root /= build;
                        if (build == part) {
                            cleanupRoots.push_back(Utf8FromPath(root));
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }
    if (!installDir.empty()) {
        cleanupRoots.push_back(installDir);
    }
    std::sort(cleanupRoots.begin(), cleanupRoots.end());
    cleanupRoots.erase(std::unique(cleanupRoots.begin(), cleanupRoots.end()), cleanupRoots.end());
    console.showInfo("Cleanup roots: " + std::to_string(cleanupRoots.size()));
    for (const auto& root : cleanupRoots) {
        console.showInfo("Cleanup root: " + root);
    }

    std::vector<std::string> explicitProcessNames;
    explicitProcessNames.reserve(uninstallCleanup.processes.size());
    for (const auto& process : uninstallCleanup.processes) {
        explicitProcessNames.push_back(process.name);
    }
    std::vector<std::string> killTargets = buildKillProcessList(displayName, installKillProcesses);
    for (const auto& processName : explicitProcessNames) {
        killTargets.push_back(normalizeProcessName(processName));
    }
    std::sort(killTargets.begin(), killTargets.end());
    killTargets.erase(std::unique(killTargets.begin(), killTargets.end()), killTargets.end());

    addWorkUnits(2); // installState: uninstalling + cleanup/mark
    if (!killTargets.empty()) {
        addWorkUnits(1);
    }
    addWorkUnits(uninstallCleanup.startup.size());
    addWorkUnits(uninstallCleanup.shortcuts.size());
    addWorkUnits(componentActions.size());
    addWorkUnits(manifestRegistryEntries.size());
    addWorkUnits(uninstallCleanup.registry.legacyKeys.size());
#ifdef _WIN32
    if (!uninstallCleanup.uninstallEntries.empty()) {
        addWorkUnits(1);
    }
#endif
    if (!uninstallPath.empty()) {
        addWorkUnits(1);
    }
    addWorkUnits(files.size());
    addWorkUnits(cleanupRoots.size() * 2); // scan root + remove root
    if (!manifestPath.empty()) {
        addWorkUnits(1);
    }
    addWorkUnits(uninstallCleanup.paths.size());

    emitProgress("Preparing old installation cleanup");

    if (isCancelled()) {
        console.showWarning("Uninstall cancelled before cleanup started");
        return false;
    }

    std::vector<std::string> running;
    if (!killTargets.empty()) {
        running = getRunningProcessesByName(killTargets);
        if (!running.empty()) {
            auto joinNames = [](const std::vector<std::string>& names) {
                std::string joined;
                for (size_t i = 0; i < names.size(); ++i) {
                    if (i > 0) {
                        joined += ", ";
                    }
                    joined += names[i];
                }
                return joined;
            };
            console.showInfo("Terminating processes: " + joinNames(running));
            terminateProcessesByName(running);
#ifdef _WIN32
            Sleep(500);
#endif
            std::vector<std::string> remaining = getRunningProcessesByName(killTargets);
            if (!remaining.empty()) {
                console.showWarning("Some processes are still running: " + joinNames(remaining));
            }
        }
        completeWorkUnit("Terminating running processes");
    }

    if (isCancelled()) {
        console.showWarning("Uninstall cancelled");
        return false;
    }

    ApplyInstallState(installState,
                      BuildUninstallInstallStateContext(installDir,
                                                        manifest.value("appVersion", ""),
                                                        displayName,
                                                        appId,
                                                        "uninstalling"),
                      resolver);
    completeWorkUnit("Marking uninstalling state");

    for (const auto& action : componentActions) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while running component uninstall actions");
            return false;
        }

        std::string label = action.componentId.empty() ? action.sourceType : action.componentId;
        if (label.empty()) {
            label = "component";
        }

#ifdef _WIN32
        DWORD exitCode = 0;
        std::string commandError;
        bool ok = executeShellCommandWithTimeout(action.uninstallCommand,
                                                 action.workingDirectory,
                                                 action.wait,
                                                 action.timeoutSec,
                                                 cancellationCallback,
                                                 exitCode,
                                                 commandError);
        if (!ok) {
            console.showWarning("Component uninstall command failed (" + label + "): " + commandError);
        } else if (action.wait && exitCode != 0) {
            console.showWarning("Component uninstall command returned non-zero exit code (" + label +
                                "): " + std::to_string(exitCode));
        } else {
            console.showInfo("Component uninstall command completed: " + label);
        }
#else
        console.showWarning("Component uninstall actions are supported on Windows only. skipped: " + label);
#endif

        completeWorkUnit("Replaying component uninstall action: " + label);
    }

    for (const auto& startup : uninstallCleanup.startup) {
        if (startup.name.empty()) {
            continue;
        }
        removeAutoStartup(startup.name);
        completeWorkUnit("Removing auto startup entry: " + startup.name);
    }

    for (const auto& shortcut : uninstallCleanup.shortcuts) {
        if (shortcut.name.empty()) {
            continue;
        }
        deleteDesktopShortcut(shortcut.name);
        deleteStartMenuShortcut(shortcut.name);
        completeWorkUnit("Removing desktop shortcut: " + shortcut.name);
    }

    for (const auto& entry : manifestRegistryEntries) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while removing registry values");
            return false;
        }
        if (!deleteRegistryValue(entry)) {
            console.showWarning("Failed to remove registry value: " +
                                (entry.key.empty() ? entry.path : (entry.path + "\\" + entry.key)));
        }
        std::string keyName = entry.key.empty() ? entry.path : (entry.path + "\\" + entry.key);
        completeWorkUnit("Removing registry value: " + keyName);
    }

    for (const auto& entry : uninstallCleanup.registry.legacyKeys) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while removing legacy registry items");
            return false;
        }
        if (entry.key.empty()) {
            if (!deleteRegistryPath(entry.path)) {
                console.showWarning("Failed to remove registry path: " + entry.path);
            }
        } else {
            if (!deleteRegistryValue(entry)) {
                console.showWarning("Failed to remove registry value: " + entry.path + "\\" + entry.key);
            }
        }
        std::string keyName = entry.key.empty() ? entry.path : (entry.path + "\\" + entry.key);
        completeWorkUnit("Removing legacy registry item: " + keyName);
    }

#ifdef _WIN32
    if (!uninstallCleanup.uninstallEntries.empty()) {
        for (const auto& entry : uninstallCleanup.uninstallEntries) {
            removedUninstall = DeleteUninstallEntryByScope(entry) || removedUninstall;
        }
        completeWorkUnit("Removing uninstall registry entry");
    }
#endif

    std::string currentExe = getCurrentExecutablePath();
    UninstallCleanupTask cleanupTask;
    cleanupTask.installDir = installDir;
    cleanupTask.manifestPath = manifestPath;
    cleanupTask.uninstallPath = uninstallPath;
    cleanupTask.currentExePath = currentExe;
    cleanupTask.files = files;
    cleanupTask.cleanupRoots = cleanupRoots;
    cleanupTask.cleanupPaths.reserve(uninstallCleanup.paths.size());
    for (const auto& rule : uninstallCleanup.paths) {
        UninstallCleanupRule expanded = rule;
        expanded.path = ExpandCleanupRulePath(rule, installDir, resolver);
        cleanupTask.cleanupPaths.push_back(std::move(expanded));
    }
    UninstallCleanupResult cleanupResult =
        RunUninstallCleanupWithWatchdog(cleanupTask, progressCallback, cancellationCallback);
    if (!cleanupResult.success && isCancelled()) {
        console.showWarning("Uninstall cancelled while cleaning files");
        return false;
    }
    if (!cleanupResult.success || cleanupResult.partial) {
        console.showWarning("Uninstall file cleanup completed with warnings: deleted=" +
                            std::to_string(cleanupResult.deletedCount) +
                            " failed=" + std::to_string(cleanupResult.failedCount) +
                            " skipped=" + std::to_string(cleanupResult.skippedCount) +
                            " timedOut=" + std::string(cleanupResult.timedOut ? "true" : "false"));
    } else {
        console.showInfo("Uninstall file cleanup completed: deleted=" +
                         std::to_string(cleanupResult.deletedCount));
    }
    completeWorkUnit("Cleaning installed files");

    CleanupInstallState(installState,
                        installStateCleanupMode,
                        BuildUninstallInstallStateContext(installDir,
                                                          manifest.value("appVersion", ""),
                                                          displayName,
                                                          appId,
                                                          "uninstalled"),
                        resolver);
    completeWorkUnit("Marking uninstalled state");

    for (const auto& entry : uninstallCleanup.registry.legacyKeys) {
        if (entry.key.empty()) {
            deleteRegistryPath(entry.path);
        } else {
            deleteRegistryValue(entry);
        }
    }

    std::filesystem::path exePath = PathFromUtf8(getCurrentExecutablePath());
    std::string exeName = Utf8FromPath(exePath.filename());
    std::transform(exeName.begin(), exeName.end(), exeName.begin(),
                   [](unsigned char c) { return ToLowerAsciiChar(c); });

    if (exeName == "uninstall.exe") {
        addWorkUnits(1);
        if (!scheduleSelfDeleteImmediate(cleanupRoots, manifestPath)) {
            if (!scheduleSelfDelete()) {
                console.showWarning("Failed to schedule uninstall.exe removal");
            }
        } else {
            console.showInfo("Scheduled immediate uninstall.exe removal");
        }
        completeWorkUnit("Scheduling self cleanup");
    }

    if (completedUnits < totalUnits) {
        completedUnits = totalUnits;
    }
    emitProgress("Old installation cleanup completed");

    console.showInfo("Uninstall completed");
    return true;
}

} // namespace MultiThreadedInstaller
