#include "installer/upgrade_cleanup.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/cleanup_delete_executor.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <json.hpp>
#include <map>
#include <mutex>
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
    // Normalized absolute paths kept in place during difference-set cleanup
    // (files that also exist in the new package).
    std::set<std::string> keepFiles;
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

bool IsMtiPendingDirectoryName(const std::filesystem::path& path) {
    const std::string name = Utf8FromPath(path.filename());
    return name.rfind(".mti_delete_pending_", 0) == 0;
}

std::filesystem::path DirectChildUnderRoot(const std::filesystem::path& path,
                                           const std::filesystem::path& root) {
    const std::filesystem::path normalizedPath = path.lexically_normal();
    const std::filesystem::path normalizedRoot = root.lexically_normal();
    if (SameNormalizedPath(normalizedPath, normalizedRoot) ||
        !IsPathUnderOrEqual(normalizedPath, normalizedRoot)) {
        return {};
    }
    const std::filesystem::path relative = normalizedPath.lexically_relative(normalizedRoot);
    auto it = relative.begin();
    if (it == relative.end()) {
        return {};
    }
    return (normalizedRoot / *it).lexically_normal();
}

std::filesystem::path MakePendingSiblingPath(const std::filesystem::path& child) {
    std::ostringstream name;
    name << ".mti_delete_pending_" << Utf8FromPath(child.filename()) << "_";
#ifdef _WIN32
    name << GetCurrentProcessId();
#else
    name << std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
    const auto nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    name << "_" << nowMs;
    return child.parent_path() / PathFromUtf8(name.str());
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
    std::unique_ptr<CleanupDeleteExecutor> deleteExecutor;
    std::vector<std::filesystem::path> pendingEmptyDirs;
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
        {"processedCount", state.processedCount},
        {"workerConcurrency", state.workerConcurrency},
        {"activeWorkers", state.activeWorkers},
        {"slowestCurrentItem", state.slowestCurrentItem},
        {"slowestCurrentThreadId", state.slowestCurrentThreadId},
        {"slowestCurrentStartedMs", state.slowestCurrentStartedMs},
        {"lastCompletedAt", state.lastCompletedAtMs},
    };
    WriteJsonFileBestEffort(PathFromUtf8(state.task.heartbeatPath), heartbeat);
    state.lastHeartbeatMs = now;
    state.processedSinceHeartbeat = 0;
}

void EmitCleanupProgress(CleanupExecutionState& state, float progress, const std::string& item) {
    EmitProgress(state.progressCallback, progress, item);
}

void SetCleanupCurrentItem(CleanupExecutionState& state,
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
    EmitCleanupHeartbeat(state, false);
}

void MarkCleanupCurrentItemCompleted(CleanupExecutionState& state,
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
    state.lastCompletedAtMs = NowMs();

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

    const uint64_t started = NowMs();
    SetCleanupCurrentItem(state, path, action, started);

    std::error_code ec;
    const bool removed = std::filesystem::remove(toLongPath(path), ec);
    const uint64_t elapsed = NowMs() - started;
    if (ec) {
        ++state.result.failedCount;
        logInstallerWarning("[InstallFlow][Cleanup] delete failed path=" + Utf8FromPath(path) +
                            " error=" + ec.message());
    } else if (!removed) {
        // remove() returns false when the path is already gone or when an
        // empty-directory cleanup target is still non-empty. Both are normal
        // best-effort cleanup outcomes and should not turn the whole upgrade
        // cleanup into a partial warning.
    } else {
        ++state.result.deletedCount;
        if (elapsed >= state.task.policy.slowItemLogMs) {
            logInstallerWarning("[InstallFlow][Cleanup] slow delete path=" + Utf8FromPath(path) +
                                " elapsedMs=" + std::to_string(elapsed));
        }
    }
    MarkCleanupCurrentItemCompleted(state, path);
    ++state.processedSinceHeartbeat;
    EmitCleanupHeartbeat(state, false);
}

void RecordParallelDeleteResult(CleanupExecutionState& state,
                                const std::filesystem::path& path,
                                const std::error_code& ec,
                                bool removed,
                                uint64_t elapsed) {
    if (ec) {
        ++state.result.failedCount;
        logInstallerWarning("[InstallFlow][Cleanup] delete failed path=" +
                            Utf8FromPath(path) + " error=" + ec.message());
    } else if (!removed) {
        // Parallel delete tasks are file paths from the old manifest. A missing
        // file usually means it was already removed by a prior directory
        // isolation step, so treat it as an idempotent cleanup success instead
        // of a partial cleanup warning.
    } else {
        ++state.result.deletedCount;
        if (elapsed >= state.task.policy.slowItemLogMs) {
            logInstallerWarning("[InstallFlow][Cleanup] slow delete path=" +
                                Utf8FromPath(path) + " elapsedMs=" +
                                std::to_string(elapsed));
        }
    }
    MarkCleanupCurrentItemCompleted(state, path);
    ++state.processedCount;
    ++state.processedSinceHeartbeat;
    EmitCleanupHeartbeat(state, false);
}

void DeleteDirectoryIfEmptyByRemove(CleanupExecutionState& state,
                                    const std::filesystem::path& path,
                                    const std::string& action);

CleanupDeleteCallbacks BuildCleanupDeleteCallbacks(CleanupExecutionState& state) {
    CleanupDeleteCallbacks callbacks;
    callbacks.onItemStarted = [&state](const std::filesystem::path& path,
                                       const std::string& action,
                                       uint64_t started,
                                       const std::string& threadId) {
        std::lock_guard<std::mutex> lock(state.mutex);
        SetCleanupCurrentItem(state, path, action, started, threadId);
    };
    callbacks.onItemFinished = [&state](const std::filesystem::path& path,
                                        const std::error_code& ec,
                                        bool removed,
                                        uint64_t elapsed,
                                        const std::string& threadId) {
        (void)threadId;
        std::lock_guard<std::mutex> lock(state.mutex);
        RecordParallelDeleteResult(state, path, ec, removed, elapsed);
    };
    callbacks.onReparsePointSkipped = [&state](const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(state.mutex);
        ++state.result.skippedCount;
        ++state.processedSinceHeartbeat;
        logInstallerWarning("[InstallFlow][Cleanup] skipped reparse point path=" +
                            Utf8FromPath(path));
        EmitCleanupHeartbeat(state, false);
    };
    callbacks.onEmptyPathSkipped = [&state]() {
        std::lock_guard<std::mutex> lock(state.mutex);
        MarkSkipped(state);
    };
    return callbacks;
}

CleanupDeleteExecutor& EnsureCleanupDeleteExecutor(CleanupExecutionState& state) {
    if (!state.deleteExecutor) {
        state.deleteExecutor = std::make_unique<CleanupDeleteExecutor>(
            CleanupDeleteWorkload::Upgrade,
            BuildCleanupDeleteCallbacks(state));
        state.workerConcurrency = state.deleteExecutor->workerConcurrency();
        logInstallerInfo("[InstallFlow][Cleanup] delete executor start workload=upgrade cleanupWorkers=" +
                         std::to_string(state.workerConcurrency));
    }
    return *state.deleteExecutor;
}

void DeleteFilesParallel(CleanupExecutionState& state,
                         const std::vector<std::filesystem::path>& files) {
    if (files.empty()) {
        return;
    }
    CleanupDeleteExecutor& executor = EnsureCleanupDeleteExecutor(state);
    std::vector<std::filesystem::path> batch;
    batch.reserve(512);
    for (const auto& file : files) {
        batch.push_back(file);
        if (batch.size() >= 512) {
            executor.submit(batch);
        }
    }
    executor.submit(batch);
}

void FinishParallelDeletes(CleanupExecutionState& state) {
    if (!state.deleteExecutor) {
        return;
    }
    const uint64_t startedDeleted = state.result.deletedCount;
    const uint64_t startedFailed = state.result.failedCount;
    const uint64_t startedSkipped = state.result.skippedCount;
    state.deleteExecutor->finish();
    state.deleteExecutor.reset();
    logInstallerInfo("[InstallFlow][Cleanup] delete executor end deletedDelta=" +
                     std::to_string(state.result.deletedCount - startedDeleted) +
                     " failedDelta=" + std::to_string(state.result.failedCount - startedFailed) +
                     " skippedDelta=" + std::to_string(state.result.skippedCount - startedSkipped));
}

void QueueEmptyDirectoryRemoval(CleanupExecutionState& state,
                                const std::filesystem::path& path) {
    if (!path.empty()) {
        state.pendingEmptyDirs.push_back(path);
    }
}

void DeleteQueuedEmptyDirectories(CleanupExecutionState& state) {
    if (state.pendingEmptyDirs.empty()) {
        return;
    }
    std::sort(state.pendingEmptyDirs.begin(), state.pendingEmptyDirs.end(), [](const auto& a, const auto& b) {
        return a.native().size() > b.native().size();
    });
    std::set<std::string> seen;
    for (const auto& dir : state.pendingEmptyDirs) {
        const std::string key = NormalizePath(dir);
        if (key.empty() || !seen.insert(key).second) {
            continue;
        }
        DeleteDirectoryIfEmptyByRemove(state, dir, "delete_empty_dir");
    }
    state.pendingEmptyDirs.clear();
}

void DeleteDirectoryIfEmptyByRemove(CleanupExecutionState& state,
                                    const std::filesystem::path& path,
                                    const std::string& action) {
    if (path.empty()) {
        MarkSkipped(state);
        return;
    }
    if (IsReparsePointPath(path)) {
        ++state.result.skippedCount;
        ++state.processedSinceHeartbeat;
        logInstallerWarning("[InstallFlow][Cleanup] skipped reparse point directory=" + Utf8FromPath(path));
        EmitCleanupHeartbeat(state, false);
        return;
    }

    const uint64_t started = NowMs();
    SetCleanupCurrentItem(state, path, action, started);
    std::error_code ec;
    const bool removed = std::filesystem::remove(toLongPath(path), ec);
    const uint64_t elapsed = NowMs() - started;
    if (!ec && removed) {
        ++state.result.deletedCount;
        if (elapsed >= state.task.policy.slowItemLogMs) {
            logInstallerWarning("[InstallFlow][Cleanup] slow delete path=" + Utf8FromPath(path) +
                                " elapsedMs=" + std::to_string(elapsed));
        }
    } else if (!ec ||
               ec == std::make_error_code(std::errc::directory_not_empty) ||
               ec == std::make_error_code(std::errc::file_exists)) {
        // Non-empty directories are expected while walking parent chains.
    } else {
        ++state.result.failedCount;
        logInstallerWarning("[InstallFlow][Cleanup] delete failed path=" + Utf8FromPath(path) +
                            " error=" + ec.message());
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
    std::vector<std::filesystem::path> batch;
    batch.reserve(512);
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    std::error_code iterEc;
    for (std::filesystem::recursive_directory_iterator it(toLongPath(root), options, iterEc), end;
         !iterEc && it != end;
         it.increment(iterEc)) {
        const std::filesystem::path entryPath = it->path();
        if (IsReparsePointPath(entryPath)) {
            it.disable_recursion_pending();
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                ++state.result.skippedCount;
                ++state.processedSinceHeartbeat;
                EmitCleanupHeartbeat(state, false);
            }
            continue;
        }
        std::error_code typeEc;
        if (it->is_directory(typeEc)) {
            directories.push_back(entryPath);
        } else {
            batch.push_back(entryPath);
            if (batch.size() >= 512) {
                EnsureCleanupDeleteExecutor(state).submit(batch);
            }
        }
    }
    if (!batch.empty()) {
        EnsureCleanupDeleteExecutor(state).submit(batch);
    }
    if (iterEc) {
        ++state.result.failedCount;
        logInstallerWarning("[InstallFlow][Cleanup] directory scan failed path=" + Utf8FromPath(root) +
                            " error=" + iterEc.message());
    }

    for (const auto& dir : directories) {
        QueueEmptyDirectoryRemoval(state, dir);
    }
    if (removeRoot) {
        QueueEmptyDirectoryRemoval(state, root);
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

void AddIsolationCandidate(std::vector<std::filesystem::path>& candidates,
                           std::set<std::string>& seen,
                           const std::filesystem::path& candidate,
                           const std::filesystem::path& previousRoot) {
    const std::filesystem::path child = DirectChildUnderRoot(candidate, previousRoot);
    if (child.empty() || SameNormalizedPath(child, previousRoot) || IsMtiPendingDirectoryName(child)) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(child, ec) || !std::filesystem::is_directory(child, ec) ||
        IsReparsePointPath(child)) {
        return;
    }
    const std::string key = NormalizePath(child);
    if (!key.empty() && seen.insert(key).second) {
        candidates.push_back(child);
    }
}

std::vector<std::filesystem::path> BuildSubdirectoryIsolationCandidates(
    const json* manifest,
    const std::filesystem::path& previousRoot,
    const std::vector<std::string>& replacementTargets) {
    std::vector<std::filesystem::path> candidates;
    std::set<std::string> seen;

    bool hasRootReplacement = false;
    for (const auto& rawTarget : replacementTargets) {
        if (rawTarget.empty()) {
            continue;
        }
        const std::filesystem::path target = PathFromUtf8(rawTarget).lexically_normal();
        if (SameNormalizedPath(target, previousRoot)) {
            hasRootReplacement = true;
            continue;
        }
        AddIsolationCandidate(candidates, seen, target, previousRoot);
    }

    if (manifest && hasRootReplacement) {
        for (const auto& file : CollectManifestFiles(*manifest)) {
            std::filesystem::path filePath = PathFromUtf8(file);
            if (!filePath.is_absolute()) {
                filePath = previousRoot / filePath;
            }
            AddIsolationCandidate(candidates, seen, filePath, previousRoot);
        }
    }

    if (!manifest && !replacementTargets.empty() && candidates.empty()) {
        std::error_code ec;
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::directory_iterator it(toLongPath(previousRoot), options, ec), end;
             !ec && it != end;
             it.increment(ec)) {
            AddIsolationCandidate(candidates, seen, it->path(), previousRoot);
        }
    }

    return candidates;
}

std::vector<std::filesystem::path> IsolateCleanupSubdirectories(
    const std::vector<std::filesystem::path>& candidates) {
    std::vector<std::filesystem::path> pendingDirs;
    for (const auto& child : candidates) {
        std::filesystem::path pending = MakePendingSiblingPath(child);
        for (int attempt = 0; attempt < 10; ++attempt) {
            std::error_code existsEc;
            if (!std::filesystem::exists(pending, existsEc)) {
                break;
            }
            pending = child.parent_path() / PathFromUtf8(
                ".mti_delete_pending_" + Utf8FromPath(child.filename()) + "_" +
                std::to_string(NowMs()) + "_" + std::to_string(attempt));
        }

        std::error_code ec;
        std::filesystem::rename(toLongPath(child), toLongPath(pending), ec);
        if (ec) {
            logInstallerWarning("[InstallFlow][Cleanup] isolate rename failed source=" +
                                Utf8FromPath(child) + " error=" + ec.message());
            continue;
        }
        logInstallerInfo("[InstallFlow][Cleanup] isolated old subdirectory source=" +
                         Utf8FromPath(child) + " pending=" + Utf8FromPath(pending));
        pendingDirs.push_back(std::move(pending));
    }
    return pendingDirs;
}

std::vector<std::filesystem::path> CollectAffectedParentDirs(
    const std::vector<std::filesystem::path>& files,
    const std::filesystem::path& root) {
    std::vector<std::filesystem::path> dirs;
    std::set<std::string> seen;
    const std::filesystem::path normalizedRoot = root.lexically_normal();
    for (const auto& file : files) {
        std::filesystem::path dir = file.lexically_normal().parent_path();
        while (!dir.empty() && IsPathUnderOrEqual(dir, normalizedRoot)) {
            const std::string key = NormalizePath(dir);
            if (!key.empty() && seen.insert(key).second) {
                dirs.push_back(dir);
            }
            if (SameNormalizedPath(dir, normalizedRoot)) {
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

void DeleteAffectedEmptyDirs(CleanupExecutionState& state,
                             const std::vector<std::filesystem::path>& files,
                             const std::filesystem::path& root,
                             bool removeRoot) {
    const std::filesystem::path normalizedRoot = root.lexically_normal();
    for (const auto& dir : CollectAffectedParentDirs(files, normalizedRoot)) {
        if (!removeRoot && SameNormalizedPath(dir, normalizedRoot)) {
            continue;
        }
        DeleteDirectoryIfEmptyByRemove(state, dir, "delete_affected_empty_dir");
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

    FinishParallelDeletes(state);
    DeleteQueuedEmptyDirectories(state);
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
    FinishParallelDeletes(state);
    DeleteQueuedEmptyDirectories(state);
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
            const bool removed = std::filesystem::remove(toLongPath(previousRoot), rootEc);
            if (removed && !rootEc) {
                console.showInfo("Removed previous install root: " + Utf8FromPath(previousRoot));
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

    if (IsCancelled(cancellationCallback)) {
        console.showWarning("Upgrade cleanup cancelled before deleting files.");
        return false;
    }
    DeleteFilesParallel(state, filesToDelete);
    FinishParallelDeletes(state);
    EmitProgress(progressCallback, 0.8f, "Removing old files");

    if (IsCancelled(cancellationCallback)) {
        console.showWarning("Upgrade cleanup cancelled before directory cleanup.");
        return false;
    }

    DeleteAffectedEmptyDirs(state, filesToDelete, previousRoot, !sameInstallRoot);
    EmitProgress(progressCallback, 0.95f, "Removing empty directories from previous install root");

    if (sameInstallRoot) {
        std::error_code rootEc;
        if (std::filesystem::is_empty(previousRoot, rootEc)) {
            console.showInfo("Previous install root is empty and will be reused: " +
                             Utf8FromPath(previousRoot));
        } else {
            console.showInfo("Previous install root is not empty after upgrade cleanup: " +
                             Utf8FromPath(previousRoot));
        }
    } else {
        const uint64_t deletedBefore = state.result.deletedCount;
        DeleteDirectoryIfEmptyByRemove(state, previousRoot, "delete_previous_root");
        if (state.result.deletedCount > deletedBefore) {
            console.showInfo("Removed previous install root: " + Utf8FromPath(previousRoot));
        } else {
            console.showInfo("Previous install root is not empty after upgrade cleanup: " +
                             Utf8FromPath(previousRoot));
        }
    }

    EmitProgress(progressCallback, 1.0f, "Upgrade cleanup completed");
    return true;
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
        FinishParallelDeletes(state);
        DeleteQueuedEmptyDirectories(state);
        if (!sameInstallRoot) {
            DeleteDirectoryIfEmptyByRemove(state, previousRoot, "delete_previous_root");
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
        if (key.empty()) {
            return;
        }
        // Difference-set cleanup: a file that also exists in the new package is
        // left in place so the extractor can skip or overwrite it.
        if (!state.task.keepFiles.empty() && state.task.keepFiles.count(key) > 0) {
            return;
        }
        if (seen.insert(key).second) {
            filesToDelete.push_back(std::move(path));
        }
    };
    for (const auto& file : CollectManifestFiles(manifest)) {
        addFile(PathFromUtf8(file));
    }
    addFile(previousRoot / "uninstall.exe");
    addFile(previousRoot / "install.manifest.json");

    DeleteFilesParallel(state, filesToDelete);
    FinishParallelDeletes(state);
    EmitCleanupProgress(state, 0.8f, "Removing old files");

    DeleteAffectedEmptyDirs(state, filesToDelete, previousRoot, !sameInstallRoot);
    if (!sameInstallRoot) {
        DeleteDirectoryIfEmptyByRemove(state, previousRoot, "delete_previous_root");
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
    FinishParallelDeletes(state);
    DeleteQueuedEmptyDirectories(state);
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
    const std::filesystem::path heartbeatPath = BuildCleanupTempPath(".heartbeat.json");
    task.heartbeatPath = Utf8FromPath(heartbeatPath);
    task.resultPath.clear();
    auto cleanupFuture = std::async(std::launch::async, [task = std::move(task)]() {
        return ExecuteCleanupTask(task);
    });

    const uint64_t startedMs = NowMs();
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
        if (IsCancelled(cancellationCallback)) {
            fallback.success = false;
            fallback.message = "Cleanup cancelled";
            cancelled = true;
            break;
        }
        if (cleanupFuture.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready) {
            break;
        }

        json heartbeat;
        if (ReadJsonFileBestEffort(heartbeatPath, heartbeat)) {
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
            fallback.timedOutPath = slowestPath.empty() ? lastPath : slowestPath;
            fallback.message = totalTimedOut ? "Cleanup worker total timeout"
                                             : "Cleanup worker heartbeat stale";
            logInstallerWarning("[InstallFlow][Cleanup] worker timeout message=" + fallback.message +
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

    UpgradeCleanupResult result = fallback;
    if (!timedOut && !cancelled) {
        result = cleanupFuture.get();
    } else {
        // In-process cleanup threads cannot be safely killed. Preserve the
        // timeout/cancel result for the caller, but wait for the worker to
        // unwind so no background deletion continues after completion UI.
        cleanupFuture.wait();
    }

    std::error_code ec;
    std::filesystem::remove(heartbeatPath, ec);
    return result;
#else
    (void)progressCallback;
    (void)cancellationCallback;
    return ExecuteCleanupTask(task);
#endif
}

UpgradeCleanupResult runPreviousInstallCleanupWithWatchdog(
    const std::string& manifestPath,
    const std::string& previousInstallDir,
    const std::string& newInstallDir,
    const std::vector<std::string>& replacementTargets,
    const UpgradeCleanupProgressCallback& progressCallback,
    const std::function<bool()>& cancellationCallback,
    const UpgradeCleanupPolicy& policy,
    const std::vector<std::string>& keepFiles) {
    UpgradeCleanupResult result;
    const std::filesystem::path previousRoot = PathFromUtf8(previousInstallDir).lexically_normal();

    std::set<std::string> normalizedKeepFiles;
    for (const auto& keep : keepFiles) {
        if (keep.empty()) {
            continue;
        }
        const std::string key = NormalizePath(PathFromUtf8(keep));
        if (!key.empty()) {
            normalizedKeepFiles.insert(key);
        }
    }
    const bool incrementalCleanup = !normalizedKeepFiles.empty();

    json manifest;
    const bool hasManifest = ReadManifestJson(manifestPath, manifest);
    std::error_code existsEc;
    // Whole-subtree isolation moves entire directories aside, which would defeat
    // incremental skip by relocating unchanged files. Skip it when a keep-set is
    // available and rely on per-file difference-set deletion instead.
    if (!incrementalCleanup &&
        !previousInstallDir.empty() &&
        std::filesystem::exists(previousRoot, existsEc) &&
        std::filesystem::is_directory(previousRoot, existsEc) &&
        !IsProtectedFullCleanupRoot(previousRoot)) {
        const std::vector<std::filesystem::path> candidates =
            BuildSubdirectoryIsolationCandidates(hasManifest ? &manifest : nullptr,
                                                 previousRoot,
                                                 replacementTargets);
        const std::vector<std::filesystem::path> pendingDirs =
            IsolateCleanupSubdirectories(candidates);
        if (!pendingDirs.empty()) {
            UpgradeCleanupTask isolatedTask;
            isolatedTask.mode = UpgradeCleanupTaskMode::ExtraPaths;
            isolatedTask.previousInstallDir = previousInstallDir;
            isolatedTask.policy = policy;
            isolatedTask.policy.allowPartialSuccess = true;
            isolatedTask.extraPaths.reserve(pendingDirs.size());
            for (const auto& pending : pendingDirs) {
                UninstallCleanupRule rule;
                rule.path = Utf8FromPath(pending);
                rule.recursive = true;
                rule.onlyIfEmpty = false;
                isolatedTask.extraPaths.push_back(std::move(rule));
            }
            result = RunTaskWithWatchdog(std::move(isolatedTask), progressCallback, cancellationCallback);
            result.skippedCount += candidates.size() - pendingDirs.size();
            result.partial = result.partial || result.skippedCount > 0;
            if (result.message.empty()) {
                result.message = "Previous install subdirectories isolated and cleaned";
            }
        }
    }

    UpgradeCleanupTask task;
    task.mode = UpgradeCleanupTaskMode::PreviousInstall;
    task.previousInstallDir = previousInstallDir;
    task.newInstallDir = newInstallDir;
    task.manifestPath = manifestPath;
    task.policy = policy;
    task.keepFiles = std::move(normalizedKeepFiles);
    UpgradeCleanupResult manifestResult =
        RunTaskWithWatchdog(std::move(task), progressCallback, cancellationCallback);
    manifestResult.deletedCount += result.deletedCount;
    manifestResult.failedCount += result.failedCount;
    manifestResult.skippedCount += result.skippedCount;
    manifestResult.partial = manifestResult.partial || result.partial;
    manifestResult.timedOut = manifestResult.timedOut || result.timedOut;
    if (manifestResult.timedOutPath.empty()) {
        manifestResult.timedOutPath = result.timedOutPath;
    }
    if (!result.success) {
        manifestResult.partial = true;
    }
    manifestResult.success = manifestResult.success && (result.success || policy.allowPartialSuccess);
    if (manifestResult.message.empty()) {
        manifestResult.message = result.message;
    }
    return manifestResult;
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
    for (const auto& name : metadata.installerCleanup.legacy.startupNames) {
        AppendUniqueName(installAutoStartupNames, seenNames, name);
    }

    const size_t totalSteps = installAutoStartupNames.size() +
                              metadata.installerCleanup.legacy.desktopShortcutNames.size() +
                              metadata.installerCleanup.systemUninstallEntry.legacyEntries.size() +
                              metadata.installerCleanup.registry.deleteKeys.size() +
                              metadata.installerCleanup.registry.deleteValues.size() +
                              (cleanupExtraPaths ? metadata.installerCleanup.paths.size() : 0);
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

    for (const auto& shortcutName : metadata.installerCleanup.legacy.desktopShortcutNames) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing shortcuts.");
            return false;
        }
        if (!shortcutName.empty()) {
            deleteDesktopShortcut(shortcutName);
            deleteStartMenuShortcut(shortcutName);
        }
        emitStep("Removing legacy shortcut");
    }

#ifdef _WIN32
    for (const auto& entry : metadata.installerCleanup.systemUninstallEntry.legacyEntries) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing uninstall entries.");
            return false;
        }
        if (!deleteSystemUninstallEntryByDisplayName(entry.displayName, entry.scope)) {
            const std::string message =
                "Upgrade system cleanup failed to remove legacy uninstall entry: " + entry.displayName;
            console.showWarning(message);
            logInstallerWarning(message);
        }
        emitStep("Removing uninstall entry");
    }
#endif

    for (const auto& path : metadata.installerCleanup.registry.deleteKeys) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing registry.");
            return false;
        }
        if (!deleteRegistryPath(path)) {
            console.showWarning("Upgrade cleanup failed to remove registry path: " + path);
        }
        emitStep("Removing legacy registry entry");
    }

    for (const auto& entry : metadata.installerCleanup.registry.deleteValues) {
        if (IsCancelled(cancellationCallback)) {
            console.showWarning("Upgrade system cleanup cancelled while removing registry.");
            return false;
        }
        if (!deleteRegistryValue(entry)) {
            console.showWarning("Upgrade cleanup failed to remove registry value: " +
                                entry.path + "\\" + entry.key);
        }
        emitStep("Removing legacy registry value");
    }

    if (cleanupExtraPaths) {
        for (const auto& rule : metadata.installerCleanup.paths) {
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
