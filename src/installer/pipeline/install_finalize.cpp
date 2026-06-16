#include "installer/pipeline/install_finalize.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/platform/embedded_resources.h"
#include "installer/state/install_state_utils.h"
#include "installer/state/install_state_store.h"
#include "installer/platform/installer_helpers.h"
#include "installer/state/registry_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace MultiThreadedInstaller {

namespace {

std::vector<std::string> CollectFilesRecursive(const std::vector<std::string>& roots) {
    std::vector<std::string> files;
    auto isInstallerTransientFile = [](const std::filesystem::path& path) {
        const std::wstring filename = path.filename().wstring();
        return filename.size() >= 10 &&
               (filename.find(L".__mti_old") != std::wstring::npos ||
                filename.find(L".__mti_new") != std::wstring::npos ||
                filename.find(L".__mti_reboot_new") != std::wstring::npos);
    };
    auto isPendingCleanupDirectory = [](const std::filesystem::path& path) {
        const std::string name = Utf8FromPath(path.filename());
        return name.rfind(".mti_delete_pending_", 0) == 0;
    };
    for (const auto& rootPath : roots) {
        if (rootPath.empty()) {
            continue;
        }
        std::filesystem::path root = PathFromUtf8(rootPath);
        if (!std::filesystem::exists(root)) {
            continue;
        }
        std::error_code iterEc;
        for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, iterEc), end;
             !iterEc && it != end;
             it.increment(iterEc)) {
            const auto& entry = *it;
            if (entry.is_directory() && isPendingCleanupDirectory(entry.path())) {
                it.disable_recursion_pending();
                continue;
            }
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

// [安装收尾-注册表写入] 需要写入的「附加注册表项」在此用代码写死（取代旧的 YAML registry 名单）。
// 返回的每一项都会在 finalize 阶段写入注册表；entry.value 支持占位符 %InstallDir% / 版本 / 产品名
//（由 applyRegistryEntries → expandRegistryValue 展开）。
// 说明：产品状态注册表（HKLM\Software\<product> 的 Version/InstallDir/InstallState 等）已由
// ApplyInstallState 写死处理、系统卸载入口由 WriteConfiguredSystemUninstallEntries 处理，二者
// 不需在此重复；此处仅放“本产品额外需要写死的注册表项”。
std::vector<RegistryEntry> BuildPostInstallRegistryEntries(const ExtendedInstallationMetadata& metadata) {
    std::vector<RegistryEntry> entries;
    (void)metadata;

    // 在此按需写死注册表项（示例，取消注释并按业务修改）：
    // 注意：在此写入的每一项都会被记入卸载账本(manifest cleanup.registry)，卸载时按 path+key
    //       对称删除，不会残留。
    //   RegistryEntry e;
    //   e.path  = "HKLM\\Software\\" + metadata.appProductName;  // 注册表键路径
    //   e.key   = "DataDir";                                     // 值名
    //   e.value = "%InstallDir%";                                // 值（支持 %InstallDir% 等占位符）
    //   e.type  = RegistryValueType::EXPAND_STRING;              // STRING / DWORD / EXPAND_STRING
    //   entries.push_back(std::move(e));

    return entries;
}

// [安装收尾-系统卸载入口] 实现：系统卸载入口写死（需求 §5）：displayName=产品名、
// publisher=publisher、scope=machine（perMachine=true），写入「程序和功能」列表。
void WriteConfiguredSystemUninstallEntries(const ExtendedInstallationMetadata& metadata,
                                           const InstallExecutionPlan& plan,
                                           const std::string& desktopShortcutDisplayName,
                                           const InstallServiceResult& result,
                                           UninstallCleanupConfig& manifestCleanup,
                                           InstallProgressReporter& reporter) {
#ifdef _WIN32
    (void)plan;
    (void)desktopShortcutDisplayName;
    if (result.uninstallPath.empty()) {
        return;
    }
    const std::string uninstallKeyName = metadata.appProductName;
    const std::string uninstallDisplayName = metadata.appProductName;

    const bool wroteAny = writeUninstallRegistryEntry(uninstallKeyName,
                                                      uninstallDisplayName,
                                                      metadata.appVersion,
                                                      result.installRootPath,
                                                      result.uninstallPath,
                                                      /*perMachine=*/true,
                                                      metadata.appPublisher);
    if (wroteAny) {
        UninstallEntryCleanup entry;
        entry.name = uninstallDisplayName;
        entry.scope = UninstallEntryScope::LOCAL_MACHINE;
        manifestCleanup.uninstallEntries.push_back(std::move(entry));
    } else {
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Failed to write uninstall registry entry");
    }
#else
    (void)metadata;
    (void)plan;
    (void)desktopShortcutDisplayName;
    (void)result;
    (void)manifestCleanup;
    (void)reporter;
#endif
}

InstallStateContext BuildInstallStateContext(const ExtendedInstallationMetadata& metadata,
                                             const InstallExecutionPlan& plan,
                                             const InstallServiceOptions& options,
                                             const std::string& installDir,
                                             const std::string& state) {
    InstallStateContext context;
    context.installDir = installDir;
    context.version = metadata.appVersion;
    context.appName = metadata.appProductName;
    context.appId = plan.effectiveAppId.empty() ? metadata.appProductName : plan.effectiveAppId;
    context.installSource = getCurrentExecutablePath();
    context.state = state;
    context.userName = GetCurrentUserNameForInstallState();
    if (context.installDir.empty()) {
        context.installDir = options.installPath;
    }
    return context;
}

// Resolves the per-file content fingerprints of the newly installed payload to
// absolute target paths so they can be recorded in install.manifest.json. The
// next upgrade uses these for the zero-read skip decision (Scheme A).
InstalledFileFingerprintMap BuildInstalledFileFingerprints(const ExtendedInstallationMetadata& metadata,
                                                           const InstallExecutionPlan& plan,
                                                           InstallerPathResolver& pathResolver) {
    InstalledFileFingerprintMap fingerprints;
    std::unordered_set<std::string> selected(plan.selectedEmbeddedFolders.begin(),
                                             plan.selectedEmbeddedFolders.end());
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        if (selected.find(mapping.folderId) == selected.end() || mapping.fileIndex.empty()) {
            continue;
        }
        std::string target = mapping.target.empty() ? mapping.targetPath : mapping.target;
        const std::string token = "%InstallDir%";
        size_t pos = 0;
        while ((pos = target.find(token, pos)) != std::string::npos) {
            target.replace(pos, token.size(), plan.pathDecision.resolvedInstallRoot);
            pos += plan.pathDecision.resolvedInstallRoot.size();
        }
        target = pathResolver.expandEnvironmentVariables(target);
        if (target.empty()) {
            continue;
        }
        for (const auto& file : mapping.fileIndex) {
            if (file.relativePath.empty() || file.contentHash == 0) {
                continue;
            }
            const std::filesystem::path candidate =
                PathFromUtf8(target) / PathFromUtf8(file.relativePath);
            const std::string key = normalizePathForCompare(Utf8FromPath(candidate));
            if (key.empty()) {
                continue;
            }
            InstalledFileFingerprint fingerprint;
            fingerprint.size = file.size;
            fingerprint.contentHash = file.contentHash;
            fingerprints[key] = fingerprint;
        }
    }
    return fingerprints;
}

} // namespace

bool ExecuteInstallFinalization(const ExtendedInstallationMetadata& metadata,
                                const InstallExecutionPlan& plan,
                                const InstallServiceOptions& options,
                                InstallerPathResolver& pathResolver,
                                InstallProgressReporter& reporter,
                                InstallServiceResult& result) {
    // 注册表写入项在代码里写死（取代旧 YAML registry 名单）；其余生效配置直接读 plan。
    const std::vector<RegistryEntry> effectiveRegistry = BuildPostInstallRegistryEntries(metadata);
    const std::vector<std::string>& effectiveKillProcesses = plan.effectiveKillProcesses;
    const bool effectiveAutoStartup = plan.effectiveAutoStartup;
    const bool effectiveDesktopIcons = plan.effectiveDesktopIcons;

    reporter.EmitStatus(InstallServiceStatus::Finalizing,
                        InstallServicePhase::Finalizing,
                        0.0f,
                        "Finalizing installation...");

    logInstallerInfo(std::string("[InstallFlow][Finalize] start installRootPath=") +
                     result.installRootPath +
                     " registryEntries=" + std::to_string(effectiveRegistry.size()));

    auto advanceFinalize = [&](float progress, const std::string& detail) {
        reporter.EmitProgress("finalize", detail, progress);
    };

    // [安装收尾-注册表写入] 写通用注册表名单 effectiveRegistry（解压前阶段）。
    // 重构后该名单已为空（YAML registry 名单删除），保留挂钩点以兼容个别需要预写的场景。
    if (options.applyRegistryBeforeFinalize && !effectiveRegistry.empty()) {
        std::string prePath = options.preRegistryInstallPath.empty()
                                  ? options.installPath
                                  : options.preRegistryInstallPath;
        applyRegistryEntries(effectiveRegistry, prePath, metadata.appVersion, metadata.appProductName);
    }
    advanceFinalize(0.15f, "Applying registry entries");

    if ((effectiveAutoStartup || effectiveDesktopIcons) && result.installRootPath.empty()) {
        reporter.EmitMessage(InstallServiceEventType::Warning,
                             "Install root not detected; installAutoStartup/installDesktopIcon skipped");
    }

    const std::string languageCode = ResolveLanguageCode(options.languageCode);
    const std::string desktopShortcutDisplayName =
        ResolveDesktopShortcutDisplayName(metadata, languageCode);
    UninstallCleanupConfig manifestCleanup;
    // [安装收尾-注册表写入] 把写入的自定义注册表项(path+key)记入卸载账本，使卸载能对称删除，
    // 避免 BuildPostInstallRegistryEntries 写的注册表残留。只记位置(path/key)，删除不需要值。
    for (const auto& entry : effectiveRegistry) {
        if (entry.path.empty() || entry.key.empty()) {
            continue;
        }
        RegistryEntry record;
        record.path = entry.path;
        record.key = entry.key;
        manifestCleanup.registryEntries.push_back(std::move(record));
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
            findPrimaryExecutable(PathFromUtf8(result.installRootPath), metadata.appProductName);
        if ((effectiveAutoStartup || effectiveDesktopIcons) && exePath.empty()) {
            reporter.EmitMessage(InstallServiceEventType::Warning,
                                 "No executable found for installAutoStartup/installDesktopIcon");
        } else {
            // [安装收尾-开机自启] 设置开机自启动（写 HKCU\...\Run\<产品名> = 主 exe），
            // 并把该项记入 manifest 的 startup 清理账本，卸载时据此移除。
            if (effectiveAutoStartup) {
                if (setAutoStartup(metadata.appProductName, exePath)) {
                    reporter.EmitMessage(InstallServiceEventType::Info, "installAutoStartup enabled");
                    AppendNamedCleanupEntry(manifestCleanup.startup, metadata.appProductName);
                } else {
                    reporter.EmitMessage(InstallServiceEventType::Warning,
                                         "Failed to enable installAutoStartup");
                }
            }
            if (effectiveDesktopIcons) {
                // [安装收尾-快捷方式图标] 手动指定桌面快捷方式图标为安装目录下的 app.ico；
                // 该文件存在时用它作图标，否则 createDesktopShortcut 回退到 exe 自带图标。
                const std::filesystem::path desktopShortcutIcon =
                    PathFromUtf8(result.installRootPath) / "app.ico";
                // [安装收尾-桌面快捷方式] 在桌面创建指向主 exe 的快捷方式，并记入 shortcuts 清理账本。
                if (createDesktopShortcut(desktopShortcutDisplayName, exePath, desktopShortcutIcon)) {
                    reporter.EmitMessage(InstallServiceEventType::Info, "Desktop icon created");
                    AppendNamedCleanupEntry(manifestCleanup.shortcuts, desktopShortcutDisplayName);
                } else {
                    reporter.EmitMessage(InstallServiceEventType::Warning,
                                         "Failed to create desktop icon");
                }
                // [安装收尾-开始菜单快捷方式] 在开始菜单创建同名快捷方式。
                if (createStartMenuShortcut(desktopShortcutDisplayName, exePath, metadata.appProductName)) {
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

    if (result.installedFiles.empty()) {
        result.installedFiles = CollectFilesRecursive(result.installedRoots);
    } else {
        std::sort(result.installedFiles.begin(), result.installedFiles.end());
        result.installedFiles.erase(std::unique(result.installedFiles.begin(), result.installedFiles.end()),
                                    result.installedFiles.end());
    }

    if (!result.installRootPath.empty()) {
        std::filesystem::path target = PathFromUtf8(result.installRootPath) / "uninstall.exe";
        const std::string targetUtf8 = Utf8FromPath(target);
        if (ExtractEmbeddedBinaryResourceToFile("UNINSTALLER_EXE", targetUtf8)) {
            result.uninstallPath = targetUtf8;
            // 方案B：内嵌的 uninstaller 不带 RES_ZIP（避免安装器里资源存两份）。此处把安装器自带的
            // RES_ZIP 注入到刚释放的 uninstall.exe，使磁盘上的卸载器自包含——不依赖任何散落资源文件，
            // 用户误删资源也不影响卸载界面。注入失败仅告警（卸载仍可进行，只是 GUI 皮肤可能缺失）。
            const std::vector<uint8_t> resZip = LoadEmbeddedBinaryResource("RES_ZIP");
            if (!resZip.empty()) {
                if (!InjectBinaryResourceIntoFile(targetUtf8, "RES_ZIP", resZip)) {
                    reporter.EmitMessage(InstallServiceEventType::Warning,
                                         "Failed to inject GUI resources into uninstall.exe");
                }
            }
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

    // [安装收尾-系统卸载入口] 写「程序和功能」卸载入口（HKLM\...\Uninstall\<产品名>），
    // 并把该入口记入 manifest 的 uninstallEntries 清理账本。
    WriteConfiguredSystemUninstallEntries(metadata,
                                          plan,
                                          desktopShortcutDisplayName,
                                          result,
                                          manifestCleanup,
                                          reporter);
    advanceFinalize(0.62f, "Writing uninstall registry");

    if (!result.installRootPath.empty()) {
        std::filesystem::path localPath = PathFromUtf8(result.installRootPath) / "install.manifest.json";
        const InstalledFileFingerprintMap fileFingerprints =
            BuildInstalledFileFingerprints(metadata, plan, pathResolver);
        if (!writeManifest(Utf8FromPath(localPath),
                           plan.effectiveAppId,
                           metadata.appProductName,
                           metadata.appVersion,
                           result.installRootPath,
                           result.installedRoots,
                           manifestCleanup,
                           result.installedFiles,
                           effectiveKillProcesses,
                           effectiveAutoStartup,
                           effectiveDesktopIcons,
                           desktopShortcutDisplayName,
                           result.uninstallPath,
                           languageCode,
                           metadata.appPublisher,
                           fileFingerprints)) {
            reporter.EmitMessage(InstallServiceEventType::Warning,
                                 "Failed to write local install manifest");
        }
    }
    advanceFinalize(0.75f, "Writing install manifest");

    // [安装收尾-注册表写入] 写通用注册表名单 effectiveRegistry（解压后阶段，名单当前为空）。
    if (options.applyRegistryAfterInstall && !effectiveRegistry.empty()) {
        applyRegistryEntries(effectiveRegistry,
                             result.installRootPath,
                             metadata.appVersion,
                             metadata.appProductName);
    }

    advanceFinalize(0.90f, "Registry finalization complete");

    // [安装收尾-注册表写入] 写产品注册表 HKLM\Software\<产品名> 的 Version/InstallDir/InstallState，
    // 这是引擎写死的安装状态落点，供后续覆盖/升级探测与卸载发现安装目录使用。
    ApplyInstallState(BuildInstallStateContext(metadata,
                                               plan,
                                               options,
                                               result.installRootPath,
                                               "installed"),
                      pathResolver);
    advanceFinalize(1.0f, "Finalization complete");
    return true;
}

} // namespace MultiThreadedInstaller
