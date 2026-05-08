#include "installer/upgrade_cleanup.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <set>
#include <cstdlib>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

namespace {

using json = nlohmann::json;

enum class UpgradeCleanupTaskMode {
    PreviousInstall,
    ExtraPaths,
};

struct UpgradeCleanupTask {
    UpgradeCleanupTaskMode mode = UpgradeCleanupTaskMode::PreviousInstall;
    std::string previousInstallDir;
    std::string newInstallDir;
    std::string manifestPath;
    std::string heartbeatPath;
    std::string resultPath;
    UpgradeCleanupPolicy policy;
    std::vector<UninstallCleanupRule> extraPaths;
};

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
    if (!manifest.contains("lifecycleInstallRegistry") || !manifest["lifecycleInstallRegistry"].is_array()) {
        return entries;
    }
    for (const auto& item : manifest["lifecycleInstallRegistry"]) {
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

std::string ReadEnvironmentPath(const char* name) {
    if (!name || name[0] == '\0') {
        return {};
    }
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

bool SameNormalizedPath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return normalizePathForCompare(Utf8FromPath(left.lexically_normal())) ==
           normalizePathForCompare(Utf8FromPath(right.lexically_normal()));
}

bool IsProtectedFullCleanupRoot(const std::filesystem::path& path) {
    if (!IsSafeCleanupPath(path)) {
        return true;
    }

    const std::filesystem::path normalized = path.lexically_normal();
    const std::vector<std::string> protectedExactRoots = {
        ReadEnvironmentPath("ProgramFiles"),
        ReadEnvironmentPath("ProgramFiles(x86)"),
        ReadEnvironmentPath("ProgramW6432"),
        ReadEnvironmentPath("ProgramData"),
    };
    for (const auto& root : protectedExactRoots) {
        if (!root.empty() && SameNormalizedPath(normalized, PathFromUtf8(root))) {
            return true;
        }
    }

    const std::vector<std::string> protectedSubtrees = {
        ReadEnvironmentPath("SystemRoot"),
        ReadEnvironmentPath("WINDIR"),
    };
    for (const auto& root : protectedSubtrees) {
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path protectedRoot = PathFromUtf8(root).lexically_normal();
        if (IsPathUnderOrEqual(normalized, protectedRoot)) {
            return true;
        }
    }

    return false;
}

bool IsReparsePointPath(const std::filesystem::path& path) {
#ifdef _WIN32
    const DWORD attrs = GetFileAttributesW(toLongPath(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == FILE_ATTRIBUTE_REPARSE_POINT;
#else
    std::error_code ec;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec));
#endif
}

uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

json PolicyToJson(const UpgradeCleanupPolicy& policy) {
    return {
        {"itemStaleTimeoutMs", policy.itemStaleTimeoutMs},
        {"totalTimeoutMs", policy.totalTimeoutMs},
        {"heartbeatIntervalMs", policy.heartbeatIntervalMs},
        {"heartbeatEveryItems", policy.heartbeatEveryItems},
        {"slowItemLogMs", policy.slowItemLogMs},
        {"allowPartialSuccess", policy.allowPartialSuccess},
    };
}

UpgradeCleanupPolicy PolicyFromJson(const json& value) {
    UpgradeCleanupPolicy policy;
    if (!value.is_object()) {
        return policy;
    }
    policy.itemStaleTimeoutMs = value.value("itemStaleTimeoutMs", policy.itemStaleTimeoutMs);
    policy.totalTimeoutMs = value.value("totalTimeoutMs", policy.totalTimeoutMs);
    policy.heartbeatIntervalMs = value.value("heartbeatIntervalMs", policy.heartbeatIntervalMs);
    policy.heartbeatEveryItems = value.value("heartbeatEveryItems", policy.heartbeatEveryItems);
    policy.slowItemLogMs = value.value("slowItemLogMs", policy.slowItemLogMs);
    policy.allowPartialSuccess = value.value("allowPartialSuccess", policy.allowPartialSuccess);
    return policy;
}

json CleanupRuleToJson(const UninstallCleanupRule& rule) {
    return {
        {"path", rule.path},
        {"recursive", rule.recursive},
        {"onlyIfEmpty", rule.onlyIfEmpty},
    };
}

UninstallCleanupRule CleanupRuleFromJson(const json& value) {
    UninstallCleanupRule rule;
    if (!value.is_object()) {
        return rule;
    }
    rule.path = value.value("path", "");
    rule.recursive = value.value("recursive", true);
    rule.onlyIfEmpty = value.value("onlyIfEmpty", false);
    return rule;
}

json TaskToJson(const UpgradeCleanupTask& task) {
    json root;
    root["mode"] = task.mode == UpgradeCleanupTaskMode::ExtraPaths ? "extraPaths" : "previousInstall";
    root["previousInstallDir"] = task.previousInstallDir;
    root["newInstallDir"] = task.newInstallDir;
    root["manifestPath"] = task.manifestPath;
    root["heartbeatPath"] = task.heartbeatPath;
    root["resultPath"] = task.resultPath;
    root["policy"] = PolicyToJson(task.policy);
    root["extraPaths"] = json::array();
    for (const auto& rule : task.extraPaths) {
        root["extraPaths"].push_back(CleanupRuleToJson(rule));
    }
    return root;
}

bool TaskFromJson(const json& root, UpgradeCleanupTask& task) {
    if (!root.is_object()) {
        return false;
    }
    const std::string mode = root.value("mode", "previousInstall");
    task.mode = mode == "extraPaths" ? UpgradeCleanupTaskMode::ExtraPaths
                                     : UpgradeCleanupTaskMode::PreviousInstall;
    task.previousInstallDir = root.value("previousInstallDir", "");
    task.newInstallDir = root.value("newInstallDir", "");
    task.manifestPath = root.value("manifestPath", "");
    task.heartbeatPath = root.value("heartbeatPath", "");
    task.resultPath = root.value("resultPath", "");
    task.policy = PolicyFromJson(root.value("policy", json::object()));
    task.extraPaths.clear();
    for (const auto& item : root.value("extraPaths", json::array())) {
        task.extraPaths.push_back(CleanupRuleFromJson(item));
    }
    return true;
}

bool WriteJsonFileBestEffort(const std::filesystem::path& path, const json& value) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(toLongPath(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << value.dump();
    return out.good();
}

bool ReadJsonFileBestEffort(const std::filesystem::path& path, json& value) {
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

std::filesystem::path BuildCleanupTempPath(const char* suffix) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "MTInstaller";
    std::ostringstream name;
    name << "cleanup_";
#ifdef _WIN32
    name << GetCurrentProcessId();
#else
    name << std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
    name << "_" << NowMs() << suffix;
    return dir / PathFromUtf8(name.str());
}

struct CleanupExecutionState {
    UpgradeCleanupTask task;
    UpgradeCleanupResult result;
    UpgradeCleanupProgressCallback progressCallback;
    std::string currentPath;
    std::string currentAction;
    uint64_t currentStartedMs = 0;
    uint64_t lastHeartbeatMs = 0;
    uint64_t processedSinceHeartbeat = 0;
};

void EmitCleanupHeartbeat(CleanupExecutionState& state, bool force) {
    if (state.task.heartbeatPath.empty()) {
        return;
    }

    const uint64_t now = NowMs();
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
    };
    WriteJsonFileBestEffort(PathFromUtf8(state.task.heartbeatPath), heartbeat);
    state.lastHeartbeatMs = now;
    state.processedSinceHeartbeat = 0;
}

void EmitCleanupProgress(CleanupExecutionState& state, float progress, const std::string& item) {
    EmitProgress(state.progressCallback, progress, item);
}

void MarkSkipped(CleanupExecutionState& state) {
    ++state.result.skippedCount;
    ++state.processedSinceHeartbeat;
    EmitCleanupHeartbeat(state, false);
}

void DeleteSinglePath(CleanupExecutionState& state,
                      const std::filesystem::path& path,
                      const std::string& action) {
    if (path.empty()) {
        MarkSkipped(state);
        return;
    }
    if (IsReparsePointPath(path)) {
        ++state.result.skippedCount;
        ++state.processedSinceHeartbeat;
        logInstallerWarning("[InstallFlow][Cleanup] skipped reparse point path=" + Utf8FromPath(path));
        EmitCleanupHeartbeat(state, false);
        return;
    }

    state.currentPath = Utf8FromPath(path);
    state.currentAction = action;
    state.currentStartedMs = NowMs();
    EmitCleanupHeartbeat(state, true);

    std::error_code ec;
    std::filesystem::remove(toLongPath(path), ec);
    const uint64_t elapsed = NowMs() - state.currentStartedMs;
    if (ec && std::filesystem::exists(path)) {
        ++state.result.failedCount;
        logInstallerWarning("[InstallFlow][Cleanup] delete failed path=" + Utf8FromPath(path) +
                            " error=" + ec.message());
    } else {
        ++state.result.deletedCount;
        if (elapsed >= state.task.policy.slowItemLogMs) {
            logInstallerWarning("[InstallFlow][Cleanup] slow delete path=" + Utf8FromPath(path) +
                                " elapsedMs=" + std::to_string(elapsed));
        }
    }
    ++state.processedSinceHeartbeat;
    EmitCleanupHeartbeat(state, false);
}

void DeleteDirectoryContentsSegmented(CleanupExecutionState& state,
                                      const std::filesystem::path& root,
                                      bool removeRoot,
                                      bool onlyIfEmpty) {
    std::error_code existsEc;
    if (!std::filesystem::exists(root, existsEc)) {
        MarkSkipped(state);
        return;
    }
    if (IsReparsePointPath(root)) {
        MarkSkipped(state);
        logInstallerWarning("[InstallFlow][Cleanup] skipped reparse point directory=" + Utf8FromPath(root));
        return;
    }
    if (onlyIfEmpty && !std::filesystem::is_empty(root, existsEc)) {
        MarkSkipped(state);
        return;
    }

    std::vector<std::filesystem::path> directories;
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    std::error_code iterEc;
    for (std::filesystem::recursive_directory_iterator it(toLongPath(root), options, iterEc), end;
         !iterEc && it != end;
         it.increment(iterEc)) {
        const std::filesystem::path entryPath = it->path();
        if (IsReparsePointPath(entryPath)) {
            it.disable_recursion_pending();
            ++state.result.skippedCount;
            continue;
        }
        std::error_code typeEc;
        if (it->is_directory(typeEc)) {
            directories.push_back(entryPath);
        } else {
            DeleteSinglePath(state, entryPath, "delete_file");
        }
    }
    if (iterEc) {
        ++state.result.failedCount;
        logInstallerWarning("[InstallFlow][Cleanup] directory scan failed path=" + Utf8FromPath(root) +
                            " error=" + iterEc.message());
    }

    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
        return a.native().size() > b.native().size();
    });
    for (const auto& dir : directories) {
        std::error_code emptyEc;
        if (std::filesystem::is_empty(dir, emptyEc)) {
            DeleteSinglePath(state, dir, "delete_empty_dir");
        }
    }
    if (removeRoot) {
        std::error_code emptyEc;
        if (std::filesystem::is_empty(root, emptyEc)) {
            DeleteSinglePath(state, root, "delete_root_dir");
        }
    }
}

void DeletePathSegmented(CleanupExecutionState& state,
                         const std::filesystem::path& path,
                         bool recursive,
                         bool onlyIfEmpty,
                         bool removeRoot) {
    std::error_code existsEc;
    if (!std::filesystem::exists(path, existsEc)) {
        MarkSkipped(state);
        return;
    }
    if (std::filesystem::is_directory(path, existsEc)) {
        if (recursive) {
            DeleteDirectoryContentsSegmented(state, path, removeRoot, onlyIfEmpty);
        } else if (!onlyIfEmpty || std::filesystem::is_empty(path, existsEc)) {
            DeleteSinglePath(state, path, "delete_dir");
        } else {
            MarkSkipped(state);
        }
    } else {
        DeleteSinglePath(state, path, "delete_file");
    }
}

bool RemoveDirectoryContentsBestEffort(const std::filesystem::path& root,
                                       CliSupport& console,
                                       const UpgradeCleanupProgressCallback& progressCallback,
                                       const std::function<bool()>& cancellationCallback) {
    if (IsProtectedFullCleanupRoot(root)) {
        console.showWarning("Upgrade cleanup skipped unsafe previous install root: " + Utf8FromPath(root));
        return false;
    }

    std::vector<std::filesystem::path> children;
    std::error_code iterEc;
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::directory_iterator it(toLongPath(root), options, iterEc), end;
         !iterEc && it != end;
         it.increment(iterEc)) {
        children.push_back(it->path());
    }
    if (iterEc) {
        console.showWarning("Upgrade cleanup failed to enumerate previous install root: " + Utf8FromPath(root));
    }

    UpgradeCleanupTask task;
    task.mode = UpgradeCleanupTaskMode::PreviousInstall;
    task.previousInstallDir = Utf8FromPath(root);
    CleanupExecutionState state;
    state.task = std::move(task);
    state.progressCallback = progressCallback;

    const size_t total = children.size();
    for (size_t i = 0; i < total; ++i) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade cleanup cancelled while deleting previous install contents.");
            return false;
        }

        const auto& child = children[i];
        DeletePathSegmented(state, child, true, false, true);

        const float progress = total == 0
                                   ? 0.8f
                                   : 0.8f * static_cast<float>(i + 1) / static_cast<float>(total);
        EmitProgress(progressCallback, progress, "Removing old path: " + Utf8FromPath(child));
    }

    return state.result.failedCount == 0;
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

    UpgradeCleanupTask task;
    task.mode = UpgradeCleanupTaskMode::ExtraPaths;
    CleanupExecutionState state;
    state.task = std::move(task);
    DeletePathSegmented(state, cleanupPath, rule.recursive, rule.onlyIfEmpty, true);
    if (state.result.failedCount > 0 && std::filesystem::exists(cleanupPath)) {
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
    const bool sameInstallRoot =
        !normalizedOld.empty() && !normalizedNew.empty() && normalizedOld == normalizedNew;

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
        console.showWarning(
            "Upgrade cleanup: failed to read previous manifest, fallback to directory contents cleanup.");
        const bool cleaned = RemoveDirectoryContentsBestEffort(previousRoot,
                                                              console,
                                                              progressCallback,
                                                              cancellationCallback);
        EmitProgress(progressCallback, 0.95f, "Removing previous install contents");

        if (!sameInstallRoot) {
            std::error_code rootEc;
            if (std::filesystem::is_empty(previousRoot, rootEc)) {
                std::filesystem::remove(toLongPath(previousRoot), rootEc);
                if (!rootEc) {
                    console.showInfo("Removed previous install root: " + Utf8FromPath(previousRoot));
                }
            }
        }

        EmitProgress(progressCallback, 1.0f, "Upgrade cleanup completed");
        return cleaned;
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

    UpgradeCleanupTask task;
    task.mode = UpgradeCleanupTaskMode::PreviousInstall;
    task.previousInstallDir = previousInstallDir;
    task.newInstallDir = newInstallDir;
    task.manifestPath = manifestPath;
    CleanupExecutionState state;
    state.task = std::move(task);
    state.progressCallback = progressCallback;

    const size_t totalFiles = filesToDelete.size();
    for (size_t i = 0; i < totalFiles; ++i) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade cleanup cancelled while deleting files.");
            return false;
        }
        const std::filesystem::path& filePath = filesToDelete[i];
        DeleteSinglePath(state, filePath, "delete_manifest_file");

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
            DeleteSinglePath(state, dir, "delete_empty_dir");
        }
    }
    EmitProgress(progressCallback, 0.95f, "Removing empty directories from previous install root");

    std::error_code rootEc;
    if (std::filesystem::is_empty(previousRoot, rootEc)) {
        if (sameInstallRoot) {
            console.showInfo("Previous install root is empty and will be reused: " +
                             Utf8FromPath(previousRoot));
        } else {
            DeleteSinglePath(state, previousRoot, "delete_previous_root");
            if (!std::filesystem::exists(previousRoot, rootEc)) {
                console.showInfo("Removed previous install root: " + Utf8FromPath(previousRoot));
            }
        }
    } else {
        console.showInfo("Previous install root is not empty after upgrade cleanup: " +
                         Utf8FromPath(previousRoot));
    }

    EmitProgress(progressCallback, 1.0f, "Upgrade cleanup completed");
    return true;
}

json ResultToJson(const UpgradeCleanupResult& result) {
    return {
        {"success", result.success},
        {"partial", result.partial},
        {"timedOut", result.timedOut},
        {"deletedCount", result.deletedCount},
        {"failedCount", result.failedCount},
        {"skippedCount", result.skippedCount},
        {"timedOutPath", result.timedOutPath},
        {"message", result.message},
    };
}

UpgradeCleanupResult ResultFromJson(const json& value) {
    UpgradeCleanupResult result;
    if (!value.is_object()) {
        return result;
    }
    result.success = value.value("success", result.success);
    result.partial = value.value("partial", result.partial);
    result.timedOut = value.value("timedOut", result.timedOut);
    result.deletedCount = value.value("deletedCount", result.deletedCount);
    result.failedCount = value.value("failedCount", result.failedCount);
    result.skippedCount = value.value("skippedCount", result.skippedCount);
    result.timedOutPath = value.value("timedOutPath", "");
    result.message = value.value("message", "");
    return result;
}

UpgradeCleanupResult ExecutePreviousInstallTask(const UpgradeCleanupTask& task) {
    CleanupExecutionState state;
    state.task = task;
    EmitCleanupHeartbeat(state, true);

    const std::filesystem::path previousRoot = PathFromUtf8(task.previousInstallDir).lexically_normal();
    if (task.previousInstallDir.empty() || previousRoot.empty()) {
        state.result.success = false;
        state.result.message = "Previous install directory is empty";
        return state.result;
    }
    if (IsProtectedFullCleanupRoot(previousRoot)) {
        state.result.success = false;
        state.result.message = "Unsafe previous install root";
        return state.result;
    }

    const std::string normalizedOld = normalizePathForCompare(task.previousInstallDir);
    const std::string normalizedNew = normalizePathForCompare(task.newInstallDir);
    const bool sameInstallRoot =
        !normalizedOld.empty() && !normalizedNew.empty() && normalizedOld == normalizedNew;

    std::error_code existsEc;
    if (!std::filesystem::exists(previousRoot, existsEc)) {
        state.result.message = "Previous install directory not found";
        return state.result;
    }
    if (!std::filesystem::is_directory(previousRoot, existsEc)) {
        state.result.success = false;
        state.result.message = "Previous install path is not a directory";
        return state.result;
    }

    json manifest;
    if (!ReadManifestJson(task.manifestPath, manifest)) {
        logInstallerWarning("[InstallFlow][Cleanup] previous manifest unavailable, using safe fallback directory cleanup");
        std::vector<std::filesystem::path> children;
        std::error_code iterEc;
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::directory_iterator it(toLongPath(previousRoot), options, iterEc), end;
             !iterEc && it != end;
             it.increment(iterEc)) {
            children.push_back(it->path());
        }
        for (size_t i = 0; i < children.size(); ++i) {
            DeletePathSegmented(state, children[i], true, false, true);
            const float progress = children.empty()
                ? 0.8f
                : 0.8f * static_cast<float>(i + 1) / static_cast<float>(children.size());
            EmitCleanupProgress(state, progress, "Removing old path");
        }
        if (!sameInstallRoot) {
            std::error_code emptyEc;
            if (std::filesystem::is_empty(previousRoot, emptyEc)) {
                DeleteSinglePath(state, previousRoot, "delete_previous_root");
            }
        }
        state.result.partial = state.result.failedCount > 0 || state.result.skippedCount > 0;
        state.result.success = state.task.policy.allowPartialSuccess || state.result.failedCount == 0;
        EmitCleanupHeartbeat(state, true);
        return state.result;
    }

    std::vector<std::filesystem::path> filesToDelete;
    filesToDelete.reserve(CollectManifestFiles(manifest).size() + 2);
    std::set<std::string> seen;
    auto addFile = [&](std::filesystem::path path) {
        if (path.empty()) {
            return;
        }
        if (!path.is_absolute()) {
            path = previousRoot / path;
        }
        path = path.lexically_normal();
        if (!IsPathUnderOrEqual(path, previousRoot)) {
            ++state.result.skippedCount;
            return;
        }
        const std::string key = NormalizePath(path);
        if (!key.empty() && seen.insert(key).second) {
            filesToDelete.push_back(std::move(path));
        }
    };
    for (const auto& file : CollectManifestFiles(manifest)) {
        addFile(PathFromUtf8(file));
    }
    addFile(previousRoot / "uninstall.exe");
    addFile(previousRoot / "install.manifest.json");

    for (size_t i = 0; i < filesToDelete.size(); ++i) {
        DeleteSinglePath(state, filesToDelete[i], "delete_manifest_file");
        const float progress = filesToDelete.empty()
            ? 0.8f
            : 0.8f * static_cast<float>(i + 1) / static_cast<float>(filesToDelete.size());
        EmitCleanupProgress(state, progress, "Removing old file");
    }

    std::vector<std::filesystem::path> directories;
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    std::error_code iterEc;
    for (std::filesystem::recursive_directory_iterator it(toLongPath(previousRoot), options, iterEc), end;
         !iterEc && it != end;
         it.increment(iterEc)) {
        const auto path = it->path();
        if (IsReparsePointPath(path)) {
            it.disable_recursion_pending();
            ++state.result.skippedCount;
            continue;
        }
        std::error_code typeEc;
        if (it->is_directory(typeEc)) {
            directories.push_back(path);
        }
    }
    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
        return a.native().size() > b.native().size();
    });
    for (const auto& dir : directories) {
        std::error_code emptyEc;
        if (std::filesystem::is_empty(dir, emptyEc)) {
            DeleteSinglePath(state, dir, "delete_empty_dir");
        }
    }
    std::error_code rootEc;
    if (!sameInstallRoot && std::filesystem::is_empty(previousRoot, rootEc)) {
        DeleteSinglePath(state, previousRoot, "delete_previous_root");
    }

    state.result.partial = state.result.failedCount > 0 || state.result.skippedCount > 0;
    state.result.success = state.task.policy.allowPartialSuccess || state.result.failedCount == 0;
    EmitCleanupHeartbeat(state, true);
    return state.result;
}

UpgradeCleanupResult ExecuteExtraPathsTask(const UpgradeCleanupTask& task) {
    CleanupExecutionState state;
    state.task = task;
    EmitCleanupHeartbeat(state, true);
    for (size_t i = 0; i < task.extraPaths.size(); ++i) {
        const auto& rule = task.extraPaths[i];
        if (rule.path.empty()) {
            MarkSkipped(state);
            continue;
        }
        const std::filesystem::path cleanupPath = PathFromUtf8(rule.path).lexically_normal();
        if (!IsSafeCleanupPath(cleanupPath)) {
            ++state.result.skippedCount;
            logInstallerWarning("[InstallFlow][Cleanup] skipped unsafe extra path=" + Utf8FromPath(cleanupPath));
            continue;
        }
        std::error_code existsEc;
        if (!std::filesystem::exists(cleanupPath, existsEc)) {
            MarkSkipped(state);
            continue;
        }
        DeletePathSegmented(state, cleanupPath, rule.recursive, rule.onlyIfEmpty, true);
        const float progress = task.extraPaths.empty()
            ? 1.0f
            : static_cast<float>(i + 1) / static_cast<float>(task.extraPaths.size());
        EmitCleanupProgress(state, progress, "Removing upgrade cleanup path");
    }
    state.result.partial = state.result.failedCount > 0 || state.result.skippedCount > 0;
    state.result.success = state.task.policy.allowPartialSuccess || state.result.failedCount == 0;
    EmitCleanupHeartbeat(state, true);
    return state.result;
}

UpgradeCleanupResult ExecuteCleanupTask(const UpgradeCleanupTask& task) {
    if (task.mode == UpgradeCleanupTaskMode::ExtraPaths) {
        return ExecuteExtraPathsTask(task);
    }
    return ExecutePreviousInstallTask(task);
}

std::wstring QuoteArg(const std::wstring& value) {
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

UpgradeCleanupResult RunTaskWithWatchdog(UpgradeCleanupTask task,
                                         const UpgradeCleanupProgressCallback& progressCallback,
                                         const std::function<bool()>& cancellationCallback) {
    UpgradeCleanupResult fallback;
#ifdef _WIN32
    const std::filesystem::path taskPath = BuildCleanupTempPath(".json");
    const std::filesystem::path heartbeatPath = BuildCleanupTempPath(".heartbeat.json");
    const std::filesystem::path resultPath = BuildCleanupTempPath(".result.json");
    task.heartbeatPath = Utf8FromPath(heartbeatPath);
    task.resultPath = Utf8FromPath(resultPath);
    if (!WriteJsonFileBestEffort(taskPath, TaskToJson(task))) {
        fallback.success = false;
        fallback.message = "Failed to write cleanup task";
        return fallback;
    }

    const std::wstring exePath = Utf8ToWide(getCurrentExecutablePath());
    const std::wstring taskPathW = taskPath.wstring();
    std::wstring cmd = QuoteArg(exePath) + L" --upgrade-cleanup-worker " + QuoteArg(taskPathW);
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    BOOL started = CreateProcessW(exePath.c_str(),
                                  cmdLine.data(),
                                  nullptr,
                                  nullptr,
                                  FALSE,
                                  CREATE_NO_WINDOW,
                                  nullptr,
                                  nullptr,
                                  &si,
                                  &pi);
    if (!started) {
        fallback.success = false;
        fallback.message = "Failed to start cleanup worker";
        std::error_code ec;
        std::filesystem::remove(taskPath, ec);
        return fallback;
    }

    const uint64_t startedMs = NowMs();
    uint64_t lastHeartbeatTimestamp = startedMs;
    std::string lastPath;
    bool killed = false;
    while (true) {
        if (IsCancelled(cancellationCallback)) {
            TerminateProcess(pi.hProcess, 2);
            killed = true;
            fallback.success = false;
            fallback.message = "Cleanup cancelled";
            break;
        }
        DWORD wait = WaitForSingleObject(pi.hProcess, 250);
        if (wait == WAIT_OBJECT_0) {
            break;
        }

        json heartbeat;
        if (ReadJsonFileBestEffort(heartbeatPath, heartbeat)) {
            lastHeartbeatTimestamp = heartbeat.value("timestampMs", lastHeartbeatTimestamp);
            lastPath = heartbeat.value("currentPath", lastPath);
            if (progressCallback) {
                UpgradeCleanupProgressInfo info;
                info.currentItem = lastPath.empty() ? "Cleaning previous installation" : lastPath;
                info.progress = 0.5f;
                progressCallback(info);
            }
        }

        const uint64_t now = NowMs();
        const bool totalTimedOut = now - startedMs >= task.policy.totalTimeoutMs;
        const bool heartbeatStale = now > lastHeartbeatTimestamp &&
            now - lastHeartbeatTimestamp >= task.policy.itemStaleTimeoutMs;
        if (totalTimedOut || heartbeatStale) {
            fallback.success = task.policy.allowPartialSuccess;
            fallback.partial = true;
            fallback.timedOut = true;
            fallback.timedOutPath = lastPath;
            fallback.message = totalTimedOut ? "Cleanup worker total timeout"
                                             : "Cleanup worker heartbeat stale";
            logInstallerWarning("[InstallFlow][Cleanup] worker timeout message=" + fallback.message +
                                " currentPath=" + lastPath);
            TerminateProcess(pi.hProcess, 3);
            killed = true;
            break;
        }
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    UpgradeCleanupResult result = fallback;
    if (!killed) {
        json resultJson;
        if (ReadJsonFileBestEffort(resultPath, resultJson)) {
            result = ResultFromJson(resultJson);
        } else {
            result.success = exitCode == 0;
            result.partial = exitCode != 0;
            result.message = "Cleanup worker finished without result file";
        }
    }

    std::error_code ec;
    std::filesystem::remove(taskPath, ec);
    std::filesystem::remove(heartbeatPath, ec);
    std::filesystem::remove(resultPath, ec);
    return result;
#else
    (void)progressCallback;
    (void)cancellationCallback;
    return ExecuteCleanupTask(task);
#endif
}

int runUpgradeCleanupWorkerFromTask(const std::string& taskPath) {
    initializeInstallerLogging();
    json taskJson;
    UpgradeCleanupTask task;
    if (!ReadJsonFileBestEffort(PathFromUtf8(taskPath), taskJson) || !TaskFromJson(taskJson, task)) {
        logInstallerError("[InstallFlow][Cleanup] worker failed to read task");
        return 2;
    }
#ifdef _WIN32
    char delayBuffer[32] = {};
    DWORD delayLen = GetEnvironmentVariableA("MTINSTALLER_TEST_CLEANUP_WORKER_DELAY_MS",
                                             delayBuffer,
                                             static_cast<DWORD>(sizeof(delayBuffer)));
    if (delayLen > 0 && delayLen < sizeof(delayBuffer)) {
        const DWORD delayMs = static_cast<DWORD>(std::strtoul(delayBuffer, nullptr, 10));
        if (delayMs > 0) {
            Sleep(delayMs);
        }
    }
#endif
    UpgradeCleanupResult result = ExecuteCleanupTask(task);
    if (!task.resultPath.empty()) {
        WriteJsonFileBestEffort(PathFromUtf8(task.resultPath), ResultToJson(result));
    }
    logInstallerInfo("[InstallFlow][Cleanup] worker end deleted=" + std::to_string(result.deletedCount) +
                     " failed=" + std::to_string(result.failedCount) +
                     " skipped=" + std::to_string(result.skippedCount) +
                     " partial=" + std::string(result.partial ? "true" : "false"));
    flushInstallerLogging();
    return result.success ? 0 : 1;
}

UpgradeCleanupResult runPreviousInstallCleanupWithWatchdog(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const std::string& newInstallDir,
    const UpgradeCleanupProgressCallback& progressCallback,
    const std::function<bool()>& cancellationCallback,
    const UpgradeCleanupPolicy& policy) {
    UpgradeCleanupTask task;
    task.mode = UpgradeCleanupTaskMode::PreviousInstall;
    task.previousInstallDir = previousInstallDir;
    task.newInstallDir = newInstallDir;
    task.manifestPath = manifestPath;
    task.policy = policy;
    return RunTaskWithWatchdog(std::move(task), progressCallback, cancellationCallback);
}

UpgradeCleanupResult runUpgradeExtraPathCleanupWithWatchdog(
    const std::vector<UninstallCleanupRule>& rules,
    const std::string& previousInstallDir,
    InstallerPathResolver& resolver,
    const UpgradeCleanupProgressCallback& progressCallback,
    const std::function<bool()>& cancellationCallback,
    const UpgradeCleanupPolicy& policy) {
    UpgradeCleanupTask task;
    task.mode = UpgradeCleanupTaskMode::ExtraPaths;
    task.previousInstallDir = previousInstallDir;
    task.policy = policy;
    task.extraPaths.reserve(rules.size());
    for (const auto& rule : rules) {
        UninstallCleanupRule expanded = rule;
        expanded.path = resolver.expandEnvironmentVariables(
            ExpandInstallDirTokenLocal(rule.path, previousInstallDir));
        task.extraPaths.push_back(std::move(expanded));
    }
    return RunTaskWithWatchdog(std::move(task), progressCallback, cancellationCallback);
}

bool cleanupUpgradeSystemArtifacts(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const ExtendedInstallationMetadata& metadata,
    InstallerPathResolver& resolver,
    CliSupport& console,
    const UpgradeCleanupProgressCallback& progressCallback,
    const std::function<bool()>& cancellationCallback,
    bool cleanupExtraPaths) {
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

    std::vector<std::string> installAutoStartupNames;
    std::set<std::string> seenNames;
    for (const auto& startup : metadata.lifecycleUpgradeCleanup.startup) {
        AppendUniqueName(installAutoStartupNames, seenNames, startup.name);
    }

    const size_t totalSteps = installAutoStartupNames.size() +
                              metadata.lifecycleUpgradeCleanup.shortcuts.size() +
                              metadata.lifecycleUpgradeCleanup.uninstallEntries.size() +
                              metadata.lifecycleUpgradeCleanup.registry.legacyKeys.size() +
                              (cleanupExtraPaths ? metadata.lifecycleUpgradeCleanup.extraPaths.size() : 0);
    size_t completedSteps = 0;
    auto emitStep = [&](const std::string& item) {
        ++completedSteps;
        float progress = totalSteps == 0
                             ? 1.0f
                             : static_cast<float>(completedSteps) / static_cast<float>(totalSteps);
        EmitProgress(progressCallback, progress, item);
    };

    for (const auto& name : installAutoStartupNames) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing auto startup.");
            return false;
        }
        if (removeAutoStartup(name)) {
            console.showInfo("Upgrade cleanup removed auto startup: " + name);
        }
        emitStep("Removing legacy auto startup: " + name);
    }

    for (const auto& shortcut : metadata.lifecycleUpgradeCleanup.shortcuts) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing shortcuts.");
            return false;
        }
        if (!shortcut.name.empty()) {
            deleteDesktopShortcut(shortcut.name);
            deleteStartMenuShortcut(shortcut.name);
        }
        emitStep("Removing legacy shortcut");
    }

#ifdef _WIN32
    for (const auto& entry : metadata.lifecycleUpgradeCleanup.uninstallEntries) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing uninstall entries.");
            return false;
        }
        switch (entry.scope) {
        case UninstallEntryScope::CURRENT_USER:
            deleteUninstallRegistryEntry(entry.name, false);
            break;
        case UninstallEntryScope::LOCAL_MACHINE:
        case UninstallEntryScope::WOW6432:
            deleteUninstallRegistryEntry(entry.name, true);
            break;
        case UninstallEntryScope::ANY:
        default:
            deleteUninstallRegistryEntry(entry.name, false);
            deleteUninstallRegistryEntry(entry.name, true);
            break;
        }
        emitStep("Removing uninstall entry");
    }
#endif

    for (const auto& entry : metadata.lifecycleUpgradeCleanup.registry.legacyKeys) {
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

    if (cleanupExtraPaths) {
        for (const auto& rule : metadata.lifecycleUpgradeCleanup.extraPaths) {
            if (IsCancelled(cancellationCallback)) {
                console.showWarning("Upgrade system cleanup cancelled while removing extra paths.");
                return false;
            }
            RemoveUpgradeCleanupPath(rule, previousInstallDir, resolver, console);
            emitStep("Removing upgrade cleanup path: " + rule.path);
        }
    }

    if (totalSteps == 0) {
        EmitProgress(progressCallback, 1.0f, "Upgrade system cleanup completed");
    } else {
        EmitProgress(progressCallback, 1.0f, "Upgrade system cleanup completed");
    }
    return true;
}

} // namespace MultiThreadedInstaller
