#include "installer/install_service.h"

#include "common/utf8_utils.h"
#include "installer/console_interface.h"
#include "installer/install_state_utils.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"
#include "installer/uninstall_manager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
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

std::string NormalizePathForCompare(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    while (!normalized.empty() && (normalized.back() == '\\' || normalized.back() == '/')) {
        normalized.pop_back();
    }
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
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

        std::string resolvedInstallRoot = pathResolver.resolveFinalPath(
            options.installPath,
            SpecialDirectoryType::INSTALL_DIRECTORY,
            metadata.applicationName);
        std::string diskCheckPath = resolvedInstallRoot.empty() ? options.installPath : resolvedInstallRoot;

        uint64_t totalInstallBytes = 0;
        for (const auto& mapping : metadata.extendedMappings) {
            totalInstallBytes += mapping.originalSize;
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
            metadata.installKillProcesses);
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

        std::string previousManifest;
        std::string previousInstallDir;
        if (resolveExistingInstallInfo(metadata.applicationName,
                                       pathResolver,
                                       previousManifest,
                                       previousInstallDir)) {
            std::string normalizedOld = NormalizePathForCompare(previousInstallDir);
            std::string normalizedNew = NormalizePathForCompare(
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

        std::unordered_map<std::string, uint64_t> folderSizes;
        std::unordered_map<std::string, float> folderProgress;
        folderSizes.reserve(metadata.extendedMappings.size());
        folderProgress.reserve(metadata.extendedMappings.size());
        for (const auto& mapping : metadata.extendedMappings) {
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
            emitProgress(folder, currentFile, phaseProgress);
        };

        LogCallback infoCallback = [&](const std::string& message) {
            emitMessage(InstallServiceEventType::Info, message);
        };
        LogCallback errorCallback = [&](const std::string& message) {
            emitMessage(InstallServiceEventType::Error, message);
        };

        ParallelInstallResult parallelResult = RunParallelInstall(
            metadata,
            parser,
            pathResolver,
            options.installPath,
            options.folderMappings,
            options.threadCount,
            progressCallback,
            infoCallback,
            errorCallback,
            options.cancellationCallback);

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

        emitProgress("", "File installation completed", 1.0f);

        currentStatus = InstallServiceStatus::Finalizing;
        currentPhase = InstallServicePhase::Finalizing;
        currentPhaseProgress = 0.0f;
        emitStatus(currentStatus, currentPhase, 0.0f, "Finalizing installation...");

        auto advanceFinalize = [&](float progress, const std::string& detail) {
            emitProgress("finalize", detail, progress);
        };

        if (options.applyRegistryBeforeFinalize && !metadata.registry.empty()) {
            std::string prePath = options.preRegistryInstallPath.empty()
                                      ? options.installPath
                                      : options.preRegistryInstallPath;
            applyRegistryEntries(metadata.registry,
                                 prePath,
                                 metadata.configVersion,
                                 metadata.applicationName);
        }
        advanceFinalize(0.15f, "Applying registry entries");

        if ((metadata.autoStartup || metadata.desktopIcons) && result.installRootPath.empty()) {
            emitMessage(InstallServiceEventType::Warning,
                        "Install root not detected; AutoStartup/DesktopIcons skipped");
        }

        if (!result.installRootPath.empty()) {
            std::filesystem::path exePath = findPrimaryExecutable(PathFromUtf8(result.installRootPath),
                                                                  metadata.applicationName);
            if ((metadata.autoStartup || metadata.desktopIcons) && exePath.empty()) {
                emitMessage(InstallServiceEventType::Warning,
                            "No executable found for AutoStartup/DesktopIcons");
            } else {
                if (metadata.autoStartup) {
                    if (setAutoStartup(metadata.applicationName, exePath)) {
                        emitMessage(InstallServiceEventType::Info, "AutoStartup enabled");
                    } else {
                        emitMessage(InstallServiceEventType::Warning,
                                    "Failed to enable AutoStartup");
                    }
                }
                if (metadata.desktopIcons) {
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
        std::string manifestPath = getDefaultManifestPath(metadata.applicationName, pathResolver);
        if (!writeManifest(manifestPath,
                           metadata.applicationName,
                           metadata.configVersion,
                           result.installRootPath,
                           result.installedFiles,
                           metadata.registry,
                           metadata.installKillProcesses,
                           metadata.autoStartup,
                           metadata.desktopIcons,
                           metadata.installState,
                           result.uninstallPath,
                           languageCode)) {
            emitMessage(InstallServiceEventType::Warning,
                        "Failed to write install manifest");
        }

        if (!result.installRootPath.empty()) {
            std::filesystem::path localPath = PathFromUtf8(result.installRootPath) / "install.manifest.json";
            if (!writeManifest(Utf8FromPath(localPath),
                               metadata.applicationName,
                               metadata.configVersion,
                               result.installRootPath,
                               result.installedFiles,
                               metadata.registry,
                               metadata.installKillProcesses,
                               metadata.autoStartup,
                               metadata.desktopIcons,
                               metadata.installState,
                               result.uninstallPath,
                               languageCode)) {
                emitMessage(InstallServiceEventType::Warning,
                            "Failed to write local install manifest");
            }
        }
        advanceFinalize(0.75f, "Writing install manifest");

        if (options.applyRegistryAfterInstall && !metadata.registry.empty()) {
            applyRegistryEntries(metadata.registry,
                                 result.installRootPath,
                                 metadata.configVersion,
                                 metadata.applicationName);
        }

#ifdef _WIN32
        if (options.writeUninstallRegistry && !result.uninstallPath.empty()) {
            bool perMachine = isRunningAsAdmin();
            if (!writeUninstallRegistryEntry(metadata.applicationName,
                                             metadata.configVersion,
                                             result.installRootPath,
                                             result.uninstallPath,
                                             perMachine)) {
                emitMessage(InstallServiceEventType::Warning,
                            "Failed to write uninstall registry entry");
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
