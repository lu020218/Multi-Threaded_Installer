#include "installer/install_execution.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/component_launcher.h"
#include "installer/folder_payload_reader.h"
#include "installer/installer_helpers.h"
#include "installer/metadata_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

float Clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

std::string ResolveComponentDisplayName(const ComponentConfig& component) {
    return component.name.empty() ? component.id : component.name;
}

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
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
        { "%AppVersion%", metadata.appVersion },
        { "%AppId%", resolveEffectiveAppId(metadata.appId, metadata.appName) },
        { "%appDirectoryName%", metadata.appDirectoryName },
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
std::wstring ExtractArgValueFromCommandLine(const std::string& argsUtf8, const std::wstring& flag) {
    if (argsUtf8.empty()) {
        return {};
    }

    std::wstring synthetic = L"placeholder.exe ";
    synthetic += Utf8ToWide(argsUtf8);
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(synthetic.c_str(), &argc);
    if (!argvW) {
        return {};
    }

    std::wstring value;
    for (int i = 1; i + 1 < argc; ++i) {
        std::wstring current = argvW[i] ? argvW[i] : L"";
        std::transform(current.begin(), current.end(), current.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (current == flag) {
            value = argvW[i + 1] ? argvW[i + 1] : L"";
            break;
        }
    }
    LocalFree(argvW);
    return value;
}

bool IsPostSetupAgentComponent(const ComponentConfig& component) {
    std::wstring installer = Utf8ToWide(component.source.local.installer);
    std::transform(installer.begin(), installer.end(), installer.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return installer.find(L"post_setup_agent.exe") != std::wstring::npos;
}

std::string QuoteForCommand(const std::string& value) {
    return "\"" + value + "\"";
}

HANDLE CreateComponentJobObject() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        logInstallerWarning("[ComponentInstall] Failed to create job object for component process tree.");
        return nullptr;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job,
                                 JobObjectExtendedLimitInformation,
                                 &limits,
                                 sizeof(limits))) {
        logInstallerWarning("[ComponentInstall] Failed to configure component job object.");
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

bool AssignComponentProcessToJob(HANDLE jobHandle, HANDLE processHandle) {
    if (!jobHandle || !processHandle) {
        return false;
    }
    if (AssignProcessToJobObject(jobHandle, processHandle)) {
        return true;
    }
    logInstallerWarning("[ComponentInstall] Failed to assign component process to job object; "
                        "falling back to single-process termination on timeout/cancel.");
    return false;
}

void TerminateComponentProcessTree(HANDLE jobHandle, HANDLE processHandle) {
    if (jobHandle && TerminateJobObject(jobHandle, 1)) {
        return;
    }
    if (jobHandle) {
        logInstallerWarning("[ComponentInstall] Failed to terminate component job object; "
                            "falling back to launcher process termination.");
    }
    if (processHandle) {
        TerminateProcess(processHandle, 1);
    }
}

bool WaitForProcessExit(HANDLE processHandle,
                        HANDLE jobHandle,
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
            TerminateComponentProcessTree(jobHandle, processHandle);
            WaitForSingleObject(processHandle, 2000);
            error = "Component execution cancelled.";
            return false;
        }

        DWORD sliceMs = 200;
        if (timeoutMs != std::numeric_limits<uint64_t>::max()) {
            if (waitedMs >= timeoutMs) {
                TerminateComponentProcessTree(jobHandle, processHandle);
                WaitForSingleObject(processHandle, 2000);
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
                    bool showWindow,
                    bool showWindowConfigured,
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

    const ComponentLaunchCommand launchCommand =
        BuildComponentLaunchCommand(executablePath, args);

    std::vector<wchar_t> commandLineBuffer(launchCommand.commandLine.begin(),
                                           launchCommand.commandLine.end());
    commandLineBuffer.push_back(L'\0');

    std::wstring workingDirectory;
    if (executablePath.has_parent_path()) {
        workingDirectory = executablePath.parent_path().wstring();
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    DWORD creationFlags = 0;
    if (showWindowConfigured) {
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = showWindow ? SW_SHOWNORMAL : SW_HIDE;
        if (!showWindow) {
            creationFlags = CREATE_NO_WINDOW;
        }
    } else if (launchCommand.hideByDefault) {
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;
        creationFlags = CREATE_NO_WINDOW;
    }
    PROCESS_INFORMATION processInfo{};
    HANDLE jobHandle = wait ? CreateComponentJobObject() : nullptr;

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
        if (jobHandle) {
            CloseHandle(jobHandle);
        }
        error = launchCommand.startFailureMessage;
        return false;
    }

    const bool assignedToJob = AssignComponentProcessToJob(jobHandle, processInfo.hProcess);
    if (jobHandle && !assignedToJob) {
        CloseHandle(jobHandle);
        jobHandle = nullptr;
    }

    CloseHandle(processInfo.hThread);
    processInfo.hThread = nullptr;

    if (!wait) {
        exitCode = 0;
        CloseHandle(processInfo.hProcess);
        return true;
    }

    bool ok = WaitForProcessExit(processInfo.hProcess,
                                 jobHandle,
                                 timeoutSec,
                                 cancellationCallback,
                                 exitCode,
                                 error);
    CloseHandle(processInfo.hProcess);
    if (jobHandle) {
        CloseHandle(jobHandle);
    }
    return ok;
}

bool DownloadFileToPath(const std::string& url,
                        const std::filesystem::path& targetPath,
                        std::string& error) {
    HRESULT hr = URLDownloadToFileW(nullptr,
                                    Utf8ToWide(url).c_str(),
                                    targetPath.wstring().c_str(),
                                    0,
                                    nullptr);
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << "Failed to download component installer. HRESULT=0x"
            << std::hex << static_cast<unsigned long>(hr);
        error = oss.str();
        return false;
    }
    return true;
}

bool ComputeFileSha256(const std::filesystem::path& filePath,
                       std::string& sha256,
                       std::string& error) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD bytesRead = 0;

    auto cleanup = [&]() {
        if (hashHandle) {
            BCryptDestroyHash(hashHandle);
            hashHandle = nullptr;
        }
        if (algorithm) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
        }
    };

    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        error = "Failed to open SHA256 provider.";
        return false;
    }
    status = BCryptGetProperty(algorithm,
                               BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength),
                               sizeof(objectLength),
                               &bytesRead,
                               0);
    if (status < 0) {
        cleanup();
        error = "Failed to read SHA256 object length.";
        return false;
    }
    status = BCryptGetProperty(algorithm,
                               BCRYPT_HASH_LENGTH,
                               reinterpret_cast<PUCHAR>(&hashLength),
                               sizeof(hashLength),
                               &bytesRead,
                               0);
    if (status < 0 || hashLength == 0) {
        cleanup();
        error = "Failed to read SHA256 hash length.";
        return false;
    }

    std::vector<unsigned char> hashObject(objectLength);
    std::vector<unsigned char> hash(hashLength);
    status = BCryptCreateHash(algorithm,
                              &hashHandle,
                              hashObject.data(),
                              objectLength,
                              nullptr,
                              0,
                              0);
    if (status < 0) {
        cleanup();
        error = "Failed to create SHA256 hash.";
        return false;
    }

    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        cleanup();
        error = "Failed to open downloaded component installer for hashing.";
        return false;
    }
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0) {
            continue;
        }
        status = BCryptHashData(hashHandle,
                                reinterpret_cast<PUCHAR>(buffer.data()),
                                static_cast<ULONG>(count),
                                0);
        if (status < 0) {
            cleanup();
            error = "Failed to hash downloaded component installer.";
            return false;
        }
    }

    status = BCryptFinishHash(hashHandle, hash.data(), hashLength, 0);
    if (status < 0) {
        cleanup();
        error = "Failed to finish SHA256 hash.";
        return false;
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char byte : hash) {
        oss << std::setw(2) << static_cast<unsigned int>(byte);
    }
    sha256 = oss.str();
    cleanup();
    return true;
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

#endif

bool ExecuteLocalComponent(const ComponentConfig& component,
                           const std::string& installRootForComponents,
                           const ExtendedInstallationMetadata& metadata,
                           InstallerPathResolver& pathResolver,
                           const InstallServiceOptions& options,
                           std::vector<ComponentExecutionRecord>& componentActions,
                           std::string& componentError) {
#ifdef _WIN32
    const std::string baseUtf8 =
        ExpandRuntimeTokens(component.source.local.base,
                            installRootForComponents,
                            metadata,
                            pathResolver);
    std::filesystem::path basePath = PathFromUtf8(baseUtf8);
    std::filesystem::path installerPath =
        basePath / PathFromUtf8(component.source.local.installer);
    installerPath = installerPath.lexically_normal();
    const std::string componentInstallDir =
        ExpandRuntimeTokens(component.source.local.base,
                            installRootForComponents,
                            metadata,
                            pathResolver);
    const std::string expandedArgs =
        ExpandRuntimeTokens(component.source.local.args,
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
                            component.source.local.showWindow,
                            component.source.local.showWindowConfigured,
                            component.source.local.wait,
                            component.source.local.timeoutSec,
                            options.cancellationCallback,
                            exitCode,
                            executeError)) {
            componentError = executeError.empty() ? "Failed to execute local component installer."
                                                  : executeError;
        } else if (component.source.local.wait && exitCode != 0) {
            componentError = "Local component installer failed with exit code " +
                             std::to_string(exitCode);
        } else if (!component.source.local.uninstall.empty() || IsPostSetupAgentComponent(component)) {
            ComponentExecutionRecord record;
            record.componentId = component.id;
            record.sourceType = "local";
            if (!component.source.local.uninstall.empty()) {
                record.uninstallCommand =
                    ExpandRuntimeTokens(component.source.local.uninstall,
                                        installRootForComponents,
                                        metadata,
                                        pathResolver,
                                        componentInstallDir);
            } else {
                const std::wstring statePathW =
                    ExtractArgValueFromCommandLine(expandedArgs, L"--state-path");
                const std::wstring logPathW =
                    ExtractArgValueFromCommandLine(expandedArgs, L"--log-path");
                if (!statePathW.empty()) {
                    record.uninstallCommand =
                        QuoteForCommand(Utf8FromPath(installerPath)) + " --uninstall --state-path " +
                        QuoteForCommand(WideToUtf8(statePathW));
                    if (!logPathW.empty()) {
                        record.uninstallCommand +=
                            " --log-path " + QuoteForCommand(WideToUtf8(logPathW));
                    }
                }
            }
            record.workingDirectory = Utf8FromPath(basePath);
            record.wait = component.source.local.wait;
            record.timeoutSec = component.source.local.timeoutSec;
            if (!record.uninstallCommand.empty()) {
                componentActions.push_back(std::move(record));
            }
        }
    }
#else
    (void)component;
    (void)installRootForComponents;
    (void)metadata;
    (void)pathResolver;
    (void)options;
    (void)componentActions;
    componentError = "Local component installers are currently supported on Windows only.";
#endif
    return componentError.empty();
}

bool ExecuteDownloadComponent(const ComponentConfig& component,
                              const std::string& installRootForComponents,
                              const ExtendedInstallationMetadata& metadata,
                              InstallerPathResolver& pathResolver,
                              const InstallServiceOptions& options,
                              std::vector<ComponentExecutionRecord>& componentActions,
                              std::string& componentError) {
#ifdef _WIN32
    const std::string saveAsUtf8 =
        ExpandRuntimeTokens(component.source.download.saveAs,
                            installRootForComponents,
                            metadata,
                            pathResolver);
    std::filesystem::path installRoot = PathFromUtf8(installRootForComponents);
    std::filesystem::path targetPath = PathFromUtf8(saveAsUtf8).lexically_normal();
    if (targetPath.empty()) {
        componentError = "Downloaded component installer save path is empty.";
        return false;
    }
    if (!IsPathUnderBase(installRoot, targetPath)) {
        componentError = "Downloaded component installer save path escapes install directory.";
        return false;
    }

    std::error_code ec;
    if (targetPath.has_parent_path()) {
        std::filesystem::create_directories(targetPath.parent_path(), ec);
        if (ec) {
            componentError = "Failed to create download target directory: " + ec.message();
            return false;
        }
    }

    const std::string downloadUrl =
        ExpandRuntimeTokens(component.source.download.url,
                            installRootForComponents,
                            metadata,
                            pathResolver);
    std::string error;
    if (!DownloadFileToPath(downloadUrl, targetPath, error)) {
        componentError = error;
        return false;
    }

    if (!component.source.download.sha256.empty()) {
        std::string actualSha256;
        if (!ComputeFileSha256(targetPath, actualSha256, error)) {
            std::filesystem::remove(targetPath, ec);
            componentError = error;
            return false;
        }
        if (ToLowerAscii(actualSha256) != ToLowerAscii(component.source.download.sha256)) {
            std::filesystem::remove(targetPath, ec);
            componentError = "Downloaded component installer SHA256 mismatch.";
            return false;
        }
    }

    const std::string componentInstallDir = Utf8FromPath(targetPath.parent_path());
    const std::string expandedArgs =
        ExpandRuntimeTokens(component.source.download.args,
                            installRootForComponents,
                            metadata,
                            pathResolver,
                            componentInstallDir);

    DWORD exitCode = 0;
    if (!ExecuteProcess(targetPath,
                        expandedArgs,
                        component.source.download.showWindow,
                        component.source.download.showWindowConfigured,
                        component.source.download.wait,
                        component.source.download.timeoutSec,
                        options.cancellationCallback,
                        exitCode,
                        error)) {
        componentError = error.empty() ? "Failed to execute downloaded component installer." : error;
        return false;
    }
    if (component.source.download.wait && exitCode != 0) {
        componentError = "Downloaded component installer failed with exit code " +
                         std::to_string(exitCode);
        return false;
    }

    if (!component.source.local.uninstall.empty()) {
        ComponentExecutionRecord record;
        record.componentId = component.id;
        record.sourceType = "download";
        record.uninstallCommand =
            ExpandRuntimeTokens(component.source.local.uninstall,
                                installRootForComponents,
                                metadata,
                                pathResolver,
                                componentInstallDir);
        record.workingDirectory = componentInstallDir;
        record.wait = component.source.download.wait;
        record.timeoutSec = component.source.download.timeoutSec;
        componentActions.push_back(std::move(record));
    }
#else
    (void)component;
    (void)installRootForComponents;
    (void)metadata;
    (void)pathResolver;
    (void)options;
    (void)componentActions;
    componentError = "Download component installers are currently supported on Windows only.";
#endif
    return componentError.empty();
}

} // namespace

bool ExecuteInstallExecution(const ExtendedInstallationMetadata& metadata,
                             MetadataParser& parser,
                             const InstallExecutionPlan& plan,
                             const InstallServiceOptions& options,
                             InstallerPathResolver& pathResolver,
                             InstallProgressReporter& reporter,
                             InstallExecutionOutput& output) {
    output = InstallExecutionOutput{};
    output.success = false;

    reporter.EmitStatus(InstallServiceStatus::Installing,
                        InstallServicePhase::Installing,
                        0.0f,
                        "Installing files...");

    size_t executableComponentCount = 0;
    for (const auto* component : plan.componentPlan.ordered) {
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
    folderSizes.reserve(plan.selectedEmbeddedFolders.size());
    folderProgress.reserve(plan.selectedEmbeddedFolders.size());
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        if (std::find(plan.selectedEmbeddedFolders.begin(),
                      plan.selectedEmbeddedFolders.end(),
                      mapping.folderId) == plan.selectedEmbeddedFolders.end()) {
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
        if (plan.totalInstallBytes > 0) {
            std::lock_guard<std::mutex> lock(installProgressMutex);
            folderProgress[folder] = Clamp01(progress);
            double completed = 0.0;
            for (const auto& entry : folderProgress) {
                auto sizeIt = folderSizes.find(entry.first);
                if (sizeIt != folderSizes.end()) {
                    completed += static_cast<double>(sizeIt->second) * static_cast<double>(entry.second);
                }
            }
            phaseProgress =
                static_cast<float>(completed / static_cast<double>(plan.totalInstallBytes));
        }
        reporter.EmitProgress(folder, currentFile, phaseProgress * extractionWeight);
    };

    LogCallback infoCallback = [&](const std::string& message) {
        reporter.EmitMessage(InstallServiceEventType::Info, message);
    };
    LogCallback errorCallback = [&](const std::string& message) {
        reporter.EmitMessage(InstallServiceEventType::Error, message);
    };

    ParallelInstallResult parallelResult;
    FolderPayloadReader payloadReader(parser.getDataPackagePath());
    if (plan.selectedEmbeddedFolders.empty() && plan.componentPlan.hasComponents) {
        parallelResult.success = true;
        parallelResult.installRootPath = plan.pathDecision.resolvedInstallRoot;
        reporter.EmitMessage(InstallServiceEventType::Info,
                             "No embedded folders selected; skipping package extraction.");
        logInstallerInfo("[InstallFlow][Extract] skipped embedded extraction");
    } else {
        logInstallerInfo(std::string("[InstallFlow][Extract] start folderCount=") +
                         std::to_string(plan.selectedEmbeddedFolders.size()) +
                         " installPath=" + options.installPath);
        parallelResult = RunParallelInstall(metadata,
                                            payloadReader,
                                            pathResolver,
                                            options.installPath,
                                            options.folderMappings,
                                            plan.selectedEmbeddedFolders,
                                            plan.componentPlan.hasComponents,
                                            0,
                                            progressCallback,
                                            infoCallback,
                                            errorCallback,
                                            options.cancellationCallback);
        logInstallerInfo(std::string("[InstallFlow][Extract] end success=") +
                         (parallelResult.success ? "true" : "false") +
                         " cancelled=" + (parallelResult.cancelled ? "true" : "false") +
                         " errors=" + std::to_string(parallelResult.errors.size()) +
                         " installRootPath=" + parallelResult.installRootPath);
    }

    output.timing = parallelResult.timing;
    output.installRootPath = parallelResult.installRootPath;
    output.installedRoots = std::move(parallelResult.installedRoots);
    output.installedFiles = std::move(parallelResult.installedFiles);
    output.cancelled = parallelResult.cancelled;
    output.rebootRequired = parallelResult.rebootRequired;
    output.pendingReplaceFiles = std::move(parallelResult.pendingReplaceFiles);

    if (!parallelResult.success) {
        logInstallerError("[InstallFlow][Extract] failed, aborting installation");
        output.errors = std::move(parallelResult.errors);
        if (output.cancelled && output.errors.empty()) {
            output.errors.push_back("Installation cancelled.");
        }
        if (output.errors.empty()) {
            output.errors.push_back("Installation failed.");
        }
        for (const auto& error : output.errors) {
            reporter.EmitMessage(InstallServiceEventType::Error, error);
        }
        return false;
    }

    if (output.rebootRequired) {
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Some locked files were scheduled for replacement after reboot.");
    }

    if (output.installRootPath.empty()) {
        output.installRootPath = plan.pathDecision.resolvedInstallRoot.empty()
                                     ? options.installPath
                                     : plan.pathDecision.resolvedInstallRoot;
    }

    reporter.EmitProgress("", "File installation completed", extractionWeight);

    output.componentActions.reserve(executableComponentCount);
    std::unordered_set<std::string> failedOptionalComponentIds;
    failedOptionalComponentIds.reserve(executableComponentCount);
    output.failedOptionalComponentMessages.reserve(executableComponentCount);

    if (executableComponentCount > 0) {
        const std::string installRootForComponents = output.installRootPath.empty()
                                                         ? plan.pathDecision.diskCheckPath
                                                         : output.installRootPath;
        size_t completedComponents = 0;

        auto advanceComponentProgress = [&](const std::string& displayName, float offset) {
            float progress = extractionWeight +
                             ((static_cast<float>(completedComponents) + offset) /
                              static_cast<float>(executableComponentCount)) *
                                 (1.0f - extractionWeight);
            reporter.EmitProgress("component", displayName, progress);
        };

        for (const auto* component : plan.componentPlan.ordered) {
            if (!component) {
                continue;
            }
            if (component->source.type != ComponentSourceType::LOCAL &&
                component->source.type != ComponentSourceType::DOWNLOAD) {
                continue;
            }

            if (IsCancellationRequested(options)) {
                output.cancelled = true;
                output.errors.push_back("Installation cancelled.");
                return false;
            }

            const std::string componentDisplayName = ResolveComponentDisplayName(*component);

            advanceComponentProgress(componentDisplayName, 0.0f);
            reporter.EmitMessage(InstallServiceEventType::Info,
                                 "Installing component: " + componentDisplayName +
                                     " (id=" + component->id + ")");

            std::string componentError;
            if (component->source.type == ComponentSourceType::LOCAL) {
                ExecuteLocalComponent(*component,
                                      installRootForComponents,
                                      metadata,
                                      pathResolver,
                                      options,
                                      output.componentActions,
                                      componentError);
            } else {
                ExecuteDownloadComponent(*component,
                                         installRootForComponents,
                                         metadata,
                                         pathResolver,
                                         options,
                                         output.componentActions,
                                         componentError);
            }

            if (!componentError.empty()) {
                std::string failureMessage =
                    "Component '" + componentDisplayName + "' failed: " + componentError;
                if (component->required) {
                    output.cancelled = IsCancellationRequested(options);
                    output.errors.push_back(failureMessage);
                    reporter.EmitMessage(InstallServiceEventType::Error, failureMessage);
                    return false;
                }

                failedOptionalComponentIds.insert(component->id);
                output.failedOptionalComponentMessages.push_back(failureMessage);
                reporter.EmitMessage(InstallServiceEventType::Warning,
                                     failureMessage + " Continuing because component is optional.");
                ++completedComponents;
                advanceComponentProgress(componentDisplayName, 0.0f);
                continue;
            }

            std::string successMessage;
            if ((component->source.type == ComponentSourceType::LOCAL &&
                 !component->source.local.wait) ||
                (component->source.type == ComponentSourceType::DOWNLOAD &&
                 !component->source.download.wait)) {
                successMessage = "Component '" + componentDisplayName +
                                 "' installer launched (not waiting for completion).";
            } else {
                successMessage = "Component '" + componentDisplayName + "' installed successfully.";
            }
            reporter.EmitMessage(InstallServiceEventType::Info, successMessage);

            ++completedComponents;
            advanceComponentProgress(componentDisplayName, 0.0f);
        }
    }

    output.effectiveRegistry = plan.effectiveRegistry;
    output.effectiveAutoStartup = plan.effectiveAutoStartup;
    output.effectiveDesktopIcons = plan.effectiveDesktopIcons;
    output.effectiveKillProcesses = plan.effectiveKillProcesses;

    if (!failedOptionalComponentIds.empty()) {
        std::string message = "Optional components failed:";
        for (const auto& id : failedOptionalComponentIds) {
            message += " ";
            message += id;
        }
        reporter.EmitMessage(InstallServiceEventType::Warning, message);
    }

    output.success = true;
    return true;
}

} // namespace MultiThreadedInstaller
