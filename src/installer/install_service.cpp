#include "installer/install_service.h"

#include "common/utf8_utils.h"
#include "installer/console_interface.h"
#include "installer/install_state_utils.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"
#include "installer/uninstall_manager.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <urlmon.h>
#endif

namespace MultiThreadedInstaller {

namespace {

constexpr float kPrecheckStart = 0.00f;
constexpr float kPrecheckEnd = 0.15f;
constexpr float kCleanupStart = 0.15f;
constexpr float kCleanupEnd = 0.35f;
constexpr float kInstallStart = 0.35f;
constexpr float kInstallEnd = 0.92f;
constexpr float kFinalizeStart = 0.92f;
constexpr float kFinalizeEnd = 1.00f;

float Clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float ToOverallProgress(InstallServicePhase phase, float phaseProgress) {
    const float clamped = Clamp01(phaseProgress);
    switch (phase) {
        case InstallServicePhase::Precheck:
            return kPrecheckStart + (kPrecheckEnd - kPrecheckStart) * clamped;
        case InstallServicePhase::CleanupOldInstall:
            return kCleanupStart + (kCleanupEnd - kCleanupStart) * clamped;
        case InstallServicePhase::Installing:
            return kInstallStart + (kInstallEnd - kInstallStart) * clamped;
        case InstallServicePhase::Finalizing:
            return kFinalizeStart + (kFinalizeEnd - kFinalizeStart) * clamped;
        case InstallServicePhase::None:
        default:
            return 0.0f;
    }
}

void EmitEvent(const InstallServiceCallbacks& callbacks, const InstallServiceEvent& event) {
    if (callbacks.onEvent) {
        callbacks.onEvent(event);
    }
}

void EmitStatus(const InstallServiceCallbacks& callbacks,
                InstallServiceStatus status,
                InstallServicePhase phase,
                float phaseProgress,
                float overallProgress,
                const std::string& message = std::string()) {
    InstallServiceEvent event;
    event.type = InstallServiceEventType::Status;
    event.status = status;
    event.phase = phase;
    event.phaseProgress = Clamp01(phaseProgress);
    event.overallProgress = Clamp01(overallProgress);
    event.progress = event.overallProgress;
    event.message = message;
    EmitEvent(callbacks, event);
}

void EmitMessage(const InstallServiceCallbacks& callbacks,
                 InstallServiceEventType type,
                 InstallServiceStatus status,
                 InstallServicePhase phase,
                 float phaseProgress,
                 float overallProgress,
                 const std::string& message) {
    if (message.empty()) {
        return;
    }
    InstallServiceEvent event;
    event.type = type;
    event.status = status;
    event.phase = phase;
    event.phaseProgress = Clamp01(phaseProgress);
    event.overallProgress = Clamp01(overallProgress);
    event.progress = event.overallProgress;
    event.message = message;
    EmitEvent(callbacks, event);
}

void EmitProgress(const InstallServiceCallbacks& callbacks,
                  InstallServiceStatus status,
                  InstallServicePhase phase,
                  const std::string& folder,
                  const std::string& currentFile,
                  float phaseProgress,
                  float overallProgress) {
    InstallServiceEvent event;
    event.type = InstallServiceEventType::Progress;
    event.status = status;
    event.phase = phase;
    event.folder = folder;
    event.currentFile = currentFile;
    event.phaseProgress = Clamp01(phaseProgress);
    event.overallProgress = Clamp01(overallProgress);
    event.progress = event.overallProgress;
    EmitEvent(callbacks, event);
}

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
}

std::vector<std::string> CollectFilesRecursive(const std::vector<std::string>& roots) {
    std::vector<std::string> files;
    for (const auto& rootPath : roots) {
        if (rootPath.empty()) {
            continue;
        }
        std::filesystem::path root = PathFromUtf8(rootPath);
        if (!std::filesystem::exists(root)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) {
                files.push_back(Utf8FromPath(entry.path()));
            }
        }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

std::string ResolveLanguageCode(const std::string& preferredLanguage) {
    if (!preferredLanguage.empty()) {
        return preferredLanguage;
    }
#ifdef _WIN32
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
#else
    return "en_US";
#endif
}

struct ComponentSelectionPlan {
    bool hasComponents = false;
    std::vector<const ComponentConfig*> ordered;
    std::vector<std::string> embeddedFolders;
    std::vector<RegistryEntry> registryEntries;
    std::vector<std::string> killProcesses;
    bool autoStartup = false;
    bool desktopIcons = false;
};

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

bool BuildComponentSelectionPlan(const ExtendedInstallationMetadata& metadata,
                                 const InstallServiceOptions& options,
                                 ComponentSelectionPlan& plan,
                                 std::string& error) {
    plan = ComponentSelectionPlan{};
    if (metadata.components.empty()) {
        return true;
    }

    plan.hasComponents = true;

    std::unordered_map<std::string, const ComponentConfig*> index;
    index.reserve(metadata.components.size());
    for (const auto& component : metadata.components) {
        if (!component.id.empty()) {
            index[component.id] = &component;
        }
    }

    std::unordered_set<std::string> initialSelection;
    initialSelection.reserve(metadata.components.size());
    for (const auto& component : metadata.components) {
        if (component.required) {
            initialSelection.insert(component.id);
        }
    }

    if (options.installAllComponents) {
        for (const auto& component : metadata.components) {
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
        for (const auto& component : metadata.components) {
            if (component.defaultSelected) {
                initialSelection.insert(component.id);
            }
        }
    }

    std::unordered_set<std::string> selected;
    selected.reserve(metadata.components.size());
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

    for (const auto& component : metadata.components) {
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
                     mapping.folderName) != plan.embeddedFolders.end();
}

std::string ExpandInstallDirToken(const std::string& text, const std::string& installDir) {
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

std::string ExpandRuntimeTokens(const std::string& text,
                                const std::string& installDir,
                                const ExtendedInstallationMetadata& metadata,
                                InstallerPathResolver& resolver,
                                const std::string& componentInstallDir = {}) {
    if (text.empty()) {
        return text;
    }

    std::string expanded = ExpandInstallDirToken(text, installDir);
    const std::vector<std::pair<std::string, std::string>> tokens = {
        { "%AppVersion%", metadata.configVersion },
        { "%AppId%", resolveEffectiveAppId(metadata.appId, metadata.applicationName) },
        { "%DirectoryName%", resolveEffectiveDirectoryName(metadata.directoryName, metadata.applicationName) },
        { "%ComponentInstallDir%", componentInstallDir }
    };
    for (const auto& token : tokens) {
        if (token.second.empty()) {
            continue;
        }
        size_t position = 0;
        while ((position = expanded.find(token.first, position)) != std::string::npos) {
            expanded.replace(position, token.first.size(), token.second);
            position += token.second.size();
        }
    }
    return resolver.expandEnvironmentVariables(expanded);
}

std::string NormalizePathString(const std::filesystem::path& path) {
    std::string value = Utf8FromPath(path.lexically_normal());
    std::replace(value.begin(), value.end(), '/', '\\');
    while (!value.empty() && (value.back() == '\\' || value.back() == '/')) {
        value.pop_back();
    }
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsPathUnderBase(const std::filesystem::path& base, const std::filesystem::path& candidate) {
    const std::string baseNormalized = NormalizePathString(base);
    const std::string candidateNormalized = NormalizePathString(candidate);
    if (baseNormalized.empty() || candidateNormalized.empty()) {
        return false;
    }
    if (candidateNormalized == baseNormalized) {
        return true;
    }
    if (candidateNormalized.size() <= baseNormalized.size()) {
        return false;
    }
    if (candidateNormalized.compare(0, baseNormalized.size(), baseNormalized) != 0) {
        return false;
    }
    return candidateNormalized[baseNormalized.size()] == '\\';
}

#ifdef _WIN32
std::wstring QuoteProcessPath(const std::wstring& value) {
    if (value.empty()) {
        return L"\"\"";
    }
    if (value.front() == L'"' && value.back() == L'"') {
        return value;
    }
    return L"\"" + value + L"\"";
}

bool IsBatchScriptPath(const std::filesystem::path& executablePath) {
    std::wstring extension = executablePath.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return extension == L".bat" || extension == L".cmd";
}

bool WaitForProcessExit(HANDLE processHandle,
                        uint32_t timeoutSec,
                        const std::function<bool()>& cancellationCallback,
                        DWORD& exitCode,
                        std::string& error) {
    const uint64_t timeoutMs = timeoutSec == 0
                                   ? std::numeric_limits<uint64_t>::max()
                                   : static_cast<uint64_t>(timeoutSec) * 1000ULL;
    uint64_t waitedMs = 0;

    while (true) {
        if (cancellationCallback && cancellationCallback()) {
            TerminateProcess(processHandle, 1);
            error = "Component execution cancelled.";
            return false;
        }

        DWORD sliceMs = 200;
        if (timeoutMs != std::numeric_limits<uint64_t>::max()) {
            if (waitedMs >= timeoutMs) {
                TerminateProcess(processHandle, 1);
                error = "Component execution timed out.";
                return false;
            }
            const uint64_t remaining = timeoutMs - waitedMs;
            if (remaining < sliceMs) {
                sliceMs = static_cast<DWORD>(remaining);
            }
        }

        DWORD waitResult = WaitForSingleObject(processHandle, sliceMs);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult != WAIT_TIMEOUT) {
            error = "Failed while waiting for component process.";
            return false;
        }
        waitedMs += sliceMs;
    }

    if (!GetExitCodeProcess(processHandle, &exitCode)) {
        error = "Failed to read component process exit code.";
        return false;
    }
    return true;
}

bool ExecuteProcess(const std::filesystem::path& executablePath,
                    const std::string& args,
                    bool wait,
                    uint32_t timeoutSec,
                    const std::function<bool()>& cancellationCallback,
                    DWORD& exitCode,
                    std::string& error) {
    std::wstring executableW = executablePath.wstring();
    if (executableW.empty()) {
        error = "Component executable path is empty.";
        return false;
    }

    std::wstring argsW = Utf8ToWide(args);
    const bool isBatchScript = IsBatchScriptPath(executablePath);
    std::wstring commandLine = isBatchScript ? (L"cmd.exe /c " + QuoteProcessPath(executableW))
                                             : QuoteProcessPath(executableW);
    if (!argsW.empty()) {
        commandLine.append(L" ");
        commandLine.append(argsW);
    }

    std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back(L'\0');

    std::wstring workingDirectory;
    if (executablePath.has_parent_path()) {
        workingDirectory = executablePath.parent_path().wstring();
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    DWORD creationFlags = 0;
    if (isBatchScript) {
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;
        creationFlags = CREATE_NO_WINDOW;
    }
    PROCESS_INFORMATION processInfo{};

    BOOL started = CreateProcessW(nullptr,
                                  commandLineBuffer.data(),
                                  nullptr,
                                  nullptr,
                                  FALSE,
                                  creationFlags,
                                  nullptr,
                                  workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                  &startupInfo,
                                  &processInfo);
    if (!started) {
        error = "Failed to start component process.";
        return false;
    }

    CloseHandle(processInfo.hThread);
    processInfo.hThread = nullptr;

    if (!wait) {
        exitCode = 0;
        CloseHandle(processInfo.hProcess);
        return true;
    }

    bool ok = WaitForProcessExit(processInfo.hProcess,
                                 timeoutSec,
                                 cancellationCallback,
                                 exitCode,
                                 error);
    CloseHandle(processInfo.hProcess);
    return ok;
}

bool DownloadFile(const std::string& url,
                  const std::filesystem::path& targetPath,
                  std::string& error) {
    std::error_code ec;
    const auto parent = targetPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "Failed to create download directory: " + Utf8FromPath(parent);
            return false;
        }
    }

    std::wstring urlW = Utf8ToWide(url);
    std::wstring targetW = targetPath.wstring();
    HRESULT hr = URLDownloadToFileW(nullptr, urlW.c_str(), targetW.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "Download failed with HRESULT=0x" << std::hex
            << static_cast<unsigned long>(hr);
        error = oss.str();
        return false;
    }
    return true;
}

bool ComputeFileSha256(const std::filesystem::path& path,
                       std::string& hashHex,
                       std::string& error) {
    BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    std::vector<unsigned char> hashObject;
    std::vector<unsigned char> hashValue;

    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithmHandle,
                                                  BCRYPT_SHA256_ALGORITHM,
                                                  nullptr,
                                                  0);
    if (status < 0) {
        error = "Failed to initialize SHA256 algorithm provider.";
        return false;
    }

    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD resultLength = 0;
    status = BCryptGetProperty(algorithmHandle,
                               BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength),
                               sizeof(objectLength),
                               &resultLength,
                               0);
    if (status < 0) {
        error = "Failed to query SHA256 object length.";
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        return false;
    }

    status = BCryptGetProperty(algorithmHandle,
                               BCRYPT_HASH_LENGTH,
                               reinterpret_cast<PUCHAR>(&hashLength),
                               sizeof(hashLength),
                               &resultLength,
                               0);
    if (status < 0) {
        error = "Failed to query SHA256 hash length.";
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        return false;
    }

    hashObject.resize(objectLength);
    hashValue.resize(hashLength);
    status = BCryptCreateHash(algorithmHandle,
                              &hashHandle,
                              hashObject.data(),
                              static_cast<ULONG>(hashObject.size()),
                              nullptr,
                              0,
                              0);
    if (status < 0) {
        error = "Failed to create SHA256 hash object.";
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error = "Failed to open downloaded file for hash verification.";
        BCryptDestroyHash(hashHandle);
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        return false;
    }

    std::vector<char> buffer(1024 * 1024);
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize readSize = input.gcount();
        if (readSize <= 0) {
            continue;
        }
        status = BCryptHashData(hashHandle,
                                reinterpret_cast<PUCHAR>(buffer.data()),
                                static_cast<ULONG>(readSize),
                                0);
        if (status < 0) {
            error = "Failed to update SHA256 hash.";
            BCryptDestroyHash(hashHandle);
            BCryptCloseAlgorithmProvider(algorithmHandle, 0);
            return false;
        }
    }

    status = BCryptFinishHash(hashHandle,
                              hashValue.data(),
                              static_cast<ULONG>(hashValue.size()),
                              0);
    BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algorithmHandle, 0);
    if (status < 0) {
        error = "Failed to finalize SHA256 hash.";
        return false;
    }

    static const char* kHex = "0123456789abcdef";
    hashHex.clear();
    hashHex.reserve(hashValue.size() * 2);
    for (unsigned char b : hashValue) {
        hashHex.push_back(kHex[(b >> 4) & 0x0F]);
        hashHex.push_back(kHex[b & 0x0F]);
    }
    return true;
}
#endif

} // namespace

InstallServiceResult ExecuteInstallService(const ExtendedInstallationMetadata& metadata,
                                           MetadataParser& parser,
                                           InstallerPathResolver& pathResolver,
                                           const InstallServiceOptions& options,
                                           const InstallServiceCallbacks& callbacks) {
    InstallServiceResult result;
    HANDLE installMutex = nullptr;
    bool installStateApplied = false;
    InstallServiceStatus currentStatus = InstallServiceStatus::Preparing;
    InstallServicePhase currentPhase = InstallServicePhase::None;
    float currentPhaseProgress = 0.0f;
    float lastOverallProgress = 0.0f;

    auto calcOverall = [&](InstallServicePhase phase, float phaseProgress) {
        float overall = ToOverallProgress(phase, phaseProgress);
        if (overall < lastOverallProgress) {
            overall = lastOverallProgress;
        }
        overall = Clamp01(overall);
        lastOverallProgress = overall;
        return overall;
    };

    auto emitStatus = [&](InstallServiceStatus status,
                          InstallServicePhase phase,
                          float phaseProgress,
                          const std::string& message) {
        currentStatus = status;
        currentPhase = phase;
        currentPhaseProgress = Clamp01(phaseProgress);
        EmitStatus(callbacks,
                   currentStatus,
                   currentPhase,
                   currentPhaseProgress,
                   calcOverall(currentPhase, currentPhaseProgress),
                   message);
    };

    auto emitMessage = [&](InstallServiceEventType type, const std::string& message) {
        EmitMessage(callbacks,
                    type,
                    currentStatus,
                    currentPhase,
                    currentPhaseProgress,
                    calcOverall(currentPhase, currentPhaseProgress),
                    message);
    };

    auto emitProgress = [&](const std::string& folder,
                            const std::string& currentFile,
                            float phaseProgress) {
        currentPhaseProgress = std::max(currentPhaseProgress, Clamp01(phaseProgress));
        EmitProgress(callbacks,
                     currentStatus,
                     currentPhase,
                     folder,
                     currentFile,
                     currentPhaseProgress,
                     calcOverall(currentPhase, currentPhaseProgress));
    };

    auto releaseResources = [&]() {
        if (installMutex) {
            releaseInstallMutex(installMutex);
            installMutex = nullptr;
        }
    };

    auto markFailed = [&](const std::string& message, bool cancelled, bool appendMessage) {
        result.success = false;
        result.cancelled = cancelled;
        if (appendMessage && !message.empty()) {
            result.errors.push_back(message);
        }
        currentStatus = cancelled ? InstallServiceStatus::Cancelled : InstallServiceStatus::Failed;
        if (!message.empty()) {
            emitMessage(InstallServiceEventType::Error, message);
        }
        EmitStatus(callbacks,
                   currentStatus,
                   currentPhase,
                   currentPhaseProgress,
                   calcOverall(currentPhase, currentPhaseProgress),
                   cancelled ? "Installation cancelled." : "Installation failed.");
        if (installStateApplied) {
            applyInstallState(metadata.installState, "failed", pathResolver);
            installStateApplied = false;
        }
        releaseResources();
    };

    try {
        emitStatus(InstallServiceStatus::Preparing,
                   InstallServicePhase::None,
                   0.0f,
                   "Preparing installation...");

        if (IsCancellationRequested(options)) {
            markFailed("Installation cancelled.", true, true);
            return result;
        }

        currentStatus = InstallServiceStatus::Precheck;
        currentPhase = InstallServicePhase::Precheck;
        currentPhaseProgress = 0.0f;
        emitStatus(currentStatus, currentPhase, 0.0f, "Running installation prechecks...");

        const std::string effectiveAppId =
            resolveEffectiveAppId(metadata.appId, metadata.applicationName);
        const std::string effectiveDirectoryName =
            resolveEffectiveDirectoryName(metadata.directoryName, metadata.applicationName);
        bool installDirectoryAppendName = true;
        for (const auto& mapping : metadata.extendedMappings) {
            if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
                installDirectoryAppendName = mapping.appendDirectoryName;
                break;
            }
        }
        const std::vector<std::string> identityCandidates =
            buildIdentityCandidates(metadata.appId, metadata.legacyAppIds, metadata.applicationName);

        std::string previousManifest;
        std::string previousInstallDir;
        std::string matchedPreviousIdentity;
        const bool hasPreviousInstall =
            resolveExistingInstallInfo(identityCandidates,
                                       pathResolver,
                                       previousManifest,
                                       previousInstallDir,
                                       &matchedPreviousIdentity);

        std::string resolvedInstallRoot = pathResolver.resolveFinalPath(
            options.installPath,
            SpecialDirectoryType::INSTALL_DIRECTORY,
            effectiveDirectoryName,
            installDirectoryAppendName);
        if (hasPreviousInstall && !options.installPathExplicit && !previousInstallDir.empty()) {
            resolvedInstallRoot = previousInstallDir;
        }
        std::string diskCheckPath = resolvedInstallRoot.empty() ? options.installPath : resolvedInstallRoot;

        ComponentSelectionPlan componentPlan;
        std::string componentPlanError;
        if (!BuildComponentSelectionPlan(metadata, options, componentPlan, componentPlanError)) {
            markFailed(componentPlanError.empty() ? "Failed to resolve component selection."
                                                  : componentPlanError,
                       false,
                       true);
            return result;
        }

        std::vector<RegistryEntry> effectiveRegistry = metadata.registry;
        std::unordered_set<std::string> registrySeen;
        registrySeen.reserve(effectiveRegistry.size() + metadata.components.size() * 2);
        for (const auto& entry : effectiveRegistry) {
            std::string key = entry.path;
            key.push_back('\n');
            key += entry.key;
            key.push_back('\n');
            key += entry.value;
            key.push_back('\n');
            key += std::to_string(static_cast<int>(entry.type));
            registrySeen.insert(key);
        }

        std::vector<std::string> effectiveKillProcesses = metadata.installKillProcesses;
        std::unordered_set<std::string> processSeen;
        processSeen.reserve(effectiveKillProcesses.size() + componentPlan.killProcesses.size());
        for (const auto& process : effectiveKillProcesses) {
            processSeen.insert(process);
        }
        for (const auto& process : componentPlan.killProcesses) {
            AppendUniqueString(effectiveKillProcesses, processSeen, process);
        }

        bool effectiveAutoStartup = metadata.autoStartup;
        bool effectiveDesktopIcons = metadata.desktopIcons;

        auto shouldInstallMapping = [&](const ExtendedFolderMapping& mapping) {
            return ShouldInstallEmbeddedFolder(componentPlan, mapping);
        };

        std::vector<std::string> selectedEmbeddedFolders;
        selectedEmbeddedFolders.reserve(componentPlan.embeddedFolders.size());
        uint64_t totalInstallBytes = 0;
        for (const auto& mapping : metadata.extendedMappings) {
            if (!shouldInstallMapping(mapping)) {
                continue;
            }
            selectedEmbeddedFolders.push_back(mapping.folderName);
            totalInstallBytes += mapping.originalSize;
        }

        if (componentPlan.hasComponents) {
            std::string selectedSummary = "Selected components:";
            if (componentPlan.ordered.empty()) {
                selectedSummary += " (none)";
            } else {
                for (const auto* component : componentPlan.ordered) {
                    selectedSummary += " ";
                    selectedSummary += component->id;
                }
            }
            emitMessage(InstallServiceEventType::Info, selectedSummary);
        }

        uint64_t availableBytes = 0;
        if (!checkDiskSpaceForInstall(diskCheckPath, totalInstallBytes, availableBytes)) {
            markFailed("Insufficient disk space for installation. required=" +
                           std::to_string(totalInstallBytes) + " available=" +
                           std::to_string(availableBytes),
                       false,
                       true);
            return result;
        }
        emitProgress("", "Disk space precheck", 0.25f);

#ifdef _WIN32
        uint16_t currentMajor = 0;
        uint16_t currentMinor = 0;
        uint32_t currentBuild = 0;
        if (!checkMinimumWindowsVersion(metadata.minWindowsMajor,
                                        metadata.minWindowsMinor,
                                        metadata.minWindowsBuild,
                                        currentMajor,
                                        currentMinor,
                                        currentBuild)) {
            markFailed("Windows version does not meet minimum requirement.", false, true);
            return result;
        }
#endif
        emitProgress("", "OS version precheck", 0.40f);

#ifdef _WIN32
        std::vector<std::string> processNames = buildKillProcessList(
            metadata.applicationName,
            effectiveKillProcesses);
        if (!processNames.empty()) {
            std::vector<std::string> running = getRunningProcessesByName(processNames);
            if (!running.empty()) {
                std::string joined;
                for (size_t i = 0; i < running.size(); ++i) {
                    if (i > 0) {
                        joined += ", ";
                    }
                    joined += running[i];
                }
                emitMessage(InstallServiceEventType::Info, "Terminating processes: " + joined);
                terminateProcessesByName(running);
                Sleep(500);
                std::vector<std::string> remaining = getRunningProcessesByName(processNames);
                if (!remaining.empty()) {
                    std::string unresolved;
                    for (size_t i = 0; i < remaining.size(); ++i) {
                        if (i > 0) {
                            unresolved += ", ";
                        }
                        unresolved += remaining[i];
                    }
                    markFailed("Failed to terminate processes: " + unresolved, false, true);
                    return result;
                }
            }
        }
#endif
        emitProgress("", "Process precheck", 0.60f);

        if (IsCancellationRequested(options)) {
            markFailed("Installation cancelled.", true, true);
            return result;
        }

        if (hasPreviousInstall) {
            std::string normalizedOld = normalizePathForCompare(previousInstallDir);
            std::string normalizedNew = normalizePathForCompare(
                resolvedInstallRoot.empty() ? options.installPath : resolvedInstallRoot);
            if (!normalizedOld.empty() && !normalizedNew.empty() && normalizedOld != normalizedNew) {
                emitMessage(InstallServiceEventType::Info,
                            "Detected previous install at: " + previousInstallDir);
                if (previousManifest.empty()) {
                    emitMessage(InstallServiceEventType::Warning,
                                "Old install manifest not found; skipping cleanup.");
                } else if (metadata.autoCleanOldInstall || options.cleanupOldInstallRequested) {
                    currentPhase = InstallServicePhase::CleanupOldInstall;
                    currentPhaseProgress = 0.0f;
                    emitStatus(InstallServiceStatus::Precheck,
                               currentPhase,
                               0.0f,
                               "Cleaning previous installation...");

                    ConsoleInterface console;
                    auto cleanupProgress = [&](const UninstallProgressInfo& info) {
                        const std::string detail = info.currentItem.empty()
                                                       ? std::string("Cleaning previous installation")
                                                       : info.currentItem;
                        emitProgress("cleanup", detail, info.progress);
                    };

                    if (!uninstallFromManifest(previousManifest,
                                               pathResolver,
                                               console,
                                               cleanupProgress,
                                               options.cancellationCallback)) {
                        if (IsCancellationRequested(options)) {
                            markFailed("Installation cancelled.", true, true);
                            return result;
                        }
                        emitMessage(InstallServiceEventType::Warning,
                                    "Previous install cleanup reported failure.");
                    }
                    emitProgress("cleanup", "Previous installation cleanup finished", 1.0f);
                } else {
                    emitMessage(InstallServiceEventType::Info,
                                "Skipping cleanup of previous installation.");
                }
            }
        }

        currentPhase = InstallServicePhase::Precheck;
        currentPhaseProgress = 0.85f;

        if (metadata.installState.useMutex) {
            emitMessage(InstallServiceEventType::Info, "Acquiring install mutex...");
            installMutex = acquireInstallMutex(metadata.installState);
        }
        emitProgress("", "Precheck completed", 1.0f);

        applyInstallState(metadata.installState, "installing", pathResolver);
        installStateApplied = true;

        currentStatus = InstallServiceStatus::Installing;
        currentPhase = InstallServicePhase::Installing;
        currentPhaseProgress = 0.0f;
        emitStatus(currentStatus, currentPhase, 0.0f, "Installing files...");

        size_t executableComponentCount = 0;
        for (const auto* component : componentPlan.ordered) {
            if (!component) {
                continue;
            }
            if (component->source.type == ComponentSourceType::LOCAL ||
                component->source.type == ComponentSourceType::DOWNLOAD) {
                ++executableComponentCount;
            }
        }
        const float extractionWeight = executableComponentCount > 0 ? 0.75f : 1.0f;

        std::unordered_map<std::string, uint64_t> folderSizes;
        std::unordered_map<std::string, float> folderProgress;
        folderSizes.reserve(selectedEmbeddedFolders.size());
        folderProgress.reserve(selectedEmbeddedFolders.size());
        for (const auto& mapping : metadata.extendedMappings) {
            if (!shouldInstallMapping(mapping)) {
                continue;
            }
            folderSizes[mapping.folderName] = mapping.originalSize;
            folderProgress[mapping.folderName] = 0.0f;
        }
        std::mutex installProgressMutex;

        ProgressCallback progressCallback = [&](const std::string& folder,
                                                const std::string& currentFile,
                                                float progress) {
            float phaseProgress = Clamp01(progress);
            if (totalInstallBytes > 0) {
                std::lock_guard<std::mutex> lock(installProgressMutex);
                folderProgress[folder] = Clamp01(progress);
                double completed = 0.0;
                for (const auto& entry : folderProgress) {
                    auto sizeIt = folderSizes.find(entry.first);
                    if (sizeIt != folderSizes.end()) {
                        completed += static_cast<double>(sizeIt->second) * static_cast<double>(entry.second);
                    }
                }
                phaseProgress = static_cast<float>(completed / static_cast<double>(totalInstallBytes));
            }
            emitProgress(folder, currentFile, phaseProgress * extractionWeight);
        };

        LogCallback infoCallback = [&](const std::string& message) {
            emitMessage(InstallServiceEventType::Info, message);
        };
        LogCallback errorCallback = [&](const std::string& message) {
            emitMessage(InstallServiceEventType::Error, message);
        };

        ParallelInstallResult parallelResult;
        if (selectedEmbeddedFolders.empty() && componentPlan.hasComponents) {
            parallelResult.success = true;
            parallelResult.installRootPath = resolvedInstallRoot;
            emitMessage(InstallServiceEventType::Info,
                        "No embedded folders selected; skipping package extraction.");
        } else {
            parallelResult = RunParallelInstall(
                metadata,
                parser,
                pathResolver,
                options.installPath,
                options.folderMappings,
                selectedEmbeddedFolders,
                componentPlan.hasComponents,
                options.threadCount,
                progressCallback,
                infoCallback,
                errorCallback,
                options.cancellationCallback);
        }

        result.timing = parallelResult.timing;
        result.installRootPath = parallelResult.installRootPath;
        result.installedRoots = std::move(parallelResult.installedRoots);
        result.cancelled = parallelResult.cancelled;

        if (!parallelResult.success) {
            result.errors = std::move(parallelResult.errors);
            if (result.cancelled && result.errors.empty()) {
                result.errors.push_back("Installation cancelled.");
            }
            if (result.errors.empty()) {
                result.errors.push_back("Installation failed.");
            }
            for (const auto& error : result.errors) {
                emitMessage(InstallServiceEventType::Error, error);
            }
            markFailed(std::string(), result.cancelled, false);
            return result;
        }

        if (result.installRootPath.empty()) {
            result.installRootPath = resolvedInstallRoot.empty() ? options.installPath : resolvedInstallRoot;
        }

        emitProgress("", "File installation completed", extractionWeight);

        std::vector<ComponentExecutionRecord> componentActions;
        componentActions.reserve(executableComponentCount);
        std::unordered_set<std::string> failedOptionalComponentIds;
        failedOptionalComponentIds.reserve(executableComponentCount);

        if (executableComponentCount > 0) {
            const std::string installRootForComponents = result.installRootPath.empty()
                                                             ? diskCheckPath
                                                             : result.installRootPath;
            size_t completedComponents = 0;

            auto advanceComponentProgress = [&](const std::string& id, float offset) {
                float progress = extractionWeight +
                                 ((static_cast<float>(completedComponents) + offset) /
                                  static_cast<float>(executableComponentCount)) *
                                     (1.0f - extractionWeight);
                emitProgress("component", id, progress);
            };

            for (const auto* component : componentPlan.ordered) {
                if (!component) {
                    continue;
                }
                if (component->source.type != ComponentSourceType::LOCAL &&
                    component->source.type != ComponentSourceType::DOWNLOAD) {
                    continue;
                }

                if (IsCancellationRequested(options)) {
                    markFailed("Installation cancelled.", true, true);
                    return result;
                }

                advanceComponentProgress(component->id, 0.0f);
                emitMessage(InstallServiceEventType::Info,
                            "Installing component: " + component->id);

                std::string componentError;

                if (component->source.type == ComponentSourceType::LOCAL) {
#ifdef _WIN32
                    const std::string baseUtf8 =
                        ExpandRuntimeTokens(component->source.local.base,
                                            installRootForComponents,
                                            metadata,
                                            pathResolver);
                    std::filesystem::path basePath = PathFromUtf8(baseUtf8);
                    std::filesystem::path installerPath =
                        basePath / PathFromUtf8(component->source.local.installer);
                    installerPath = installerPath.lexically_normal();
                    const std::string componentInstallDir =
                        ExpandRuntimeTokens(component->source.local.base,
                                            installRootForComponents,
                                            metadata,
                                            pathResolver);
                    const std::string expandedArgs =
                        ExpandRuntimeTokens(component->source.local.args,
                                            installRootForComponents,
                                            metadata,
                                            pathResolver,
                                            componentInstallDir);

                    if (!IsPathUnderBase(basePath, installerPath)) {
                        componentError = "Local installer path escapes component base directory.";
                    } else if (!std::filesystem::exists(installerPath)) {
                        componentError = "Local installer not found: " + Utf8FromPath(installerPath);
                    } else {
                        DWORD exitCode = 0;
                        std::string executeError;
                        if (!ExecuteProcess(installerPath,
                                            expandedArgs,
                                            component->source.local.wait,
                                            component->source.local.timeoutSec,
                                            options.cancellationCallback,
                                            exitCode,
                                            executeError)) {
                            componentError = executeError.empty() ? "Failed to execute local component installer."
                                                                  : executeError;
                        } else if (component->source.local.wait && exitCode != 0) {
                            componentError = "Local component installer failed with exit code " +
                                             std::to_string(exitCode);
                        } else if (!component->source.local.uninstall.empty()) {
                            ComponentExecutionRecord record;
                            record.componentId = component->id;
                            record.sourceType = "local";
                            record.uninstallCommand =
                                ExpandRuntimeTokens(component->source.local.uninstall,
                                                    installRootForComponents,
                                                    metadata,
                                                    pathResolver,
                                                    componentInstallDir);
                            record.workingDirectory = Utf8FromPath(basePath);
                            record.wait = component->source.local.wait;
                            record.timeoutSec = component->source.local.timeoutSec;
                            componentActions.push_back(std::move(record));
                        }
                    }
#else
                    componentError = "Local component installers are currently supported on Windows only.";
#endif
                } else if (component->source.type == ComponentSourceType::DOWNLOAD) {
#ifdef _WIN32
                    std::string saveAs = component->source.download.saveAs;
                    if (saveAs.empty()) {
                        saveAs = "%InstallDir%\\downloads\\" + component->id + "_setup.bin";
                    }
                    const std::string componentInstallDir;
                    std::string targetUtf8 = ExpandRuntimeTokens(saveAs,
                                                                 installRootForComponents,
                                                                 metadata,
                                                                 pathResolver,
                                                                 componentInstallDir);
                    std::filesystem::path targetPath = PathFromUtf8(targetUtf8);
                    if (!targetPath.is_absolute()) {
                        targetPath = PathFromUtf8(installRootForComponents) / targetPath;
                    }
                    targetPath = targetPath.lexically_normal();

                    const std::string downloadUrl =
                        ExpandRuntimeTokens(component->source.download.url,
                                            installRootForComponents,
                                            metadata,
                                            pathResolver,
                                            componentInstallDir);
                    const std::string expandedArgs =
                        ExpandRuntimeTokens(component->source.download.args,
                                            installRootForComponents,
                                            metadata,
                                            pathResolver,
                                            componentInstallDir);
                    if (!IsPathUnderBase(PathFromUtf8(installRootForComponents), targetPath)) {
                        componentError = "Downloaded installer target path must stay under install directory.";
                    } else {
                        std::string downloadError;
                        if (!DownloadFile(downloadUrl, targetPath, downloadError)) {
                            componentError = downloadError.empty() ? "Failed to download component installer."
                                                                   : downloadError;
                        } else {
                            std::string actualHash;
                            std::string hashError;
                            if (!ComputeFileSha256(targetPath, actualHash, hashError)) {
                                componentError = hashError.empty() ? "Failed to verify downloaded component hash."
                                                                   : hashError;
                            } else {
                                std::string expectedHash = component->source.download.sha256;
                                std::transform(expectedHash.begin(), expectedHash.end(), expectedHash.begin(),
                                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                                if (actualHash != expectedHash) {
                                    componentError = "Downloaded component hash mismatch.";
                                    std::error_code removeEc;
                                    std::filesystem::remove(targetPath, removeEc);
                                } else {
                                    DWORD exitCode = 0;
                                    std::string executeError;
                                    if (!ExecuteProcess(targetPath,
                                                        expandedArgs,
                                                        component->source.download.wait,
                                                        component->source.download.timeoutSec,
                                                        options.cancellationCallback,
                                                        exitCode,
                                                        executeError)) {
                                        componentError = executeError.empty()
                                                             ? "Failed to execute downloaded component installer."
                                                             : executeError;
                                    } else if (component->source.download.wait && exitCode != 0) {
                                        componentError =
                                            "Downloaded component installer failed with exit code " +
                                            std::to_string(exitCode);
                                    } else if (!component->source.download.uninstall.empty()) {
                                        ComponentExecutionRecord record;
                                        record.componentId = component->id;
                                        record.sourceType = "download";
                                        record.uninstallCommand =
                                            ExpandRuntimeTokens(component->source.download.uninstall,
                                                                installRootForComponents,
                                                                metadata,
                                                                pathResolver,
                                                                componentInstallDir);
                                        record.workingDirectory = Utf8FromPath(targetPath.parent_path());
                                        record.wait = component->source.download.wait;
                                        record.timeoutSec = component->source.download.timeoutSec;
                                        componentActions.push_back(std::move(record));
                                    }
                                }
                            }
                        }
                    }
#else
                    componentError = "Downloaded component installers are currently supported on Windows only.";
#endif
                }

                if (!componentError.empty()) {
                    std::string failureMessage =
                        "Component '" + component->id + "' failed: " + componentError;
                    if (component->required) {
                        markFailed(failureMessage, IsCancellationRequested(options), true);
                        return result;
                    }

                    failedOptionalComponentIds.insert(component->id);
                    emitMessage(InstallServiceEventType::Warning,
                                failureMessage + " Continuing because component is optional.");
                    ++completedComponents;
                    advanceComponentProgress(component->id, 0.0f);
                    continue;
                }

                std::string successMessage;
                if (component->source.type == ComponentSourceType::LOCAL &&
                    !component->source.local.wait) {
                    successMessage = "Component '" + component->id +
                                     "' installer launched (not waiting for completion).";
                } else if (component->source.type == ComponentSourceType::DOWNLOAD &&
                           !component->source.download.wait) {
                    successMessage = "Component '" + component->id +
                                     "' installer launched (not waiting for completion).";
                } else {
                    successMessage = "Component '" + component->id + "' installed successfully.";
                }
                emitMessage(InstallServiceEventType::Info, successMessage);

                ++completedComponents;
                advanceComponentProgress(component->id, 0.0f);
            }
        }

        for (const auto* component : componentPlan.ordered) {
            if (!component) {
                continue;
            }
            if (failedOptionalComponentIds.find(component->id) != failedOptionalComponentIds.end()) {
                continue;
            }
            for (const auto& entry : component->registry) {
                AppendUniqueRegistry(effectiveRegistry, registrySeen, entry);
            }
            effectiveAutoStartup = effectiveAutoStartup || component->autoStartup;
            effectiveDesktopIcons = effectiveDesktopIcons || component->createDesktopShortcut;
        }

        if (!failedOptionalComponentIds.empty()) {
            std::string message = "Optional components failed:";
            for (const auto& id : failedOptionalComponentIds) {
                message += " ";
                message += id;
            }
            emitMessage(InstallServiceEventType::Warning, message);
        }

        currentStatus = InstallServiceStatus::Finalizing;
        currentPhase = InstallServicePhase::Finalizing;
        currentPhaseProgress = 0.0f;
        emitStatus(currentStatus, currentPhase, 0.0f, "Finalizing installation...");

        auto advanceFinalize = [&](float progress, const std::string& detail) {
            emitProgress("finalize", detail, progress);
        };

        if (options.applyRegistryBeforeFinalize && !effectiveRegistry.empty()) {
            std::string prePath = options.preRegistryInstallPath.empty()
                                      ? options.installPath
                                      : options.preRegistryInstallPath;
            applyRegistryEntries(effectiveRegistry,
                                 prePath,
                                 metadata.configVersion,
                                 metadata.applicationName);
        }
        advanceFinalize(0.15f, "Applying registry entries");

        if ((effectiveAutoStartup || effectiveDesktopIcons) && result.installRootPath.empty()) {
            emitMessage(InstallServiceEventType::Warning,
                        "Install root not detected; AutoStartup/DesktopIcons skipped");
        }

        if (!result.installRootPath.empty()) {
            std::filesystem::path exePath = findPrimaryExecutable(PathFromUtf8(result.installRootPath),
                                                                  metadata.applicationName);
            if ((effectiveAutoStartup || effectiveDesktopIcons) && exePath.empty()) {
                emitMessage(InstallServiceEventType::Warning,
                            "No executable found for AutoStartup/DesktopIcons");
            } else {
                if (effectiveAutoStartup) {
                    if (setAutoStartup(metadata.applicationName, exePath)) {
                        emitMessage(InstallServiceEventType::Info, "AutoStartup enabled");
                    } else {
                        emitMessage(InstallServiceEventType::Warning,
                                    "Failed to enable AutoStartup");
                    }
                }
                if (effectiveDesktopIcons) {
                    if (createDesktopShortcut(metadata.applicationName, exePath)) {
                        emitMessage(InstallServiceEventType::Info, "Desktop icon created");
                    } else {
                        emitMessage(InstallServiceEventType::Warning,
                                    "Failed to create desktop icon");
                    }
                }
            }
        }
        advanceFinalize(0.35f, "Creating startup and shortcut entries");

        result.installedFiles = CollectFilesRecursive(result.installedRoots);

        if (!result.installRootPath.empty()) {
            std::filesystem::path target = PathFromUtf8(result.installRootPath) / "uninstall.exe";
            std::string currentExe = getCurrentExecutablePath();
            std::filesystem::path currentExePath = PathFromUtf8(currentExe);
            std::error_code ec;
            if (!currentExe.empty() && std::filesystem::exists(currentExePath)) {
                std::string targetUtf8 = Utf8FromPath(target);
                if (createUninstallStub(currentExe, targetUtf8)) {
                    result.uninstallPath = targetUtf8;
                } else {
                    std::filesystem::copy_file(currentExePath, target,
                                               std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec) {
                        emitMessage(InstallServiceEventType::Warning,
                                    "Failed to create uninstall.exe");
                    } else {
                        result.uninstallPath = targetUtf8;
                    }
                }
            }
        }
        advanceFinalize(0.50f, "Preparing uninstall entry point");

        if (!result.uninstallPath.empty()) {
            result.installedFiles.erase(
                std::remove(result.installedFiles.begin(), result.installedFiles.end(), result.uninstallPath),
                result.installedFiles.end());
        }

        std::string languageCode = ResolveLanguageCode(options.languageCode);
        if (!result.installRootPath.empty()) {
            std::filesystem::path localPath = PathFromUtf8(result.installRootPath) / "install.manifest.json";
            if (!writeManifest(Utf8FromPath(localPath),
                               effectiveAppId,
                               metadata.applicationName,
                               metadata.legacyAppIds,
                               metadata.configVersion,
                               result.installRootPath,
                               result.installedRoots,
                               metadata.uninstallCleanupRules,
                               result.installedFiles,
                               effectiveRegistry,
                               effectiveKillProcesses,
                               effectiveAutoStartup,
                               effectiveDesktopIcons,
                               metadata.installState,
                               result.uninstallPath,
                               languageCode,
                               componentActions)) {
                emitMessage(InstallServiceEventType::Warning,
                            "Failed to write local install manifest");
            }
        }
        advanceFinalize(0.75f, "Writing install manifest");

        if (options.applyRegistryAfterInstall && !effectiveRegistry.empty()) {
            applyRegistryEntries(effectiveRegistry,
                                 result.installRootPath,
                                 metadata.configVersion,
                                 metadata.applicationName);
        }

#ifdef _WIN32
        if (options.writeUninstallRegistry && !result.uninstallPath.empty()) {
            bool perMachine = isRunningAsAdmin();
            if (!writeUninstallRegistryEntry(effectiveAppId,
                                             metadata.configVersion,
                                             result.installRootPath,
                                             result.uninstallPath,
                                             perMachine)) {
                emitMessage(InstallServiceEventType::Warning,
                            "Failed to write uninstall registry entry");
            }
            for (const auto& legacyId : metadata.legacyAppIds) {
                deleteUninstallRegistryEntry(legacyId, perMachine);
                deleteUninstallRegistryEntry(legacyId, !perMachine);
            }
            if (!metadata.applicationName.empty() &&
                normalizePathForCompare(metadata.applicationName) !=
                    normalizePathForCompare(effectiveAppId)) {
                deleteUninstallRegistryEntry(metadata.applicationName, perMachine);
                deleteUninstallRegistryEntry(metadata.applicationName, !perMachine);
            }
        }
#endif
        advanceFinalize(0.90f, "Writing uninstall registry");

        applyInstallState(metadata.installState, "installed", pathResolver);
        installStateApplied = false;
        releaseResources();

        advanceFinalize(1.0f, "Finalization complete");

        result.success = true;
        currentStatus = InstallServiceStatus::Completed;
        EmitStatus(callbacks,
                   currentStatus,
                   InstallServicePhase::Finalizing,
                   1.0f,
                   calcOverall(InstallServicePhase::Finalizing, 1.0f),
                   "Installation completed.");
        return result;
    } catch (const std::exception& ex) {
        markFailed(ex.what(), IsCancellationRequested(options), true);
        return result;
    } catch (...) {
        markFailed("Unknown installation error.", IsCancellationRequested(options), true);
        return result;
    }
}

} // namespace MultiThreadedInstaller
