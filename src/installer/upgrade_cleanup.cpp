#include "installer/upgrade_cleanup.h"

#include "common/utf8_utils.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <set>
#include <system_error>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

using json = nlohmann::json;

bool IsCancelled(const std::function<bool()>& cancellationCallback) {
    return cancellationCallback && cancellationCallback();
}

void EmitProgress(const UpgradeCleanupProgressCallback& progressCallback,
                  float progress,
                  const std::string& item) {
    if (!progressCallback) {
        return;
    }
    UpgradeCleanupProgressInfo info;
    info.progress = progress;
    info.currentItem = item;
    progressCallback(info);
}

bool ReadManifestJson(const std::string& manifestPath, json& outManifest) {
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

std::string NormalizePath(const std::filesystem::path& path) {
    return normalizePathForCompare(Utf8FromPath(path.lexically_normal()));
}

bool IsPathUnderOrEqual(const std::filesystem::path& candidate,
                        const std::filesystem::path& root) {
    const std::string normalizedCandidate = NormalizePath(candidate);
    const std::string normalizedRoot = NormalizePath(root);
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

std::vector<std::string> CollectManifestFiles(const json& manifest) {
    std::vector<std::string> files;
    if (!manifest.contains("files") || !manifest["files"].is_array()) {
        return files;
    }
    for (const auto& item : manifest["files"]) {
        if (item.is_string()) {
            files.push_back(item.get<std::string>());
        }
    }
    return files;
}

std::vector<RegistryEntry> CollectManifestRegistryEntries(const json& manifest) {
    std::vector<RegistryEntry> entries;
    if (!manifest.contains("registry") || !manifest["registry"].is_array()) {
        return entries;
    }
    for (const auto& item : manifest["registry"]) {
        if (!item.is_object()) {
            continue;
        }
        RegistryEntry entry;
        entry.path = item.value("path", "");
        entry.key = item.value("key", "");
        entry.value = item.value("value", "");
        entry.type = static_cast<RegistryValueType>(item.value("type", static_cast<int>(RegistryValueType::STRING)));
        if (!entry.path.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

std::string GetManifestDisplayName(const json& manifest) {
    std::string displayName = manifest.value("displayName", "");
    if (!displayName.empty()) {
        return displayName;
    }
    return manifest.value("appName", "");
}

void AppendUniqueName(std::vector<std::string>& names,
                      std::set<std::string>& seen,
                      const std::string& value) {
    if (value.empty()) {
        return;
    }
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (seen.insert(lowered).second) {
        names.push_back(value);
    }
}

void AppendUniqueRegistryEntry(std::vector<RegistryEntry>& entries,
                               std::set<std::string>& seen,
                               const RegistryEntry& entry) {
    if (entry.path.empty()) {
        return;
    }
    std::string key = entry.path;
    key.push_back('\n');
    key += entry.key;
    key.push_back('\n');
    key += entry.value;
    key.push_back('\n');
    key += std::to_string(static_cast<int>(entry.type));
    if (seen.insert(key).second) {
        entries.push_back(entry);
    }
}

std::string ExpandInstallDirTokenLocal(const std::string& text, const std::string& installDir) {
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

bool IsSafeCleanupPath(const std::filesystem::path& path) {
    if (path.empty() || !path.is_absolute()) {
        return false;
    }
    const std::filesystem::path normalized = path.lexically_normal();
    const std::filesystem::path rootPath = normalized.root_path();
    if (rootPath.empty()) {
        return false;
    }
    if (normalized == rootPath) {
        return false;
    }
    const std::string normalizedUtf8 = Utf8FromPath(normalized);
    const std::string rootUtf8 = Utf8FromPath(rootPath);
    if (normalizedUtf8.size() <= rootUtf8.size()) {
        return false;
    }
    if (normalized.filename().empty()) {
        return false;
    }
    return true;
}

bool RemoveUpgradeCleanupPath(const UninstallCleanupRule& rule,
                              const std::string& previousInstallDir,
                              InstallerPathResolver& resolver,
                              CliSupport& console) {
    const std::string expanded =
        resolver.expandEnvironmentVariables(ExpandInstallDirTokenLocal(rule.path, previousInstallDir));
    if (expanded.empty()) {
        console.showWarning("Upgrade cleanup skipped empty path rule.");
        return false;
    }

    const std::filesystem::path cleanupPath = PathFromUtf8(expanded).lexically_normal();
    if (!IsSafeCleanupPath(cleanupPath)) {
        console.showWarning("Upgrade cleanup skipped unsafe path: " + Utf8FromPath(cleanupPath));
        return false;
    }

    std::error_code existsEc;
    if (!std::filesystem::exists(cleanupPath, existsEc)) {
        return true;
    }

    if (rule.onlyIfEmpty &&
        std::filesystem::is_directory(cleanupPath, existsEc) &&
        !std::filesystem::is_empty(cleanupPath, existsEc)) {
        console.showInfo("Upgrade cleanup skipped non-empty path: " + Utf8FromPath(cleanupPath));
        return false;
    }

    std::error_code removeEc;
    if (rule.recursive && std::filesystem::is_directory(cleanupPath, existsEc)) {
        std::filesystem::remove_all(toLongPath(cleanupPath), removeEc);
    } else {
        std::filesystem::remove(toLongPath(cleanupPath), removeEc);
    }
    if (removeEc && std::filesystem::exists(cleanupPath)) {
        console.showWarning("Upgrade cleanup failed to remove path: " + Utf8FromPath(cleanupPath));
        return false;
    }
    console.showInfo("Upgrade cleanup removed path: " + Utf8FromPath(cleanupPath));
    return true;
}

} // namespace

bool cleanupPreviousInstallForUpgrade(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const std::string& newInstallDir,
    CliSupport& console,
    const UpgradeCleanupProgressCallback& progressCallback,
    const std::function<bool()>& cancellationCallback) {
    std::filesystem::path previousRoot = PathFromUtf8(previousInstallDir).lexically_normal();
    if (previousInstallDir.empty() || previousRoot.empty()) {
        console.showWarning("Upgrade cleanup skipped: previous install directory is empty.");
        return false;
    }

    std::string normalizedOld = normalizePathForCompare(previousInstallDir);
    std::string normalizedNew = normalizePathForCompare(newInstallDir);
    if (!normalizedOld.empty() && !normalizedNew.empty() && normalizedOld == normalizedNew) {
        console.showInfo("Upgrade cleanup skipped: previous and new install directories are the same.");
        return true;
    }

    std::error_code existsEc;
    if (!std::filesystem::exists(previousRoot, existsEc)) {
        console.showInfo("Upgrade cleanup skipped: previous install directory not found.");
        return true;
    }
    if (!std::filesystem::is_directory(previousRoot, existsEc)) {
        console.showWarning("Upgrade cleanup skipped: previous install path is not a directory.");
        return false;
    }

    EmitProgress(progressCallback, 0.0f, "Preparing upgrade cleanup");
    if (IsCancelled(cancellationCallback)) {
        console.showWarning("Upgrade cleanup cancelled before start.");
        return false;
    }

    json manifest;
    std::vector<std::string> manifestFiles;
    if (ReadManifestJson(manifestPath, manifest)) {
        manifestFiles = CollectManifestFiles(manifest);
    } else {
        console.showWarning("Upgrade cleanup: failed to read previous manifest, fallback to minimal cleanup.");
    }

    std::vector<std::filesystem::path> filesToDelete;
    filesToDelete.reserve(manifestFiles.size() + 2);
    std::set<std::string> seen;

    auto tryAddFile = [&](const std::filesystem::path& path, bool requiredUnderOldRoot) {
        if (path.empty()) {
            return;
        }
        std::filesystem::path normalizedPath = path.lexically_normal();
        if (requiredUnderOldRoot && !IsPathUnderOrEqual(normalizedPath, previousRoot)) {
            console.showWarning("Upgrade cleanup skipped out-of-root file: " + Utf8FromPath(normalizedPath));
            return;
        }
        const std::string key = NormalizePath(normalizedPath);
        if (key.empty()) {
            return;
        }
        if (seen.insert(key).second) {
            filesToDelete.push_back(std::move(normalizedPath));
        }
    };

    for (const auto& file : manifestFiles) {
        if (file.empty()) {
            continue;
        }
        std::filesystem::path path = PathFromUtf8(file);
        if (!path.is_absolute()) {
            path = previousRoot / path;
        }
        tryAddFile(path, true);
    }

    tryAddFile(previousRoot / "uninstall.exe", true);
    tryAddFile(previousRoot / "install.manifest.json", true);

    const size_t totalFiles = filesToDelete.size();
    for (size_t i = 0; i < totalFiles; ++i) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade cleanup cancelled while deleting files.");
            return false;
        }
        const std::filesystem::path& filePath = filesToDelete[i];
        std::error_code removeEc;
        std::filesystem::remove(toLongPath(filePath), removeEc);
        if (removeEc && std::filesystem::exists(filePath)) {
            console.showWarning("Upgrade cleanup failed to remove file: " + Utf8FromPath(filePath));
        }

        float progress = 0.8f;
        if (totalFiles > 0) {
            progress = 0.8f * static_cast<float>(i + 1) / static_cast<float>(totalFiles);
        }
        EmitProgress(progressCallback, progress, "Removing old file: " + Utf8FromPath(filePath));
    }

    if (IsCancelled(cancellationCallback)) {
        console.showWarning("Upgrade cleanup cancelled before directory cleanup.");
        return false;
    }

    std::vector<std::filesystem::path> directories;
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(toLongPath(previousRoot), options)) {
        if (entry.is_directory()) {
            directories.push_back(entry.path());
        }
    }
    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
        return a.native().size() > b.native().size();
    });

    for (const auto& dir : directories) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade cleanup cancelled while removing empty directories.");
            return false;
        }
        std::error_code ec;
        if (std::filesystem::is_empty(dir, ec)) {
            std::filesystem::remove(toLongPath(dir), ec);
        }
    }
    EmitProgress(progressCallback, 0.95f, "Removing empty directories from previous install root");

    std::error_code rootEc;
    if (std::filesystem::is_empty(previousRoot, rootEc)) {
        std::filesystem::remove(toLongPath(previousRoot), rootEc);
        if (!rootEc) {
            console.showInfo("Removed previous install root: " + Utf8FromPath(previousRoot));
        }
    } else {
        console.showInfo("Previous install root is not empty after upgrade cleanup: " +
                         Utf8FromPath(previousRoot));
    }

    EmitProgress(progressCallback, 1.0f, "Upgrade cleanup completed");
    return true;
}

bool cleanupUpgradeSystemArtifacts(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const ExtendedInstallationMetadata& metadata,
    InstallerPathResolver& resolver,
    CliSupport& console,
    const UpgradeCleanupProgressCallback& progressCallback,
    const std::function<bool()>& cancellationCallback) {
    json manifest;
    const bool hasManifest = ReadManifestJson(manifestPath, manifest);
    if (!hasManifest && !manifestPath.empty()) {
        console.showWarning("Upgrade cleanup: failed to read previous manifest for system cleanup.");
    }

    EmitProgress(progressCallback, 0.0f, "Preparing upgrade system cleanup");
    if (IsCancelled(cancellationCallback)) {
        console.showWarning("Upgrade system cleanup cancelled before start.");
        return false;
    }

    std::vector<std::string> autoStartupNames;
    std::set<std::string> seenNames;
    if (hasManifest) {
        AppendUniqueName(autoStartupNames, seenNames, GetManifestDisplayName(manifest));
    }
    for (const auto& legacyId : metadata.legacyAppIds) {
        AppendUniqueName(autoStartupNames, seenNames, legacyId);
    }
    AppendUniqueName(autoStartupNames, seenNames, metadata.applicationName);

    const size_t totalSteps = autoStartupNames.size() +
                              metadata.upgradeCleanup.registry.legacyKeys.size() +
                              metadata.upgradeCleanup.extraPaths.size() +
                              (metadata.upgradeCleanup.registry.deleteFromManifest
                                   ? CollectManifestRegistryEntries(manifest).size()
                                   : 0);
    size_t completedSteps = 0;
    auto emitStep = [&](const std::string& item) {
        ++completedSteps;
        float progress = totalSteps == 0
                             ? 1.0f
                             : static_cast<float>(completedSteps) / static_cast<float>(totalSteps);
        EmitProgress(progressCallback, progress, item);
    };

    for (const auto& name : autoStartupNames) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing auto startup.");
            return false;
        }
        if (removeAutoStartup(name)) {
            console.showInfo("Upgrade cleanup removed auto startup: " + name);
        }
        emitStep("Removing legacy auto startup: " + name);
    }

    std::vector<RegistryEntry> registryEntries;
    std::set<std::string> seenRegistry;
    if (metadata.upgradeCleanup.registry.deleteFromManifest && hasManifest) {
        for (const auto& entry : CollectManifestRegistryEntries(manifest)) {
            AppendUniqueRegistryEntry(registryEntries, seenRegistry, entry);
        }
    }
    for (const auto& entry : metadata.upgradeCleanup.registry.legacyKeys) {
        AppendUniqueRegistryEntry(registryEntries, seenRegistry, entry);
    }

    for (const auto& entry : registryEntries) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing registry.");
            return false;
        }
        bool removed = false;
        if (entry.key.empty()) {
            removed = deleteRegistryPath(entry.path);
            if (!removed) {
                console.showWarning("Upgrade cleanup failed to remove registry path: " + entry.path);
            }
        } else {
            removed = deleteRegistryValue(entry);
            if (!removed) {
                console.showWarning("Upgrade cleanup failed to remove registry value: " +
                                    entry.path + "\\" + entry.key);
            }
        }
        (void)removed;
        emitStep("Removing legacy registry entry");
    }

    for (const auto& rule : metadata.upgradeCleanup.extraPaths) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing extra paths.");
            return false;
        }
        RemoveUpgradeCleanupPath(rule, previousInstallDir, resolver, console);
        emitStep("Removing upgrade cleanup path: " + rule.path);
    }

    if (totalSteps == 0) {
        EmitProgress(progressCallback, 1.0f, "Upgrade system cleanup completed");
    } else {
        EmitProgress(progressCallback, 1.0f, "Upgrade system cleanup completed");
    }
    return true;
}

} // namespace MultiThreadedInstaller
