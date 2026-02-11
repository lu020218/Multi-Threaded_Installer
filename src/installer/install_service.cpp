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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

namespace {

void EmitEvent(const InstallServiceCallbacks& callbacks, const InstallServiceEvent& event) {
    if (callbacks.onEvent) {
        callbacks.onEvent(event);
    }
}

void EmitStatus(const InstallServiceCallbacks& callbacks,
                InstallServiceStatus status,
                const std::string& message = std::string()) {
    InstallServiceEvent event;
    event.type = InstallServiceEventType::Status;
    event.status = status;
    event.message = message;
    EmitEvent(callbacks, event);
}

void EmitMessage(const InstallServiceCallbacks& callbacks,
                 InstallServiceEventType type,
                 InstallServiceStatus status,
                 const std::string& message) {
    if (message.empty()) {
        return;
    }
    InstallServiceEvent event;
    event.type = type;
    event.status = status;
    event.message = message;
    EmitEvent(callbacks, event);
}

void EmitProgress(const InstallServiceCallbacks& callbacks,
                  InstallServiceStatus status,
                  const std::string& folder,
                  const std::string& currentFile,
                  float progress) {
    InstallServiceEvent event;
    event.type = InstallServiceEventType::Progress;
    event.status = status;
    event.folder = folder;
    event.currentFile = currentFile;
    event.progress = progress;
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
            EmitMessage(callbacks, InstallServiceEventType::Error, currentStatus, message);
        }
        EmitStatus(callbacks, currentStatus,
                   cancelled ? "Installation cancelled." : "Installation failed.");
        if (installStateApplied) {
            applyInstallState(metadata.installState, "failed", pathResolver);
            installStateApplied = false;
        }
        releaseResources();
    };

    try {
        EmitStatus(callbacks, InstallServiceStatus::Preparing, "Preparing installation...");

        if (IsCancellationRequested(options)) {
            markFailed("Installation cancelled.", true, true);
            return result;
        }

        currentStatus = InstallServiceStatus::Precheck;
        EmitStatus(callbacks, currentStatus, "Running installation prechecks...");

        std::string resolvedInstallRoot = pathResolver.resolveFinalPath(
            options.installPath,
            SpecialDirectoryType::INSTALL_DIRECTORY,
            metadata.applicationName);
        std::string diskCheckPath = resolvedInstallRoot.empty() ? options.installPath : resolvedInstallRoot;

        uint64_t requiredBytes = 0;
        for (const auto& mapping : metadata.extendedMappings) {
            requiredBytes += mapping.originalSize;
        }
        uint64_t availableBytes = 0;
        if (!checkDiskSpaceForInstall(diskCheckPath, requiredBytes, availableBytes)) {
            markFailed("Insufficient disk space for installation. required=" +
                           std::to_string(requiredBytes) + " available=" +
                           std::to_string(availableBytes),
                       false,
                       true);
            return result;
        }

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
                EmitMessage(callbacks, InstallServiceEventType::Info, currentStatus,
                            "Terminating processes: " + joined);
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
                EmitMessage(callbacks, InstallServiceEventType::Info, currentStatus,
                            "Detected previous install at: " + previousInstallDir);
                if (previousManifest.empty()) {
                    EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
                                "Old install manifest not found; skipping cleanup.");
                } else if (metadata.autoCleanOldInstall || options.cleanupOldInstallRequested) {
                    ConsoleInterface console;
                    console.showInfo("Cleaning previous installation...");
                    if (!uninstallFromManifest(previousManifest, pathResolver, console)) {
                        EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
                                    "Previous install cleanup reported failure.");
                    }
                } else {
                    EmitMessage(callbacks, InstallServiceEventType::Info, currentStatus,
                                "Skipping cleanup of previous installation.");
                }
            }
        }

        if (metadata.installState.useMutex) {
            EmitMessage(callbacks, InstallServiceEventType::Info, currentStatus,
                        "Acquiring install mutex...");
            installMutex = acquireInstallMutex(metadata.installState);
        }

        applyInstallState(metadata.installState, "installing", pathResolver);
        installStateApplied = true;

        currentStatus = InstallServiceStatus::Installing;
        EmitStatus(callbacks, currentStatus, "Installing files...");

        ProgressCallback progressCallback = [&](const std::string& folder,
                                                const std::string& currentFile,
                                                float progress) {
            EmitProgress(callbacks, currentStatus, folder, currentFile, progress);
        };
        LogCallback infoCallback = [&](const std::string& message) {
            EmitMessage(callbacks, InstallServiceEventType::Info, currentStatus, message);
        };
        LogCallback errorCallback = [&](const std::string& message) {
            EmitMessage(callbacks, InstallServiceEventType::Error, currentStatus, message);
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
                EmitMessage(callbacks, InstallServiceEventType::Error, currentStatus, error);
            }
            markFailed(std::string(), result.cancelled, false);
            return result;
        }

        currentStatus = InstallServiceStatus::Finalizing;
        EmitStatus(callbacks, currentStatus, "Finalizing installation...");

        if (options.applyRegistryBeforeFinalize && !metadata.registry.empty()) {
            std::string prePath = options.preRegistryInstallPath.empty()
                                      ? options.installPath
                                      : options.preRegistryInstallPath;
            applyRegistryEntries(metadata.registry,
                                 prePath,
                                 metadata.configVersion,
                                 metadata.applicationName);
        }

        if ((metadata.autoStartup || metadata.desktopIcons) && result.installRootPath.empty()) {
            EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
                        "Install root not detected; AutoStartup/DesktopIcons skipped");
        }

        if (!result.installRootPath.empty()) {
            std::filesystem::path exePath = findPrimaryExecutable(PathFromUtf8(result.installRootPath),
                                                                  metadata.applicationName);
            if ((metadata.autoStartup || metadata.desktopIcons) && exePath.empty()) {
                EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
                            "No executable found for AutoStartup/DesktopIcons");
            } else {
                if (metadata.autoStartup) {
                    if (setAutoStartup(metadata.applicationName, exePath)) {
                        EmitMessage(callbacks, InstallServiceEventType::Info, currentStatus,
                                    "AutoStartup enabled");
                    } else {
                        EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
                                    "Failed to enable AutoStartup");
                    }
                }
                if (metadata.desktopIcons) {
                    if (createDesktopShortcut(metadata.applicationName, exePath)) {
                        EmitMessage(callbacks, InstallServiceEventType::Info, currentStatus,
                                    "Desktop icon created");
                    } else {
                        EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
                                    "Failed to create desktop icon");
                    }
                }
            }
        }

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
                        EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
                                    "Failed to create uninstall.exe");
                    } else {
                        result.uninstallPath = targetUtf8;
                    }
                }
            }
        }

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
            EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
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
                EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
                            "Failed to write local install manifest");
            }
        }

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
                EmitMessage(callbacks, InstallServiceEventType::Warning, currentStatus,
                            "Failed to write uninstall registry entry");
            }
        }
#endif

        applyInstallState(metadata.installState, "installed", pathResolver);
        installStateApplied = false;
        releaseResources();

        result.success = true;
        currentStatus = InstallServiceStatus::Completed;
        EmitStatus(callbacks, currentStatus, "Installation completed.");
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