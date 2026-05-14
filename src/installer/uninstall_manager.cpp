#include "installer/uninstall_manager.h"
#include "installer/install_manifest_store.h"
#include "installer/self_delete_scheduler.h"
#include "installer/install_state_utils.h"
#include "installer/installer_helpers.h"
#include "installer/installed_instance_resolver.h"
#include "installer/registry_utils.h"
#include "installer/shortcut_startup_utils.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <limits>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <set>
#include <mutex>
#include <condition_variable>
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
    context.fallbackAllowed = true;
    if (metadata) {
        context.appId = resolveEffectiveAppId(metadata->appId, metadata->appName);
        context.appName = metadata->appName;
        context.installInfoRegistryPath = metadata->installInfo.path;
    }
    context.errorMessage = "Uninstall manifest missing; safe fallback uninstall will be used.";
    return true;
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

json UninstallPolicyToJson(const UninstallCleanupPolicy& policy) {
    return {
        {"itemStaleTimeoutMs", policy.itemStaleTimeoutMs},
        {"totalTimeoutMs", policy.totalTimeoutMs},
        {"heartbeatIntervalMs", policy.heartbeatIntervalMs},
        {"heartbeatEveryItems", policy.heartbeatEveryItems},
        {"slowItemLogMs", policy.slowItemLogMs},
        {"workerConcurrency", policy.workerConcurrency},
        {"allowPartialSuccess", policy.allowPartialSuccess},
    };
}

UninstallCleanupPolicy UninstallPolicyFromJson(const json& value) {
    UninstallCleanupPolicy policy;
    if (!value.is_object()) {
        return policy;
    }
    policy.itemStaleTimeoutMs = value.value("itemStaleTimeoutMs", policy.itemStaleTimeoutMs);
    policy.totalTimeoutMs = value.value("totalTimeoutMs", policy.totalTimeoutMs);
    policy.heartbeatIntervalMs = value.value("heartbeatIntervalMs", policy.heartbeatIntervalMs);
    policy.heartbeatEveryItems = value.value("heartbeatEveryItems", policy.heartbeatEveryItems);
    policy.slowItemLogMs = value.value("slowItemLogMs", policy.slowItemLogMs);
    policy.workerConcurrency = value.value("workerConcurrency", policy.workerConcurrency);
    policy.allowPartialSuccess = value.value("allowPartialSuccess", policy.allowPartialSuccess);
    return policy;
}

json UninstallCleanupRuleToJson(const UninstallCleanupRule& rule) {
    return {{"path", rule.path}, {"recursive", rule.recursive}, {"onlyIfEmpty", rule.onlyIfEmpty}};
}

UninstallCleanupRule UninstallCleanupRuleFromJson(const json& value) {
    UninstallCleanupRule rule;
    if (!value.is_object()) {
        return rule;
    }
    rule.path = value.value("path", "");
    rule.recursive = value.value("recursive", true);
    rule.onlyIfEmpty = value.value("onlyIfEmpty", false);
    return rule;
}

json UninstallTaskToJson(const UninstallCleanupTask& task) {
    json root;
    root["fallbackMode"] = task.fallbackMode;
    root["installDir"] = task.installDir;
    root["manifestPath"] = task.manifestPath;
    root["uninstallPath"] = task.uninstallPath;
    root["currentExePath"] = task.currentExePath;
    root["heartbeatPath"] = task.heartbeatPath;
    root["resultPath"] = task.resultPath;
    root["files"] = task.files;
    root["cleanupRoots"] = task.cleanupRoots;
    root["policy"] = UninstallPolicyToJson(task.policy);
    root["cleanupPaths"] = json::array();
    for (const auto& rule : task.cleanupPaths) {
        root["cleanupPaths"].push_back(UninstallCleanupRuleToJson(rule));
    }
    return root;
}

bool UninstallTaskFromJson(const json& root, UninstallCleanupTask& task) {
    if (!root.is_object()) {
        return false;
    }
    task.fallbackMode = root.value("fallbackMode", false);
    task.installDir = root.value("installDir", "");
    task.manifestPath = root.value("manifestPath", "");
    task.uninstallPath = root.value("uninstallPath", "");
    task.currentExePath = root.value("currentExePath", "");
    task.heartbeatPath = root.value("heartbeatPath", "");
    task.resultPath = root.value("resultPath", "");
    task.files = root.value("files", std::vector<std::string>{});
    task.cleanupRoots = root.value("cleanupRoots", std::vector<std::string>{});
    task.policy = UninstallPolicyFromJson(root.value("policy", json::object()));
    task.cleanupPaths.clear();
    for (const auto& item : root.value("cleanupPaths", json::array())) {
        task.cleanupPaths.push_back(UninstallCleanupRuleFromJson(item));
    }
    return true;
}

json UninstallResultToJson(const UninstallCleanupResult& result) {
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

UninstallCleanupResult UninstallResultFromJson(const json& value) {
    UninstallCleanupResult result;
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
};

struct DeleteBatchWorkerPool {
    UninstallCleanupExecutionState& state;
    std::vector<std::filesystem::path> files;
    std::mutex filesMutex;
    std::condition_variable cv;
    std::atomic<bool> done{false};
    std::vector<std::thread> workers;

    explicit DeleteBatchWorkerPool(UninstallCleanupExecutionState& stateRef)
        : state(stateRef) {}
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
    };
    WriteUninstallJsonBestEffort(PathFromUtf8(state.task.heartbeatPath), heartbeat);
    state.lastHeartbeatMs = now;
    state.processedSinceHeartbeat = 0;
}

void SetUninstallCurrentItem(UninstallCleanupExecutionState& state,
                             const std::filesystem::path& path,
                             const std::string& action,
                             uint64_t startedMs) {
    state.currentPath = Utf8FromPath(path);
    state.currentAction = action;
    state.currentStartedMs = startedMs;
    // Do not force heartbeat here. File deletion is the hot path for large
    // node_modules-like trees; heartbeat must stay time/count-throttled.
    EmitUninstallHeartbeat(state, false);
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
    ++state.processedSinceHeartbeat;
    EmitUninstallHeartbeat(state, false);
}

uint32_t ResolveUninstallWorkerConcurrency(const UninstallCleanupPolicy& policy) {
    if (policy.workerConcurrency > 0) {
        return std::max<uint32_t>(1, policy.workerConcurrency);
    }
    const unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) {
        return 2;
    }
    return std::min<uint32_t>(8, std::max<uint32_t>(2, hw / 2));
}

void DeleteUninstallFilesParallel(UninstallCleanupExecutionState& state,
                                  const std::vector<std::filesystem::path>& files) {
    if (files.empty()) {
        return;
    }
    const uint32_t concurrency = std::min<uint32_t>(
        ResolveUninstallWorkerConcurrency(state.task.policy),
        static_cast<uint32_t>(files.size()));
    state.workerConcurrency = concurrency;

    std::atomic<size_t> nextIndex{0};
    auto worker = [&]() {
        while (true) {
            const size_t index = nextIndex.fetch_add(1);
            if (index >= files.size()) {
                break;
            }
            const auto& path = files[index];
            const uint64_t started = UninstallNowMs();
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                SetUninstallCurrentItem(state, path, "delete_file_parallel", started);
            }

            std::error_code ec;
            const bool removed = std::filesystem::remove(toLongPath(path), ec);
            const uint64_t elapsed = UninstallNowMs() - started;

            {
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
                ++state.processedCount;
                ++state.processedSinceHeartbeat;
                EmitUninstallHeartbeat(state, false);
            }

            if ((index + 1) % 200 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(concurrency);
    for (uint32_t i = 0; i < concurrency; ++i) {
        workers.emplace_back(worker);
    }
    for (auto& thread : workers) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void StartDeleteBatchWorkerPool(DeleteBatchWorkerPool& pool) {
    const uint32_t concurrency = ResolveUninstallWorkerConcurrency(pool.state.task.policy);
    pool.state.workerConcurrency = concurrency;
    pool.workers.reserve(concurrency);
    for (uint32_t i = 0; i < concurrency; ++i) {
        pool.workers.emplace_back([&pool]() {
            while (true) {
                std::filesystem::path path;
                {
                    std::unique_lock<std::mutex> lock(pool.filesMutex);
                    pool.cv.wait(lock, [&]() {
                        return pool.done.load() || !pool.files.empty();
                    });
                    if (pool.files.empty()) {
                        if (pool.done.load()) {
                            return;
                        }
                        continue;
                    }
                    path = std::move(pool.files.back());
                    pool.files.pop_back();
                }

                const uint64_t started = UninstallNowMs();
                {
                    std::lock_guard<std::mutex> lock(pool.state.mutex);
                    SetUninstallCurrentItem(pool.state, path, "delete_file_parallel", started);
                }

                std::error_code ec;
                const bool removed = std::filesystem::remove(toLongPath(path), ec);
                const uint64_t elapsed = UninstallNowMs() - started;

                {
                    std::lock_guard<std::mutex> lock(pool.state.mutex);
                    if (ec) {
                        ++pool.state.result.failedCount;
                        logInstallerWarning("[Uninstall][Cleanup] delete failed path=" +
                                            Utf8FromPath(path) + " error=" + ec.message());
                    } else if (!removed) {
                        ++pool.state.result.skippedCount;
                    } else {
                        ++pool.state.result.deletedCount;
                        if (elapsed >= pool.state.task.policy.slowItemLogMs) {
                            logInstallerWarning("[Uninstall][Cleanup] slow delete path=" +
                                                Utf8FromPath(path) + " elapsedMs=" +
                                                std::to_string(elapsed));
                        }
                    }
                    ++pool.state.processedCount;
                    ++pool.state.processedSinceHeartbeat;
                    EmitUninstallHeartbeat(pool.state, false);
                }
            }
        });
    }
}

void SubmitDeleteBatch(DeleteBatchWorkerPool& pool,
                       std::vector<std::filesystem::path>& batch) {
    if (batch.empty()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(pool.filesMutex);
        for (auto& file : batch) {
            pool.files.push_back(std::move(file));
        }
    }
    batch.clear();
    pool.cv.notify_all();
}

void FinishDeleteBatchWorkerPool(DeleteBatchWorkerPool& pool) {
    pool.done.store(true);
    pool.cv.notify_all();
    for (auto& worker : pool.workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
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
    DeleteBatchWorkerPool pool(state);
    StartDeleteBatchWorkerPool(pool);
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    std::error_code iterEc;
    for (std::filesystem::recursive_directory_iterator it(toLongPath(root), options, iterEc), end;
         !iterEc && it != end;
         it.increment(iterEc)) {
        const auto path = it->path();
        if (IsReparsePointPathLocal(path)) {
            it.disable_recursion_pending();
            ++state.result.skippedCount;
            continue;
        }
        std::error_code typeEc;
        if (it->is_directory(typeEc)) {
            dirs.push_back(path);
        } else {
            batch.push_back(path);
            if (batch.size() >= 512) {
                SubmitDeleteBatch(pool, batch);
            }
        }
    }
    SubmitDeleteBatch(pool, batch);
    FinishDeleteBatchWorkerPool(pool);
    if (iterEc) {
        ++state.result.failedCount;
        logInstallerWarning("[Uninstall][Cleanup] directory scan failed path=" + Utf8FromPath(root) +
                            " error=" + iterEc.message());
    }
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
        return a.native().size() > b.native().size();
    });
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (std::filesystem::is_empty(dir, ec)) {
            DeleteUninstallSinglePath(state, dir, "delete_empty_dir");
        }
    }
    if (removeRoot) {
        std::error_code ec;
        if (std::filesystem::is_empty(root, ec)) {
            DeleteUninstallSinglePath(state, root, "delete_root_dir");
        }
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
            std::error_code ec;
            if (std::filesystem::exists(dir, ec) && std::filesystem::is_empty(dir, ec)) {
                DeleteUninstallSinglePath(state, dir, "delete_affected_empty_dir");
            }
        }
        std::error_code rootEc;
        if (std::filesystem::exists(root, rootEc) && std::filesystem::is_empty(root, rootEc)) {
            const bool currentExeInsideRoot =
                !currentExePath.empty() && IsPathUnderOrEqualLocal(currentExe, root);
            if (!currentExeInsideRoot) {
                DeleteUninstallSinglePath(state, root, "delete_cleanup_root");
            }
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
        std::error_code ec;
        if (std::filesystem::exists(installRoot, ec) && std::filesystem::is_empty(installRoot, ec)) {
            const bool currentExeInsideRoot =
                !task.currentExePath.empty() &&
                IsPathUnderOrEqualLocal(PathFromUtf8(task.currentExePath).lexically_normal(), installRoot);
            if (!currentExeInsideRoot) {
                DeleteUninstallSinglePath(state, installRoot, "delete_install_root");
            }
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
    const auto taskPath = BuildUninstallCleanupTempPath(".json");
    const auto heartbeatPath = BuildUninstallCleanupTempPath(".heartbeat.json");
    const auto resultPath = BuildUninstallCleanupTempPath(".result.json");
    task.heartbeatPath = Utf8FromPath(heartbeatPath);
    task.resultPath = Utf8FromPath(resultPath);
    if (!WriteUninstallJsonBestEffort(taskPath, UninstallTaskToJson(task))) {
        fallback.success = false;
        fallback.message = "Failed to write uninstall cleanup task";
        return fallback;
    }
    const std::wstring exePath = Utf8ToWide(getCurrentExecutablePath());
    std::wstring cmd = QuoteUninstallArg(exePath) + L" --uninstall-cleanup-worker " +
                       QuoteUninstallArg(taskPath.wstring());
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    const std::string parentLogPath = getInstallerLogPath();
    std::wstring envBlock;
    if (!parentLogPath.empty()) {
        LPWCH currentEnv = GetEnvironmentStringsW();
        if (currentEnv) {
            for (LPWCH entry = currentEnv; *entry != L'\0'; entry += wcslen(entry) + 1) {
                envBlock.append(entry);
                envBlock.push_back(L'\0');
            }
            FreeEnvironmentStringsW(currentEnv);
        }
        envBlock.append(L"MTINSTALLER_LOG_PATH=");
        envBlock.append(Utf8ToWide(parentLogPath));
        envBlock.push_back(L'\0');
        envBlock.push_back(L'\0');
    }
    DWORD creationFlags = CREATE_NO_WINDOW;
    if (!envBlock.empty()) {
        creationFlags |= CREATE_UNICODE_ENVIRONMENT;
    }
    BOOL started = CreateProcessW(exePath.c_str(), cmdLine.data(), nullptr, nullptr, FALSE,
                                  creationFlags,
                                  envBlock.empty() ? nullptr : envBlock.data(),
                                  nullptr,
                                  &si,
                                  &pi);
    if (!started) {
        fallback.success = false;
        fallback.message = "Failed to start uninstall cleanup worker";
        std::error_code ec;
        std::filesystem::remove(taskPath, ec);
        return fallback;
    }
    const uint64_t startedMs = UninstallNowMs();
    uint64_t lastHeartbeatTimestamp = startedMs;
    std::string lastPath;
    bool killed = false;
    while (true) {
        if (cancellationCallback && cancellationCallback()) {
            TerminateProcess(pi.hProcess, 2);
            killed = true;
            fallback.success = false;
            fallback.message = "Uninstall cleanup cancelled";
            break;
        }
        DWORD wait = WaitForSingleObject(pi.hProcess, 250);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        json heartbeat;
        if (ReadUninstallJsonBestEffort(heartbeatPath, heartbeat)) {
            lastHeartbeatTimestamp = heartbeat.value("timestampMs", lastHeartbeatTimestamp);
            lastPath = heartbeat.value("currentPath", lastPath);
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
            fallback.timedOutPath = lastPath;
            fallback.message = totalTimedOut ? "Uninstall cleanup total timeout"
                                             : "Uninstall cleanup heartbeat stale";
            logInstallerWarning("[Uninstall][Cleanup] worker timeout message=" + fallback.message +
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
    UninstallCleanupResult result = fallback;
    if (!killed) {
        json resultJson;
        if (ReadUninstallJsonBestEffort(resultPath, resultJson)) {
            result = UninstallResultFromJson(resultJson);
        } else {
            result.success = exitCode == 0;
            result.partial = exitCode != 0;
            result.message = "Uninstall cleanup worker finished without result file";
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
    return ExecuteUninstallCleanupTask(task);
#endif
}

bool ResolveUninstallContext(const ExtendedInstallationMetadata* metadata,
                             InstallerPathResolver& resolver,
                             const std::string& explicitManifestPath,
                             UninstallContext& context) {
    (void)resolver;
    context = UninstallContext{};

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
        auto valueIt = metadata->installInfo.values.find("installDir");
        if (!metadata->installInfo.path.empty() &&
            valueIt != metadata->installInfo.values.end() &&
            !valueIt->second.key.empty()) {
            std::string installDir;
            std::string manifestPath;
            if (resolveInstallInfoFromRegistry(metadata->installInfo.path,
                                               valueIt->second.key,
                                               manifestPath,
                                               installDir)) {
                if (TryLoadManifestIntoContext(manifestPath, context, false)) {
                    return context.manifestReadable;
                }
                if (TrySetFallbackContext(installDir, metadata, context, manifestPath)) {
                    return true;
                }
            }
        }

        InstalledInstanceInfo installedInstance;
        if (resolveInstalledInstanceFromInstallRoots(*metadata, installedInstance)) {
            if (TryLoadManifestIntoContext(installedInstance.manifestPath, context, false)) {
                return context.manifestReadable;
            }
            if (TrySetFallbackContext(installedInstance.installDir,
                                      metadata,
                                      context,
                                      installedInstance.manifestPath)) {
                return true;
            }
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

    const std::string displayName =
        !context.appName.empty() ? context.appName : (metadata ? metadata->appName : std::string());
    if (!displayName.empty()) {
        removeAutoStartup(displayName);
        deleteDesktopShortcut(displayName);
        deleteStartMenuShortcut(displayName);
        deleteUninstallRegistryEntry(displayName, false);
        deleteUninstallRegistryEntry(displayName, true);
    }
    if (!context.appId.empty() && context.appId != displayName) {
        deleteUninstallRegistryEntry(context.appId, false);
        deleteUninstallRegistryEntry(context.appId, true);
    }
    if (!context.installInfoRegistryPath.empty()) {
        deleteRegistryPath(context.installInfoRegistryPath);
    } else if (metadata && !metadata->installInfo.path.empty()) {
        deleteRegistryPath(metadata->installInfo.path);
    }

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

int runUninstallCleanupWorkerFromTask(const std::string& taskPath) {
    initializeInstallerLogging();
    json taskJson;
    UninstallCleanupTask task;
    if (!ReadUninstallJsonBestEffort(PathFromUtf8(taskPath), taskJson) ||
        !UninstallTaskFromJson(taskJson, task)) {
        logInstallerError("[Uninstall][Cleanup] worker failed to read task");
        return 2;
    }
#ifdef _WIN32
    char delayBuffer[32] = {};
    DWORD delayLen = GetEnvironmentVariableA("MTINSTALLER_TEST_UNINSTALL_CLEANUP_WORKER_DELAY_MS",
                                             delayBuffer,
                                             static_cast<DWORD>(sizeof(delayBuffer)));
    if (delayLen > 0 && delayLen < sizeof(delayBuffer)) {
        const DWORD delayMs = static_cast<DWORD>(std::strtoul(delayBuffer, nullptr, 10));
        if (delayMs > 0) {
            Sleep(delayMs);
        }
    }
#endif
    UninstallCleanupResult result = ExecuteUninstallCleanupTask(task);
    if (!task.resultPath.empty()) {
        WriteUninstallJsonBestEffort(PathFromUtf8(task.resultPath), UninstallResultToJson(result));
    }
    logInstallerInfo("[Uninstall][Cleanup] worker end deleted=" + std::to_string(result.deletedCount) +
                     " failed=" + std::to_string(result.failedCount) +
                     " skipped=" + std::to_string(result.skippedCount) +
                     " partial=" + std::string(result.partial ? "true" : "false"));
    flushInstallerLogging();
    return result.success ? 0 : 1;
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

    applyCoreInstallInfo(installInfo,
                         installDir,
                         manifest.value("appVersion", ""),
                         displayName,
                         "uninstalled",
                         resolver);
    completeWorkUnit("Marking uninstalled state");

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
