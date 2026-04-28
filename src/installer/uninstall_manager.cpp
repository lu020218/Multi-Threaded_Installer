#include "installer/uninstall_manager.h"
#include "installer/install_state_utils.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <limits>
#include <unordered_set>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

using json = nlohmann::json;

static std::string GetManifestAppId(const json& manifest) {
    std::string appId = manifest.value("appId", "");
    if (!appId.empty()) {
        return appId;
    }
    return manifest.value("appName", "");
}

static std::string GetManifestDisplayName(const json& manifest) {
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
    if (!manifest.contains("lifecycleUninstallCleanup") ||
        !manifest["lifecycleUninstallCleanup"].is_object()) {
        return cleanup;
    }

    const auto& node = manifest["lifecycleUninstallCleanup"];
    if (node.contains("processes")) {
        cleanup.processes = GetManifestNamedEntries(node["processes"]);
    }
    if (node.contains("registry") && node["registry"].is_object() &&
        node["registry"].contains("legacyKeys")) {
        cleanup.registry.legacyKeys = GetManifestRegistryEntries(node["registry"]["legacyKeys"]);
    }
    if (node.contains("uninstallEntries") && node["uninstallEntries"].is_object() &&
        node["uninstallEntries"].contains("entries")) {
        cleanup.uninstallEntries = GetManifestUninstallEntries(node["uninstallEntries"]["entries"]);
    }
    if (node.contains("shortcuts")) {
        cleanup.shortcuts = GetManifestNamedEntries(node["shortcuts"]);
    }
    if (node.contains("startup")) {
        cleanup.startup = GetManifestNamedEntries(node["startup"]);
    }
    if (node.contains("paths")) {
        cleanup.paths = GetManifestCleanupRules(node["paths"]);
    }
    return cleanup;
}

static InstallInfoConfig GetManifestInstallInfo(const json& manifest) {
    InstallInfoConfig config;
    if (!manifest.contains("installInfo") || !manifest["installInfo"].is_object()) {
        return config;
    }
    const auto& node = manifest["installInfo"];
    config.mode = static_cast<InstallStateMode>(
        node.value("mode", static_cast<int>(InstallStateMode::REGISTRY)));
    config.path = node.value("path", "");
    if (node.contains("values") && node["values"].is_object()) {
        for (auto it = node["values"].begin(); it != node["values"].end(); ++it) {
            if (!it.value().is_object()) {
                continue;
            }
            InstallInfoValueConfig value;
            value.key = it.value().value("key", "");
            value.value = it.value().value("value", "");
            value.type = static_cast<RegistryValueType>(
                it.value().value("type", static_cast<int>(RegistryValueType::STRING)));
            config.values[it.key()] = std::move(value);
        }
    }
    return config;
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

static bool IsDirectoryEmptySafe(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec) && std::filesystem::is_empty(path, ec);
}

static std::string ExpandCleanupRulePath(const UninstallCleanupRule& rule,
                                         const std::string& installDir,
                                         InstallerPathResolver& resolver) {
    std::string expanded = ExpandInstallDirTokenLocal(rule.path, installDir);
    return resolver.expandEnvironmentVariables(expanded);
}

#ifdef _WIN32
static bool DeleteUninstallEntryByScope(const UninstallEntryCleanup& entry) {
    switch (entry.scope) {
    case UninstallEntryScope::CURRENT_USER:
        return deleteUninstallRegistryEntry(entry.name, false);
    case UninstallEntryScope::LOCAL_MACHINE:
    case UninstallEntryScope::WOW6432:
        return deleteUninstallRegistryEntry(entry.name, true);
    case UninstallEntryScope::ANY:
    default:
        return deleteUninstallRegistryEntry(entry.name, false) ||
               deleteUninstallRegistryEntry(entry.name, true);
    }
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
    console.showInfo("Loaded manifest: " + manifestPath);

    std::string appId = GetManifestAppId(manifest);
    std::string displayName = GetManifestDisplayName(manifest);
    std::string installDir = manifest.value("installDir", "");
    bool installAutoStartup = manifest.value("installAutoStartup", false);
    bool installDesktopIcon = manifest.value("installDesktopIcon", false);
    std::string desktopShortcutDisplayName = manifest.value("desktopShortcutDisplayName", "");
    bool removedUninstall = false;
    std::string uninstallPath = manifest.value("uninstallPath", "");
    std::vector<std::string> installKillProcesses;
    if (manifest.contains("killProcesses")) {
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

    InstallInfoConfig installInfo = GetManifestInstallInfo(manifest);
    UninstallCleanupConfig uninstallCleanup = GetManifestUninstallCleanup(manifest);

    std::vector<ComponentExecutionRecord> componentActions;
    if (manifest.contains("componentActions") && manifest["componentActions"].is_array()) {
        for (const auto& item : manifest["componentActions"]) {
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
    if (manifest.contains("lifecycleInstallRegistry") && manifest["lifecycleInstallRegistry"].is_array()) {
        for (const auto& reg : manifest["lifecycleInstallRegistry"]) {
            RegistryEntry entry;
            entry.path = reg.value("path", "");
            entry.key = reg.value("key", "");
            manifestRegistryEntries.push_back(entry);
        }
    }

    std::vector<std::string> files;
    if (manifest.contains("files") && manifest["files"].is_array()) {
        for (const auto& item : manifest["files"]) {
            if (item.is_string()) {
                files.push_back(item.get<std::string>());
            }
        }
    }
    console.showInfo("Manifest files: " + std::to_string(files.size()));
    std::vector<std::string> cleanupRoots;
    if (manifest.contains("cleanupRoots") && manifest["cleanupRoots"].is_array()) {
        for (const auto& item : manifest["cleanupRoots"]) {
            if (item.is_string()) {
                cleanupRoots.push_back(item.get<std::string>());
            }
        }
    }
    if (cleanupRoots.empty() && !displayName.empty()) {
        std::string appLower = displayName;
        std::transform(appLower.begin(), appLower.end(), appLower.begin(), ::tolower);
        for (const auto& file : files) {
            std::filesystem::path path = PathFromUtf8(file);
            for (const auto& part : path) {
                std::string partStr = Utf8FromPath(part);
                std::string partLower = partStr;
                std::transform(partLower.begin(), partLower.end(), partLower.begin(), ::tolower);
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

    addWorkUnits(2); // install info: uninstalling + uninstalled
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

    applyCoreInstallInfo(installInfo,
                         installDir,
                         manifest.value("appVersion", ""),
                         displayName,
                         "uninstalling",
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
        deleteRegistryValue(entry);
        std::string keyName = entry.key.empty() ? entry.path : (entry.path + "\\" + entry.key);
        completeWorkUnit("Removing registry value: " + keyName);
    }

    for (const auto& entry : uninstallCleanup.registry.legacyKeys) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while removing legacy registry items");
            return false;
        }
        if (entry.key.empty()) {
            deleteRegistryPath(entry.path);
        } else {
            deleteRegistryValue(entry);
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
    std::string currentExeNorm = normalizePathForCompare(currentExe);
    std::string uninstallPathNorm = normalizePathForCompare(uninstallPath);
    if (!uninstallPath.empty()) {
        if (!currentExe.empty() && uninstallPathNorm == currentExeNorm) {
            console.showInfo("Skipping uninstall.exe removal (currently running).");
        } else {
            std::filesystem::path path = PathFromUtf8(uninstallPath);
            std::error_code removeEc;
            std::filesystem::remove(toLongPath(path), removeEc);
            if (removeEc && std::filesystem::exists(path)) {
                console.showWarning("Failed to remove uninstall.exe: " + uninstallPath);
            } else if (!removeEc) {
                console.showInfo("Removed uninstall.exe: " + uninstallPath);
            }
        }
        completeWorkUnit("Removing uninstall executable");
    }

    for (const auto& file : files) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while deleting files");
            return false;
        }
        std::string fileNorm = normalizePathForCompare(file);
        if (!currentExe.empty() && fileNorm == currentExeNorm) {
            console.showInfo("Skipping file removal (current exe): " + file);
            completeWorkUnit("Skipping current running executable");
            continue;
        }
        std::filesystem::path path = PathFromUtf8(file);
        std::error_code removeEc;
        std::filesystem::remove(toLongPath(path), removeEc);
        if (removeEc && std::filesystem::exists(path)) {
            console.showWarning("Failed to remove file: " + file);
        }
        completeWorkUnit("Removing old file: " + file);
    }

    auto hasNonIgnoredFiles = [](const std::filesystem::path& rootPath) -> bool {
        if (!std::filesystem::exists(rootPath)) {
            return false;
        }
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(toLongPath(rootPath), options)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string name = Utf8FromPath(entry.path().filename());
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name == "desktop.ini" || name == "thumbs.db") {
                continue;
            }
            return true;
        }
        return false;
    };

    for (const auto& root : cleanupRoots) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while cleaning directories");
            return false;
        }

        std::filesystem::path rootPath = PathFromUtf8(root);
        completeWorkUnit("Scanning old directory: " + root);

        if (!std::filesystem::exists(rootPath)) {
            completeWorkUnit("Directory already removed: " + root);
            continue;
        }

        std::vector<std::filesystem::path> emptyDirs;
        std::vector<std::filesystem::path> ignoredFiles;
        auto cleanupStart = std::chrono::steady_clock::now();
        console.showInfo("Cleanup dirs start: " + root);
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(toLongPath(rootPath), options)) {
            if (entry.is_regular_file()) {
                std::string name = Utf8FromPath(entry.path().filename());
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "desktop.ini" || name == "thumbs.db") {
                    ignoredFiles.push_back(entry.path());
                    addWorkUnits(1);
                }
            }
            if (entry.is_directory()) {
                emptyDirs.push_back(entry.path());
                addWorkUnits(1);
            }
            if (isCancelled()) {
                console.showWarning("Uninstall cancelled during directory scan");
                return false;
            }
        }

        for (const auto& ignored : ignoredFiles) {
            std::error_code ec;
            std::filesystem::remove(toLongPath(ignored), ec);
            completeWorkUnit("Removing ignored file: " + Utf8FromPath(ignored));
        }

        std::sort(emptyDirs.begin(), emptyDirs.end(), [](const auto& a, const auto& b) {
            return a.native().size() > b.native().size();
        });
        for (const auto& dir : emptyDirs) {
            if (isCancelled()) {
                console.showWarning("Uninstall cancelled during directory removal");
                return false;
            }
            std::error_code ec;
            std::filesystem::remove(toLongPath(dir), ec);
            if (ec && std::filesystem::exists(dir)) {
                console.showWarning("Failed to remove empty directory: " + Utf8FromPath(dir));
            }
            completeWorkUnit("Removing old directory: " + Utf8FromPath(dir));
        }

        std::error_code rootEc;
        std::filesystem::remove(toLongPath(rootPath), rootEc);
        if (rootEc && std::filesystem::exists(rootPath)) {
            console.showWarning("Failed to remove root directory: " + Utf8FromPath(rootPath));
        }

        auto cleanupEnd = std::chrono::steady_clock::now();
        auto cleanupMs = std::chrono::duration_cast<std::chrono::milliseconds>(cleanupEnd - cleanupStart).count();
        console.showInfo("Cleanup dirs done: " + root + " (" + std::to_string(cleanupMs) + " ms)");

        if (!hasNonIgnoredFiles(rootPath)) {
            std::error_code removeEc;
            std::filesystem::remove_all(toLongPath(rootPath), removeEc);
            if (removeEc && std::filesystem::exists(rootPath)) {
                console.showWarning("Failed to remove empty root tree: " + Utf8FromPath(rootPath));
            } else if (!removeEc) {
                console.showInfo("Removed empty root tree: " + Utf8FromPath(rootPath));
            }
        } else {
            console.showWarning("Root not empty after cleanup: " + Utf8FromPath(rootPath));
        }

        completeWorkUnit("Removing install root: " + root);
    }

    for (const auto& rule : uninstallCleanup.paths) {
        if (isCancelled()) {
            console.showWarning("Uninstall cancelled while running cleanup rules");
            return false;
        }
        const std::string expandedPath = ExpandCleanupRulePath(rule, installDir, resolver);
        if (expandedPath.empty()) {
            continue;
        }
        std::filesystem::path cleanupPath = PathFromUtf8(expandedPath);
        std::error_code existsEc;
        if (!std::filesystem::exists(cleanupPath, existsEc)) {
            continue;
        }

        if (rule.onlyIfEmpty) {
            if (std::filesystem::is_directory(cleanupPath, existsEc) &&
                !IsDirectoryEmptySafe(cleanupPath)) {
                console.showInfo("Skipping non-empty cleanup path: " + expandedPath);
                continue;
            }
        }

        std::error_code removeEc;
        if (rule.recursive && std::filesystem::is_directory(cleanupPath, existsEc)) {
            std::filesystem::remove_all(toLongPath(cleanupPath), removeEc);
        } else {
            std::filesystem::remove(toLongPath(cleanupPath), removeEc);
        }

        if (removeEc && std::filesystem::exists(cleanupPath)) {
            console.showWarning("Failed to remove cleanup path: " + expandedPath);
        } else {
            console.showInfo("Removed cleanup path: " + expandedPath);
        }
        completeWorkUnit("Removing cleanup path: " + expandedPath);
    }

    applyCoreInstallInfo(installInfo,
                         installDir,
                         manifest.value("appVersion", ""),
                         displayName,
                         "uninstalled",
                         resolver);
    completeWorkUnit("Marking uninstalled state");

    if (!manifestPath.empty()) {
        if (!std::filesystem::remove(toLongPath(PathFromUtf8(manifestPath)))) {
            if (std::filesystem::exists(PathFromUtf8(manifestPath))) {
                console.showWarning("Failed to remove manifest: " + manifestPath);
            }
        }
        completeWorkUnit("Removing uninstall manifest");
    }

    std::filesystem::path exePath = PathFromUtf8(getCurrentExecutablePath());
    std::string exeName = Utf8FromPath(exePath.filename());
    std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
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
