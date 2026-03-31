#include "installer/install_execution.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/folder_payload_reader.h"
#include "installer/installer_helpers.h"
#include "installer/metadata_parser.h"

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

float Clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

bool IsCancellationRequested(const InstallServiceOptions& options) {
    return options.cancellationCallback && options.cancellationCallback();
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
        } else if (!component.source.local.uninstall.empty()) {
            ComponentExecutionRecord record;
            record.componentId = component.id;
            record.sourceType = "local";
            record.uninstallCommand =
                ExpandRuntimeTokens(component.source.local.uninstall,
                                    installRootForComponents,
                                    metadata,
                                    pathResolver,
                                    componentInstallDir);
            record.workingDirectory = Utf8FromPath(basePath);
            record.wait = component.source.local.wait;
            record.timeoutSec = component.source.local.timeoutSec;
            componentActions.push_back(std::move(record));
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
    std::string saveAs = component.source.download.saveAs;
    if (saveAs.empty()) {
        saveAs = "%InstallDir%\\downloads\\" + component.id + "_setup.bin";
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
        ExpandRuntimeTokens(component.source.download.url,
                            installRootForComponents,
                            metadata,
                            pathResolver,
                            componentInstallDir);
    const std::string expandedArgs =
        ExpandRuntimeTokens(component.source.download.args,
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
                std::string expectedHash = component.source.download.sha256;
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
                                        component.source.download.wait,
                                        component.source.download.timeoutSec,
                                        options.cancellationCallback,
                                        exitCode,
                                        executeError)) {
                        componentError = executeError.empty()
                                             ? "Failed to execute downloaded component installer."
                                             : executeError;
                    } else if (component.source.download.wait && exitCode != 0) {
                        componentError =
                            "Downloaded component installer failed with exit code " +
                            std::to_string(exitCode);
                    } else if (!component.source.download.uninstall.empty()) {
                        ComponentExecutionRecord record;
                        record.componentId = component.id;
                        record.sourceType = "download";
                        record.uninstallCommand =
                            ExpandRuntimeTokens(component.source.download.uninstall,
                                                installRootForComponents,
                                                metadata,
                                                pathResolver,
                                                componentInstallDir);
                        record.workingDirectory = Utf8FromPath(targetPath.parent_path());
                        record.wait = component.source.download.wait;
                        record.timeoutSec = component.source.download.timeoutSec;
                        componentActions.push_back(std::move(record));
                    }
                }
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
    componentError = "Downloaded component installers are currently supported on Windows only.";
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
    for (const auto& mapping : metadata.extendedMappings) {
        if (std::find(plan.selectedEmbeddedFolders.begin(),
                      plan.selectedEmbeddedFolders.end(),
                      mapping.folderName) == plan.selectedEmbeddedFolders.end()) {
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
                         " installPath=" + options.installPath +
                         " threadCount=" + std::to_string(options.threadCount));
        parallelResult = RunParallelInstall(metadata,
                                            payloadReader,
                                            pathResolver,
                                            options.installPath,
                                            options.folderMappings,
                                            plan.selectedEmbeddedFolders,
                                            plan.componentPlan.hasComponents,
                                            options.threadCount,
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
    output.cancelled = parallelResult.cancelled;

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

        auto advanceComponentProgress = [&](const std::string& id, float offset) {
            float progress = extractionWeight +
                             ((static_cast<float>(completedComponents) + offset) /
                              static_cast<float>(executableComponentCount)) *
                                 (1.0f - extractionWeight);
            reporter.EmitProgress("component", id, progress);
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

            advanceComponentProgress(component->id, 0.0f);
            reporter.EmitMessage(InstallServiceEventType::Info,
                                 "Installing component: " + component->id);

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
                    "Component '" + component->id + "' failed: " + componentError;
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
            reporter.EmitMessage(InstallServiceEventType::Info, successMessage);

            ++completedComponents;
            advanceComponentProgress(component->id, 0.0f);
        }
    }

    output.effectiveRegistry = plan.effectiveRegistry;
    std::unordered_set<std::string> registrySeen;
    registrySeen.reserve(output.effectiveRegistry.size() + metadata.components.size() * 2);
    for (const auto& entry : output.effectiveRegistry) {
        std::string key = entry.path;
        key.push_back('\n');
        key += entry.key;
        key.push_back('\n');
        key += entry.value;
        key.push_back('\n');
        key += std::to_string(static_cast<int>(entry.type));
        registrySeen.insert(key);
    }
    output.effectiveAutoStartup = plan.effectiveAutoStartup;
    output.effectiveDesktopIcons = plan.effectiveDesktopIcons;
    output.effectiveKillProcesses = plan.effectiveKillProcesses;

    for (const auto* component : plan.componentPlan.ordered) {
        if (!component) {
            continue;
        }
        if (failedOptionalComponentIds.find(component->id) != failedOptionalComponentIds.end()) {
            continue;
        }
        for (const auto& entry : component->registry) {
            AppendUniqueRegistry(output.effectiveRegistry, registrySeen, entry);
        }
        output.effectiveAutoStartup = output.effectiveAutoStartup || component->autoStartup;
        output.effectiveDesktopIcons =
            output.effectiveDesktopIcons || component->createDesktopShortcut;
    }

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
