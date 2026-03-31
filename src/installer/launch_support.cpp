#include "installer/launch_support.h"

#include "common/installer_exit_codes.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "gui/gui_helpers.h"
#include "installer/embedded_resources.h"
#include "installer/gui_resource_loader.h"
#include "installer/install_service.h"
#include "installer/installer_helpers.h"
#include "installer/metadata_parser.h"
#include "installer/path_resolver.h"
#include "installer/registry_utils.h"
#include "installer/uninstall_manager.h"

#include <Shlwapi.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <sstream>

#pragma comment(lib, "Shlwapi.lib")

using namespace DuiLib;

namespace MultiThreadedInstaller {

namespace {

template <typename CharT>
bool HasFlag(const std::vector<std::basic_string<CharT>>& args, const std::basic_string<CharT>& flag) {
    std::basic_string<CharT> loweredFlag = flag;
    std::transform(loweredFlag.begin(), loweredFlag.end(), loweredFlag.begin(),
                   [](CharT ch) { return static_cast<CharT>(std::towlower(ch)); });
    for (const auto& arg : args) {
        std::basic_string<CharT> lowered = arg;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](CharT ch) { return static_cast<CharT>(std::towlower(ch)); });
        if (lowered == loweredFlag) {
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> GetWideArgs() {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (!argvW) {
        return args;
    }
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argvW[i] ? argvW[i] : L"");
    }
    LocalFree(argvW);
    return args;
}

std::vector<char*> BuildArgv(std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& arg : args) {
        argv.push_back(arg.empty() ? const_cast<char*>("") : arg.data());
    }
    argv.push_back(nullptr);
    return argv;
}

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
    if (component.source.type != ComponentSourceType::LOCAL) {
        return false;
    }
    std::wstring installer = Utf8ToWide(component.source.local.installer);
    std::transform(installer.begin(), installer.end(), installer.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return installer.find(L"post_setup_agent.exe") != std::wstring::npos;
}

std::wstring ResolvePostSetupStatePathTemplate(const ExtendedInstallationMetadata& metadata) {
    for (const auto& component : metadata.layoutComponents) {
        if (!IsPostSetupAgentComponent(component)) {
            continue;
        }
        std::wstring statePath =
            ExtractArgValueFromCommandLine(component.source.local.args, L"--state-path");
        if (!statePath.empty()) {
            return statePath;
        }
    }
    return {};
}

int RunCleanupHelper(const std::vector<std::wstring>& wideArgs) {
    auto getValue = [&](const std::wstring& flag) -> std::wstring {
        for (size_t i = 1; i + 1 < wideArgs.size(); ++i) {
            std::wstring current = wideArgs[i];
            std::transform(current.begin(), current.end(), current.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            std::wstring loweredFlag = flag;
            std::transform(loweredFlag.begin(), loweredFlag.end(), loweredFlag.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            if (current == loweredFlag) {
                return wideArgs[i + 1];
            }
        }
        return {};
    };

    auto collectValues = [&](const std::wstring& flag) -> std::vector<std::wstring> {
        std::vector<std::wstring> values;
        for (size_t i = 1; i + 1 < wideArgs.size(); ++i) {
            std::wstring current = wideArgs[i];
            std::transform(current.begin(), current.end(), current.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            std::wstring loweredFlag = flag;
            std::transform(loweredFlag.begin(), loweredFlag.end(), loweredFlag.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
            if (current == loweredFlag) {
                values.push_back(wideArgs[i + 1]);
                ++i;
            }
        }
        return values;
    };

    auto removePathWithRetry = [](const std::filesystem::path& path, bool recursive) {
        if (path.empty()) {
            return true;
        }
        for (int attempt = 0; attempt < 120; ++attempt) {
            std::error_code existsEc;
            if (!std::filesystem::exists(path, existsEc)) {
                return true;
            }
            std::error_code removeEc;
            if (recursive) {
                std::filesystem::remove_all(path, removeEc);
            } else {
                std::filesystem::remove(path, removeEc);
            }
            std::error_code existsAfterEc;
            if (!std::filesystem::exists(path, existsAfterEc)) {
                return true;
            }
            Sleep(500);
        }
        std::error_code finalExistsEc;
        return !std::filesystem::exists(path, finalExistsEc);
    };

    std::wstring parentPidValue = getValue(L"--cleanup-parent-pid");
    DWORD parentPid = parentPidValue.empty() ? 0 : static_cast<DWORD>(std::wcstoul(parentPidValue.c_str(), nullptr, 10));
    if (parentPid != 0) {
        HANDLE parentHandle = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        if (parentHandle) {
            WaitForSingleObject(parentHandle, 60000);
            CloseHandle(parentHandle);
        } else {
            Sleep(1000);
        }
    }

    const std::wstring cleanupExe = getValue(L"--cleanup-exe");
    const std::wstring cleanupManifest = getValue(L"--cleanup-manifest");
    const std::vector<std::wstring> cleanupRoots = collectValues(L"--cleanup-root");

    if (!cleanupExe.empty()) {
        removePathWithRetry(cleanupExe, false);
    }
    if (!cleanupManifest.empty()) {
        removePathWithRetry(cleanupManifest, false);
    }
    for (const auto& root : cleanupRoots) {
        if (!root.empty()) {
            removePathWithRetry(root, true);
        }
    }

    std::string helperExePath = getCurrentExecutablePath();
    if (!helperExePath.empty()) {
        std::wstring helperExeWide = Utf8ToWide(helperExePath);
        if (!helperExeWide.empty()) {
            MoveFileExW(helperExeWide.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
    }
    return 0;
}

void EnsureInstallerLoggingInitialized() {
    static std::once_flag once;
    std::call_once(once, []() { initializeInstallerLogging(); });
}

void EnablePerMonitorDpiAwareness() {
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)-3)
#endif
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto setContext = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext) {
            if (setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                return;
            }
            setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
            return;
        }
    }

    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        auto setAwareness = reinterpret_cast<HRESULT(WINAPI*)(int)>(
            GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (setAwareness) {
            setAwareness(2);
            FreeLibrary(shcore);
            return;
        }
        FreeLibrary(shcore);
    }

    if (user32) {
        auto setAware = reinterpret_cast<BOOL(WINAPI*)(void)>(
            GetProcAddress(user32, "SetProcessDPIAware"));
        if (setAware) {
            setAware();
        }
    }
}

void SetInstallerAppNameEnv(const std::string& appName) {
    if (appName.empty()) {
        return;
    }
    std::wstring wide = Utf8ToWide(appName);
    if (!wide.empty()) {
        SetEnvironmentVariableW(L"MTINSTALLER_APPNAME", wide.c_str());
    }
}

class ScopedComInit {
public:
    ScopedComInit() : initialized_(false), hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {
        initialized_ = SUCCEEDED(hr_);
    }
    ~ScopedComInit() {
        if (initialized_) {
            CoUninitialize();
        }
    }
    bool ok() const { return initialized_; }
private:
    bool initialized_;
    HRESULT hr_;
};

InstallConfig CreateInstallConfigFromMetadata(const ExtendedInstallationMetadata& metadata) {
    InstallConfig config;
    config.applicationName = Utf8ToWide(metadata.appName);
    config.appId = Utf8ToWide(resolveEffectiveAppId(metadata.appId, metadata.appName));
    config.directoryName =
        Utf8ToWide(resolveEffectiveDirectoryName(metadata.appDirectoryName, metadata.appName));
    config.legacyAppIds.reserve(metadata.compatibilityLegacyAppIds.size());
    for (const auto& legacyId : metadata.compatibilityLegacyAppIds) {
        config.legacyAppIds.push_back(Utf8ToWide(legacyId));
    }
    config.version = Utf8ToWide(metadata.appVersion);
    config.defaultInstallPath = Utf8ToWide(metadata.installDefaultDir);
    for (const auto& entry : metadata.lifecycleInstallRegistry) {
        if (entry.key == "InstallDir") {
            config.registryPath = Utf8ToWide(entry.path);
            config.registryKey = Utf8ToWide(entry.key);
            break;
        }
    }
    config.webPageUrl = Utf8ToWide(metadata.appWebsite);
    config.executableName = Utf8ToWide(metadata.appName + ".exe");
    config.autoStartup = metadata.installAutoStartup;
    config.desktopIcons = metadata.installDesktopIcon;
    config.repairMode = false;
    config.postSetupStatePath = ResolvePostSetupStatePathTemplate(metadata);

    uint64_t totalSize = 0;
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        totalSize += mapping.originalSize;
    }
    config.requiredDiskSpace = totalSize;
    config.installDirectoryAppendName = true;
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
            config.installDirectoryAppendName = mapping.appendDirectoryName;
            break;
        }
    }
    return config;
}

UINT GetDpiForWindowSafe(HWND hwnd) {
    typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (fn && hwnd) {
            return fn(hwnd);
        }
    }
    HDC screen = GetDC(nullptr);
    const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) {
        ReleaseDC(nullptr, screen);
    }
    return dpi > 0 ? static_cast<UINT>(dpi) : 96U;
}

int RunGuiWindow(GUIManager& frame,
                 const std::wstring& title,
                 const GuiResourceContext& context,
                 bool uninstallMode,
                 WindowSize fallbackSize) {
    HWND hwnd = nullptr;
    WindowSize baseSize = GetWindowSizeFromResources(
        context.useZip, context.resourcePath, context.skinsPath, uninstallMode, fallbackSize);
    const UINT startupDpi = GetDpiForWindowSafe(nullptr);
    const float startupScale = static_cast<float>(startupDpi) / 96.0f;
    const int scaledWidth = static_cast<int>(baseSize.width * startupScale);
    const int scaledHeight = static_cast<int>(baseSize.height * startupScale);
    frame.PrepareInitialDpi(startupDpi);

    try {
        logInstallerInfo("[GUI] About to call Create().");
        hwnd = frame.Create(nullptr, title.c_str(), UI_WNDSTYLE_FRAME, 0L, 0, 0, scaledWidth, scaledHeight);
    } catch (const std::exception& e) {
        logInstallerError(std::string("[GUI] Exception during Create(): ") + e.what());
        return 1;
    } catch (...) {
        logInstallerError("[GUI] Unknown exception during Create().");
        return 1;
    }

    if (!hwnd) {
        logInstallerError(std::string("[GUI] Create() returned NULL. GetLastError()=") + std::to_string(GetLastError()));
        return 1;
    }
    frame.CenterWindow();
    logInstallerInfo("[GUI] Centered window.");
    frame.ShowWindow(true);
    logInstallerInfo("[GUI] Showed window, entering message loop.");
    CPaintManagerUI::MessageLoop();
    logInstallerInfo("[GUI] Message loop exited.");
    CPaintManagerUI::SetResourceZip(_T(""), true);
    return 0;
}

std::wstring GetRepairTitle(const std::wstring& languageCode) {
    std::wstring lowered = languageCode;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    if (lowered.find(L"zh") != std::wstring::npos) {
        return L"修复向导";
    }
    return L"Repair Wizard";
}

bool TryResolveRepairInstallDir(const ExtendedInstallationMetadata& metadata,
                                InstallerPathResolver& pathResolver,
                                std::string& manifestPath,
                                std::string& installDir) {
    const std::vector<std::string> identityCandidates =
        buildIdentityCandidates(metadata.appId, metadata.compatibilityLegacyAppIds, metadata.appName);
    return resolveExistingInstallInfo(identityCandidates, pathResolver, manifestPath, installDir) &&
           !installDir.empty();
}

std::string ResolveInstallPathForSilentRun(const ExtendedInstallationMetadata& metadata,
                                           InstallerPathResolver& pathResolver,
                                           const LaunchContext& context,
                                           bool repairMode,
                                           std::string& existingManifest) {
    if (repairMode) {
        std::string existingDir;
        if (!TryResolveRepairInstallDir(metadata, pathResolver, existingManifest, existingDir)) {
            return {};
        }
        return existingDir;
    }
    if (!context.args.defaultDestination.empty()) {
        return context.args.defaultDestination;
    }
    return pathResolver.expandEnvironmentVariables(metadata.installDefaultDir);
}

InstallServiceCallbacks BuildConsoleServiceCallbacks(CliSupport& console) {
    InstallServiceCallbacks callbacks;
    callbacks.onEvent = [&console](const InstallServiceEvent& event) {
        switch (event.type) {
            case InstallServiceEventType::Progress: {
                const std::string& display = event.currentFile.empty() ? event.folder : event.currentFile;
                const float progress = (std::max)(0.0f, (std::min)(1.0f, event.overallProgress));
                console.showInstallationProgress(display, progress);
                break;
            }
            case InstallServiceEventType::Info:
                console.showInfo(event.message);
                break;
            case InstallServiceEventType::Warning:
                console.showWarning(event.message);
                break;
            case InstallServiceEventType::Error:
                console.showError(event.message);
                break;
            case InstallServiceEventType::Status:
                if (!event.message.empty()) {
                    console.showInfo(event.message);
                }
                break;
            default:
                break;
        }
    };
    return callbacks;
}

std::string ResolveUninstallManifestPath(const ExtendedInstallationMetadata* metadata,
                                         InstallerPathResolver& resolver) {
    const std::string exePath = getCurrentExecutablePath();
    const std::string localManifest = getLocalManifestPath(exePath);
    if (!localManifest.empty() && std::filesystem::exists(PathFromUtf8(localManifest))) {
        return localManifest;
    }
    if (!metadata) {
        return {};
    }
    const std::vector<std::string> identityCandidates =
        buildIdentityCandidates(metadata->appId, metadata->compatibilityLegacyAppIds, metadata->appName);
    return resolveInstalledManifestPath(identityCandidates, exePath, resolver);
}

InstallConfig BuildUninstallConfigFromManifest(const std::string& manifestPath) {
    InstallConfig config;
    config.applicationName = L"Application";
    nlohmann::json manifest;
    if (readManifest(manifestPath, manifest)) {
        const std::string appName = manifest.value("appName", "");
        const std::string appId = manifest.value("appId", appName);
        const std::string lang = manifest.value("language", "");
        if (!appName.empty()) {
            config.applicationName = Utf8ToWide(appName);
        }
        if (!appId.empty()) {
            config.appId = Utf8ToWide(appId);
        }
        if (!lang.empty()) {
            config.languageCode = Utf8ToWide(lang);
        }
        if (manifest.contains("compatibilityLegacyAppIds") && manifest["compatibilityLegacyAppIds"].is_array()) {
            for (const auto& item : manifest["compatibilityLegacyAppIds"]) {
                if (item.is_string()) {
                    config.legacyAppIds.push_back(Utf8ToWide(item.get<std::string>()));
                }
            }
        }
    }
    return config;
}

int RunSilentInstallLikeMode(const LaunchContext& context, bool repairMode) {
    CliSupport console;
    InstallerPathResolver pathResolver;
    MetadataParser parser;
    ExtendedInstallationMetadata metadata = parser.parseExtendedEmbeddedMetadata();
    if (!parser.validateMetadata(metadata)) {
        console.showError("Invalid or corrupted installer metadata");
        return INSTALLER_EXIT_FAILED;
    }

    SetInstallerAppNameEnv(metadata.appName);
    EnsureInstallerLoggingInitialized();

    std::string existingManifest;
    std::string installPath = ResolveInstallPathForSilentRun(metadata, pathResolver, context, repairMode, existingManifest);
    if (installPath.empty()) {
        console.showError(repairMode ? "Repair target not found." : "Silent install requires a valid installation path.");
        return INSTALLER_EXIT_FAILED;
    }

    if ((metadata.installRequireAdmin || requiresAdminForInstall(installPath, metadata, pathResolver)) && !isRunningAsAdmin()) {
        if (relaunchSelfAsAdmin()) {
            return INSTALLER_EXIT_SUCCESS;
        }
        console.showError("Administrator privileges required.");
        return INSTALLER_EXIT_ADMIN_REQUIRED;
    }

    InstallServiceOptions options;
    options.installPath = installPath;
    options.installPathExplicit = true;
    options.selectedComponentIds = context.args.selectedComponents;
    options.installAllComponents = context.args.installAllComponents;
    options.writeUninstallRegistry = true;
    options.cleanupOldInstallRequested = repairMode;
    options.overrideAutoStartup = context.args.autoStartupSpecified;
    options.autoStartupEnabled = context.args.autoStartupEnabled;
    options.overrideDesktopIcons = context.args.desktopIconSpecified;
    options.desktopIconsEnabled = context.args.desktopIconEnabled;

    InstallServiceResult result = ExecuteInstallService(
        metadata, parser, pathResolver, options, BuildConsoleServiceCallbacks(console));
    console.showInstallationResult(result.success, result.errors);
    if (result.success) {
        return INSTALLER_EXIT_SUCCESS;
    }
    return result.cancelled ? INSTALLER_EXIT_CANCELLED : INSTALLER_EXIT_FAILED;
}

int RunGuiInstallLikeMode(HINSTANCE hInstance, const LaunchContext& context, bool repairMode) {
    ScopedComInit com;
    if (!com.ok()) {
        GUIHelpers::ShowErrorDialog(nullptr, L"Error", L"Failed to initialize COM.");
        return 1;
    }

    MetadataParser parser;
    ExtendedInstallationMetadata metadata = parser.parseExtendedEmbeddedMetadata();
    if (!parser.validateMetadata(metadata)) {
        GUIHelpers::ShowErrorDialog(nullptr,
                                    GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L""),
                                    GUIHelpers::GetLocalizedText(L"msg.error.metadata_invalid", L""));
        return 1;
    }

    InstallerPathResolver pathResolver;
    std::string repairManifest;
    std::string repairInstallDir;
    if (repairMode && !TryResolveRepairInstallDir(metadata, pathResolver, repairManifest, repairInstallDir)) {
        GUIHelpers::ShowErrorDialog(nullptr, GetRepairTitle(L""), L"Repair target not found.");
        return 1;
    }

    SetInstallerAppNameEnv(metadata.appName);
    EnsureInstallerLoggingInitialized();

    if (metadata.installRequireAdmin && !isRunningAsAdmin()) {
        if (relaunchSelfAsAdmin()) {
            return 0;
        }
        GUIHelpers::ShowWarningDialog(nullptr,
                                      GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L""),
                                      GUIHelpers::GetLocalizedText(L"msg.error.require_admin", L""));
        return 1;
    }

    InstallConfig config = CreateInstallConfigFromMetadata(metadata);
    config.repairMode = repairMode;
    if (repairMode) {
        config.defaultInstallPath = Utf8ToWide(repairInstallDir);
    }

    GuiResourceContext resources;
    PrepareGuiResources(hInstance, resources, true);
    const GuiResourceValidationResult validation = ValidateInstallGuiResources(resources);
    if (validation != GuiResourceValidationResult::Ok) {
        return 1;
    }
    ApplyGuiResources(resources, true);

    auto frame = std::make_unique<GUIManager>();
    frame->SetUninstallMode(false);
    frame->SetRepairMode(repairMode);
    frame->SetInstallConfig(config);

    const std::wstring title = repairMode
                                   ? GetRepairTitle(config.languageCode)
                                   : GUIHelpers::GetLocalizedText(L"msg.title.install", L"");
    return RunGuiWindow(*frame, title, resources, false, WindowSize{800, 600});
}

int RunSilentUninstallMode() {
    CliSupport console;
    InstallerPathResolver resolver;
    MetadataParser parser;
    ExtendedInstallationMetadata metadata = parser.parseExtendedEmbeddedMetadata();
    const ExtendedInstallationMetadata* metadataPtr = parser.validateMetadata(metadata) ? &metadata : nullptr;

    const std::string manifestPath = ResolveUninstallManifestPath(metadataPtr, resolver);
    if (manifestPath.empty()) {
        console.showError("Manifest not found for uninstall.");
        return INSTALLER_EXIT_FAILED;
    }

    EnsureInstallerLoggingInitialized();
    if (!isRunningAsAdmin()) {
        if (relaunchSelfAsAdmin()) {
            return INSTALLER_EXIT_SUCCESS;
        }
        console.showError("Please run the uninstaller as Administrator.");
        return INSTALLER_EXIT_ADMIN_REQUIRED;
    }

    const bool ok = uninstallFromManifest(manifestPath, resolver, console);
    return ok ? INSTALLER_EXIT_SUCCESS : INSTALLER_EXIT_FAILED;
}

int RunGuiUninstallMode(HINSTANCE hInstance) {
    ScopedComInit com;
    if (!com.ok()) {
        GUIHelpers::ShowErrorDialog(nullptr, L"Error", L"Failed to initialize COM.");
        return 1;
    }

    InstallerPathResolver resolver;
    MetadataParser parser;
    ExtendedInstallationMetadata metadata = parser.parseExtendedEmbeddedMetadata();
    const ExtendedInstallationMetadata* metadataPtr = parser.validateMetadata(metadata) ? &metadata : nullptr;

    const std::string manifestPath = ResolveUninstallManifestPath(metadataPtr, resolver);
    if (manifestPath.empty()) {
        GUIHelpers::ShowErrorDialog(nullptr, L"Error", L"Manifest not found for uninstall.");
        return 1;
    }

    EnsureInstallerLoggingInitialized();
    if (!isRunningAsAdmin()) {
        if (relaunchSelfAsAdmin()) {
            return 0;
        }
        GUIHelpers::ShowWarningDialog(nullptr,
                                      GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L""),
                                      GUIHelpers::GetLocalizedText(L"msg.error.require_admin", L""));
        return 1;
    }

    GuiResourceContext resources;
    PrepareGuiResources(hInstance, resources, false);
    const GuiResourceValidationResult validation = ValidateInstallGuiResources(resources);
    if (validation != GuiResourceValidationResult::Ok) {
        return 1;
    }
    ApplyGuiResources(resources, false);

    auto frame = std::make_unique<GUIManager>();
    frame->SetUninstallMode(true);
    frame->SetInstallConfig(BuildUninstallConfigFromManifest(manifestPath));
    return RunGuiWindow(*frame,
                        GUIHelpers::GetLocalizedText(L"msg.title.uninstall", L""),
                        resources,
                        true,
                        WindowSize{560, 350});
}

} // namespace

LaunchContext BuildLaunchContextFromCommandLine(LaunchBinary binary) {
    LaunchContext context;
    context.binary = binary;

    const std::vector<std::wstring> wideArgs = GetWideArgs();
    context.utf8Args.reserve(wideArgs.size());
    for (const auto& arg : wideArgs) {
        context.utf8Args.push_back(WideToUtf8(arg));
    }

    CliSupport console;
    std::vector<char*> argv = BuildArgv(context.utf8Args);
    context.args = console.parseInstallerArgs(static_cast<int>(context.utf8Args.size()), argv.data());

    if (HasFlag(wideArgs, std::wstring(L"--cleanup-self"))) {
        context.mode = LaunchMode::CleanupSelf;
        return context;
    }

    if (binary == LaunchBinary::Installer) {
        if (context.args.repair) {
            context.mode = context.args.silent ? LaunchMode::RepairSilent : LaunchMode::RepairGui;
        } else {
            context.mode = context.args.silent ? LaunchMode::InstallSilent : LaunchMode::InstallGui;
        }
    } else {
        context.mode = context.args.silent ? LaunchMode::UninstallSilent : LaunchMode::UninstallGui;
    }

    return context;
}

int RunLaunchContext(HINSTANCE hInstance, const LaunchContext& context) {
    EnablePerMonitorDpiAwareness();

    CliSupport console;
    if (context.args.showHelp) {
        if (context.binary == LaunchBinary::Installer) {
            console.showInstallerHelp();
        } else {
            console.showUninstallerHelp();
        }
        return INSTALLER_EXIT_SUCCESS;
    }

    if (context.binary == LaunchBinary::Installer &&
        context.args.silent &&
        context.args.repair) {
        console.showError("Silent mode does not support --repair.");
        return INSTALLER_EXIT_FAILED;
    }

    switch (context.mode) {
        case LaunchMode::CleanupSelf:
            return RunCleanupHelper(GetWideArgs());
        case LaunchMode::InstallGui:
            return RunGuiInstallLikeMode(hInstance, context, false);
        case LaunchMode::InstallSilent:
            return RunSilentInstallLikeMode(context, false);
        case LaunchMode::RepairGui:
            return RunGuiInstallLikeMode(hInstance, context, true);
        case LaunchMode::RepairSilent:
            return RunSilentInstallLikeMode(context, true);
        case LaunchMode::UninstallGui:
            return RunGuiUninstallMode(hInstance);
        case LaunchMode::UninstallSilent:
            return RunSilentUninstallMode();
        default:
            return INSTALLER_EXIT_FAILED;
    }
}

} // namespace MultiThreadedInstaller
