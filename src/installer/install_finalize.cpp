#include "installer/install_finalize.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/embedded_resources.h"
#include "installer/install_state_utils.h"
#include "installer/installer_helpers.h"
#include "installer/registry_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace MultiThreadedInstaller {

namespace {

std::vector<std::string> CollectFilesRecursive(const std::vector<std::string>& roots) {
    std::vector<std::string> files;
    auto isInstallerTransientFile = [](const std::filesystem::path& path) {
        const std::wstring filename = path.filename().wstring();
        return filename.size() >= 10 &&
               (filename.find(L".__mti_old") != std::wstring::npos ||
                filename.find(L".__mti_reboot_new") != std::wstring::npos);
    };
    for (const auto& rootPath : roots) {
        if (rootPath.empty()) {
            continue;
        }
        std::filesystem::path root = PathFromUtf8(rootPath);
        if (!std::filesystem::exists(root)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && !isInstallerTransientFile(entry.path())) {
                files.push_back(Utf8FromPath(entry.path()));
            }
        }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

void AppendNamedCleanupEntry(std::vector<NamedCleanupEntry>& entries, const std::string& name) {
    if (name.empty()) {
        return;
    }
    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    auto it = std::find_if(entries.begin(), entries.end(), [&](const NamedCleanupEntry& entry) {
        std::string candidate = entry.name;
        std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return candidate == lowered;
    });
    if (it == entries.end()) {
        NamedCleanupEntry entry;
        entry.name = name;
        entries.push_back(std::move(entry));
    }
}

} // namespace

bool ExecuteInstallFinalization(const ExtendedInstallationMetadata& metadata,
                                const InstallExecutionPlan& plan,
                                const InstallServiceOptions& options,
                                const std::vector<RegistryEntry>& effectiveRegistry,
                                const std::vector<std::string>& effectiveKillProcesses,
                                bool effectiveAutoStartup,
                                bool effectiveDesktopIcons,
                                const std::vector<ComponentExecutionRecord>& componentActions,
                                InstallerPathResolver& pathResolver,
                                InstallProgressReporter& reporter,
                                InstallServiceResult& result) {
    reporter.EmitStatus(InstallServiceStatus::Finalizing,
                        InstallServicePhase::Finalizing,
                        0.0f,
                        "Finalizing installation...");

    logInstallerInfo(std::string("[InstallFlow][Finalize] start installRootPath=") +
                     result.installRootPath +
                     " registryEntries=" + std::to_string(effectiveRegistry.size()) +
                     " componentActions=" + std::to_string(componentActions.size()));

    auto advanceFinalize = [&](float progress, const std::string& detail) {
        reporter.EmitProgress("finalize", detail, progress);
    };

    if (options.applyRegistryBeforeFinalize && !effectiveRegistry.empty()) {
        std::string prePath = options.preRegistryInstallPath.empty()
                                  ? options.installPath
                                  : options.preRegistryInstallPath;
        applyRegistryEntries(effectiveRegistry, prePath, metadata.appVersion, metadata.appName);
    }
    advanceFinalize(0.15f, "Applying registry entries");

    if ((effectiveAutoStartup || effectiveDesktopIcons) && result.installRootPath.empty()) {
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Install root not detected; installAutoStartup/installDesktopIcon skipped");
    }

    const std::string languageCode = ResolveLanguageCode(options.languageCode);
    const std::string desktopShortcutDisplayName =
        ResolveDesktopShortcutDisplayName(metadata, languageCode);
    UninstallCleanupConfig manifestCleanup = metadata.lifecycleUninstallCleanup;
    if (!plan.effectiveAppId.empty()) {
        UninstallEntryCleanup uninstallEntry;
        uninstallEntry.name = plan.effectiveAppId;
        uninstallEntry.scope = UninstallEntryScope::ANY;
        manifestCleanup.uninstallEntries.push_back(std::move(uninstallEntry));
    }

    if (!result.installRootPath.empty()) {
        if (!plan.legacyDesktopShortcutCandidates.empty()) {
            for (const auto& shortcutName : plan.legacyDesktopShortcutCandidates) {
                if (deleteDesktopShortcut(shortcutName)) {
                    reporter.EmitMessage(InstallServiceEventType::Info,
                                         "Removed legacy desktop shortcut: " + shortcutName);
                }
                if (deleteStartMenuShortcut(shortcutName)) {
                    reporter.EmitMessage(InstallServiceEventType::Info,
                                         "Removed legacy start menu shortcut: " + shortcutName);
                }
            }
        }

        std::filesystem::path exePath =
            findPrimaryExecutable(PathFromUtf8(result.installRootPath), metadata.appName);
        if ((effectiveAutoStartup || effectiveDesktopIcons) && exePath.empty()) {
            reporter.EmitMessage(InstallServiceEventType::Warning,
                                 "No executable found for installAutoStartup/installDesktopIcon");
        } else {
            if (effectiveAutoStartup) {
                if (setAutoStartup(metadata.appName, exePath)) {
                    reporter.EmitMessage(InstallServiceEventType::Info, "installAutoStartup enabled");
                    AppendNamedCleanupEntry(manifestCleanup.startup, metadata.appName);
                } else {
                    reporter.EmitMessage(InstallServiceEventType::Warning,
                                         "Failed to enable installAutoStartup");
                }
            }
            if (effectiveDesktopIcons) {
                if (createDesktopShortcut(desktopShortcutDisplayName, exePath)) {
                    reporter.EmitMessage(InstallServiceEventType::Info, "Desktop icon created");
                    AppendNamedCleanupEntry(manifestCleanup.shortcuts, desktopShortcutDisplayName);
                } else {
                    reporter.EmitMessage(InstallServiceEventType::Warning,
                                         "Failed to create desktop icon");
                }
                if (createStartMenuShortcut(desktopShortcutDisplayName, exePath, metadata.appName)) {
                    reporter.EmitMessage(InstallServiceEventType::Info, "Start menu shortcut created");
                    AppendNamedCleanupEntry(manifestCleanup.shortcuts, desktopShortcutDisplayName);
                } else {
                    reporter.EmitMessage(InstallServiceEventType::Warning,
                                         "Failed to create start menu shortcut");
                }
            }
        }
    }
    advanceFinalize(0.35f, "Creating startup and shortcut entries");

    result.installedFiles = CollectFilesRecursive(result.installedRoots);

    if (!result.installRootPath.empty()) {
        std::filesystem::path target = PathFromUtf8(result.installRootPath) / "uninstall.exe";
        const std::string targetUtf8 = Utf8FromPath(target);
        if (ExtractEmbeddedBinaryResourceToFile("UNINSTALLER_EXE", targetUtf8)) {
            result.uninstallPath = targetUtf8;
        } else {
            reporter.EmitMessage(InstallServiceEventType::Warning,
                                 "Failed to extract embedded uninstaller.exe");
        }
    }
    advanceFinalize(0.50f, "Preparing uninstall entry point");

    if (!result.uninstallPath.empty()) {
        result.installedFiles.erase(
            std::remove(result.installedFiles.begin(), result.installedFiles.end(), result.uninstallPath),
            result.installedFiles.end());
    }

    if (!result.installRootPath.empty()) {
        std::filesystem::path localPath = PathFromUtf8(result.installRootPath) / "install.manifest.json";
        if (!writeManifest(Utf8FromPath(localPath),
                           plan.effectiveAppId,
                           metadata.appName,
                           metadata.appVersion,
                           result.installRootPath,
                           result.installedRoots,
                           manifestCleanup,
                           result.installedFiles,
                           effectiveRegistry,
                           effectiveKillProcesses,
                           effectiveAutoStartup,
                           effectiveDesktopIcons,
                           desktopShortcutDisplayName,
                           metadata.installInfo,
                           result.uninstallPath,
                           languageCode,
                           componentActions,
                           options.selectedComponentIds,
                           options.installAllComponents)) {
            reporter.EmitMessage(InstallServiceEventType::Warning,
                                 "Failed to write local install manifest");
        }
    }
    advanceFinalize(0.75f, "Writing install manifest");

    if (options.applyRegistryAfterInstall && !effectiveRegistry.empty()) {
        applyRegistryEntries(effectiveRegistry,
                             result.installRootPath,
                             metadata.appVersion,
                             metadata.appName);
    }

#ifdef _WIN32
    if (options.writeUninstallRegistry && !result.uninstallPath.empty()) {
        bool perMachine = isRunningAsAdmin();
        if (!writeUninstallRegistryEntry(plan.effectiveAppId,
                                         desktopShortcutDisplayName,
                                         metadata.appVersion,
                                         result.installRootPath,
                                         result.uninstallPath,
                                         perMachine)) {
            reporter.EmitMessage(InstallServiceEventType::Warning,
                                 "Failed to write uninstall registry entry");
        }
    }
#endif
    advanceFinalize(0.90f, "Writing uninstall registry");

    applyCoreInstallInfo(metadata.installInfo,
                         result.installRootPath,
                         metadata.appVersion,
                         metadata.appName,
                         "installed",
                         pathResolver);
    advanceFinalize(1.0f, "Finalization complete");
    return true;
}

} // namespace MultiThreadedInstaller
