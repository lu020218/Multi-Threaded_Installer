#include "installer/app/launch_support.h"

#include "installer/app/single_instance.h"
#include "installer/components/component_registry.h"
#include "common/engine_defaults.h"
#include "common/installer_exit_codes.h"
#include "common/installer_logger.h"
#include "common/win32_raii.h"
#include "common/utf8_utils.h"
#include "common/version_utils.h"
#include "gui/core/gui_helpers.h"
#include "installer/platform/embedded_resources.h"
#include "installer/platform/gui_resource_loader.h"
#include "installer/state/install_manifest_store.h"
#include "installer/pipeline/install_plan_builder.h"
#include "installer/pipeline/install_service.h"
#include "installer/state/installed_instance_resolver.h"
#include "installer/platform/installer_helpers.h"
#include "installer/payload/metadata_parser.h"
#include "installer/platform/path_resolver.h"
#include "installer/state/registry_utils.h"
#include "installer/uninstall/upgrade_cleanup.h"
#include "installer/uninstall/uninstall_manager.h"

#include <Shlwapi.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>

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
        UniqueHandle parentHandle(OpenProcess(SYNCHRONIZE, FALSE, parentPid));
        if (parentHandle.valid()) {
            WaitForSingleObject(parentHandle.get(), 60000);
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

// 资源诊断默认关闭；设置环境变量 MTINSTALLER_DIAG=1 可开启（排查用）。
bool IsGuiResourceDiagnosticsEnabled() {
    wchar_t buf[8] = {};
    DWORD n = GetEnvironmentVariableW(L"MTINSTALLER_DIAG", buf, 8);
    return n == 1 && buf[0] == L'1';
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
    config.applicationName = Utf8ToWide(metadata.appProductName);
    config.appId = Utf8ToWide(metadata.appProductName);
    config.directoryName = Utf8ToWide(metadata.appProductName);
    config.version = Utf8ToWide(metadata.appVersion);
    config.defaultInstallPath = Utf8ToWide(metadata.appDefaultDir);
    config.webPageUrl = std::wstring();
    config.executableName = Utf8ToWide(metadata.appProductName + ".exe");
    config.autoStartup = EngineDefaults::kDefaultAutoStartup;
    config.desktopIcons = EngineDefaults::kDefaultDesktopShortcut;
    config.overwriteMode = false;

    uint64_t totalSize = 0;
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        totalSize += mapping.originalSize;
    }
    config.requiredDiskSpace = totalSize;
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
                 WindowSize fallbackSize,
                 const std::string& instanceKey) {
    HWND hwnd = nullptr;
    WindowSize baseSize = GetWindowSizeFromResources(uninstallMode, fallbackSize);
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

    // 单例：发布本 GUI 窗口句柄，供后续启动的同类实例查找并置顶。
    if (!instanceKey.empty()) {
        PublishGuiInstanceWindow(instanceKey, hwnd);
    }

    // 资源诊断会遍历整个 zip（数百条目），首启时与首帧抢 IO/CPU。默认关闭，
    // 仅当显式设置环境变量 MTINSTALLER_DIAG=1 时才在后台线程运行，用于排查。
    std::thread convergeDiagThread;
    if (IsGuiResourceDiagnosticsEnabled()) {
        convergeDiagThread = std::thread([]() {
            RunDeferredGuiResourceDiagnostics();
            logInstallerInfo("[GUI][RES] Deferred resource diagnostics completed.");
        });
    }

    CPaintManagerUI::MessageLoop();
    if (convergeDiagThread.joinable()) {
        convergeDiagThread.join();
    }
    logInstallerInfo("[GUI] Message loop exited.");
    if (!instanceKey.empty()) {
        ClearGuiInstanceWindow();  // 窗口关闭后撤下已发布的句柄。
    }
    CPaintManagerUI::SetResourceZip(_T(""), true);
    return 0;
}

std::string ResolveInstallPathForSilentRun(const ExtendedInstallationMetadata& metadata,
                                           InstallerPathResolver& pathResolver,
                                           const LaunchContext& context,
                                           std::string& existingManifest) {
    if (!context.args.defaultDestination.empty()) {
        return context.args.defaultDestination;
    }

    InstalledInstanceInfo installedInstance;
    if (resolveInstalledInstanceFromInstallState(metadata, pathResolver, installedInstance, nullptr) &&
        installedInstance.found &&
        !installedInstance.installDir.empty()) {
        existingManifest = installedInstance.manifestPath;
        return installedInstance.installDir;
    }
    return pathResolver.expandEnvironmentVariables(metadata.appDefaultDir);
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
    InstalledInstanceInfo installedInstance;
    if (resolveInstalledInstanceFromInstallState(*metadata, resolver, installedInstance, nullptr) &&
        !installedInstance.manifestPath.empty()) {
        return installedInstance.manifestPath;
    }
    return {};
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
    }
    return config;
}

bool ApplyPreviousOptionsForUpgrade(const ExtendedInstallationMetadata& metadata,
                                    InstallerPathResolver& resolver,
                                    InstallServiceOptions& options,
                                    std::string& installDir,
                                    bool* restoredPreviousOptions,
                                    std::string& error) {
    std::string manifestPath;
    if (restoredPreviousOptions) {
        *restoredPreviousOptions = false;
    }
    if (!ResolveUpgradeInstallFromInstallStateDetect(metadata, resolver, installDir, manifestPath, error)) {
        return false;
    }

    options.upgradeMode = true;
    options.installPath = installDir;
    options.installPathExplicit = true;

    if (manifestPath.empty()) {
        error.clear();
        logInstallerWarning("[Upgrade] Previous install manifest not found; using current package defaults.");
        return true;
    }

    PreviousInstallOptions previous;
    std::string previousOptionsError;
    if (!loadPreviousInstallOptions(manifestPath, previous, previousOptionsError)) {
        error.clear();
        logInstallerWarning("[Upgrade] Failed to load previous install options from " + manifestPath +
                            "; using current package defaults. error=" + previousOptionsError);
        return true;
    }

    options.overrideAutoStartup = true;
    options.autoStartupEnabled = previous.autoStartup;
    options.overrideDesktopIcons = true;
    options.desktopIconsEnabled = previous.desktopIcon;
    options.languageCode = previous.languageCode;
    if (restoredPreviousOptions) {
        *restoredPreviousOptions = true;
    }
    return true;
}

// 静默模式组件选择（无界面，按"注册表 + CLI 开关"决定）：required 恒选中；
// --all-components 全选；--skip-components 仅 required；显式 --component(s) 取并集；
// 否则取注册表 defaultSelected。仅保留命中注册表的 id。
std::vector<std::string> ResolveSilentComponentSelection(const CliSupport::InstallerArgs& args) {
    const auto& registry = GetComponentRegistry();
    std::vector<std::string> selected;
    auto addUnique = [&selected](const std::string& id) {
        if (std::find(selected.begin(), selected.end(), id) == selected.end()) {
            selected.push_back(id);
        }
    };
    for (const auto& spec : registry) {
        if (spec.required) {
            addUnique(spec.id);
        }
    }
    if (args.allComponents) {
        for (const auto& spec : registry) {
            addUnique(spec.id);
        }
    } else if (args.skipComponents) {
        // 仅 required（上面已加）。
    } else if (!args.selectedComponentIds.empty()) {
        for (const auto& id : args.selectedComponentIds) {
            if (FindComponentById(id)) {
                addUnique(id);
            }
        }
    } else {
        for (const auto& spec : registry) {
            if (spec.defaultSelected) {
                addUnique(spec.id);
            }
        }
    }
    return selected;
}

int RunSilentInstallLikeMode(const LaunchContext& context) {
    CliSupport console;
    InstallerPathResolver pathResolver;
    MetadataParser parser;
    ExtendedInstallationMetadata metadata = parser.parseExtendedEmbeddedMetadata();
    if (!parser.validateMetadata(metadata)) {
        console.showError("Invalid or corrupted installer metadata");
        return INSTALLER_EXIT_FAILED;
    }

    SetInstallerAppNameEnv(metadata.appProductName);
    EnsureInstallerLoggingInitialized();

    // 单例：已有安装器实例在运行时，本静默进程直接退出（如已有 GUI 实例则先置顶其窗口）。
    // 静默模式无 UI，不弹提示；用专用退出码让调用方区分“被跳过”而非“安装成功”。
    SingleInstanceGuard instanceGuard(metadata.appProductName + "|installer");
    if (!instanceGuard.acquired()) {
        instanceGuard.activateExistingWindow();
        console.showError("Another installer instance is already running; this run was skipped.");
        logInstallerInfo("[SingleInstance] Installer already running; this silent instance exits.");
        return INSTALLER_EXIT_ALREADY_RUNNING;
    }

    InstallServiceOptions options;
    std::string installPath;
    if (context.args.upgrade) {
        std::string upgradeError;
        bool restoredPreviousOptions = false;
        if (!ApplyPreviousOptionsForUpgrade(metadata,
                                            pathResolver,
                                            options,
                                            installPath,
                                            &restoredPreviousOptions,
                                            upgradeError)) {
            console.showError(upgradeError.empty() ? "Upgrade mode requires an existing installation." : upgradeError);
            return INSTALLER_EXIT_FAILED;
        }
    } else {
        InstalledInstanceInfo installedInstance;
        const bool hasInstalledInstance =
            resolveInstalledInstanceFromInstallState(metadata, pathResolver, installedInstance, nullptr) &&
            installedInstance.found;

        if (hasInstalledInstance) {
            const int versionOrder =
                compareSemanticVersion(metadata.appVersion, installedInstance.installedVersion);
            if (versionOrder <= 0) {
                console.showError("Silent install only supports upgrading to a higher version.");
                return INSTALLER_EXIT_FAILED;
            }
        }

        std::string existingManifest;
        installPath = ResolveInstallPathForSilentRun(metadata, pathResolver, context, existingManifest);
        if (installPath.empty()) {
            console.showError("Silent install requires a valid installation path.");
            return INSTALLER_EXIT_FAILED;
        }

        options.installPath = installPath;
        options.installPathExplicit = true;
        options.overrideAutoStartup = context.args.autoStartupSpecified;
        options.autoStartupEnabled = context.args.autoStartupEnabled;
        options.overrideDesktopIcons = context.args.desktopIconSpecified;
        options.desktopIconsEnabled = context.args.desktopIconEnabled;
    }
    options.writeUninstallRegistry = true;
    options.selectedComponentIds = ResolveSilentComponentSelection(context.args);

    InstallServiceResult result = ExecuteInstallService(
        metadata, parser, pathResolver, options, BuildConsoleServiceCallbacks(console));
    console.showInstallationResult(result.success, result.rebootRequired, result.errors);
    if (result.rebootRequired) {
        return INSTALLER_EXIT_REBOOT_REQUIRED;
    }
    if (result.success) {
        return INSTALLER_EXIT_SUCCESS;
    }
    return result.cancelled ? INSTALLER_EXIT_CANCELLED : INSTALLER_EXIT_FAILED;
}

int RunGuiInstallLikeMode(HINSTANCE hInstance, const LaunchContext& context) {
    ScopedComInit com;
    if (!com.ok()) {
        GUIHelpers::ShowErrorDialog(nullptr, L"Error", L"Failed to initialize COM.");
        return 1;
    }

    EnsureInstallerLoggingInitialized();

    // [Perf] 启动各阶段计时（用于定位欢迎页前的耗时分配）。
    using PerfClock = std::chrono::steady_clock;
    auto perfMs = [](PerfClock::time_point a, PerfClock::time_point b) {
        return std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count());
    };

    const auto tParseStart = PerfClock::now();
    MetadataParser parser;
    // GUI 启动只需文件夹/标量元数据；跳过 37641 条 fileIndex（安装 worker 会自行全量重解析）。
    ExtendedInstallationMetadata metadata =
        parser.parseExtendedEmbeddedMetadata(/*deferFileIndex=*/true);
    const auto tParseEnd = PerfClock::now();
    logInstallerInfo("[GUI][Perf] parseExtendedEmbeddedMetadata=" +
                     perfMs(tParseStart, tParseEnd) + "ms");
    if (!parser.validateMetadata(metadata)) {
        GUIHelpers::ShowErrorDialog(nullptr,
                                    GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L""),
                                    GUIHelpers::GetLocalizedText(L"msg.error.metadata_invalid", L""));
        return 1;
    }

    // 单例：已有安装器实例在运行时本进程退出。若已有实例为 GUI 则置顶其窗口；
    // 若已有实例为静默安装（无窗口可置顶），则弹提示框告知用户后退出。
    const std::string instanceKey = metadata.appProductName + "|installer";
    SingleInstanceGuard instanceGuard(instanceKey);
    if (!instanceGuard.acquired()) {
        if (!instanceGuard.activateExistingWindow()) {
            GUIHelpers::ShowWarningDialog(
                nullptr,
                GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L"提示"),
                GUIHelpers::GetLocalizedText(L"msg.error.already_running",
                                             L"已有一个安装正在进行，请稍候。"));
        }
        logInstallerInfo("[SingleInstance] Installer already running; this GUI instance exits.");
        return INSTALLER_EXIT_ALREADY_RUNNING;
    }

    InstallerPathResolver pathResolver;
    InstallServiceOptions upgradeOptions;
    std::string upgradeInstallDir;
    bool upgradeMode = context.args.upgrade;
    bool overwriteMode = false;

    const auto tInstanceStart = PerfClock::now();
    if (upgradeMode) {
        std::string upgradeError;
        bool restoredPreviousOptions = false;
        if (!ApplyPreviousOptionsForUpgrade(metadata,
                                            pathResolver,
                                            upgradeOptions,
                                            upgradeInstallDir,
                                            &restoredPreviousOptions,
                                            upgradeError)) {
            GUIHelpers::ShowErrorDialog(nullptr,
                                        GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L""),
                                        Utf8ToWide(upgradeError.empty()
                                                       ? "Upgrade mode requires an existing installation."
                                                       : upgradeError));
            return 1;
        }
        overwriteMode = true;
    } else {
        InstalledInstanceInfo installedInstance;
        overwriteMode =
            resolveInstalledInstanceFromInstallState(metadata, pathResolver, installedInstance, nullptr) &&
            installedInstance.found;
        if (overwriteMode && !installedInstance.installDir.empty()) {
            upgradeInstallDir = installedInstance.installDir;
        }
    }
    const auto tInstanceEnd = PerfClock::now();
    logInstallerInfo("[GUI][Perf] resolveInstalledInstance=" +
                     perfMs(tInstanceStart, tInstanceEnd) + "ms");

    InstallConfig config = CreateInstallConfigFromMetadata(metadata);
    config.overwriteMode = overwriteMode;
    if (!upgradeInstallDir.empty()) {
        config.defaultInstallPath = Utf8ToWide(upgradeInstallDir);
    }
    bool restoredPreviousOptions = upgradeOptions.overrideAutoStartup || upgradeOptions.overrideDesktopIcons ||
                                   !upgradeOptions.languageCode.empty();
    if (upgradeMode && restoredPreviousOptions) {
        config.autoStartup = upgradeOptions.autoStartupEnabled;
        config.desktopIcons = upgradeOptions.desktopIconsEnabled;
        if (!upgradeOptions.languageCode.empty()) {
            config.languageCode = Utf8ToWide(upgradeOptions.languageCode);
        }
    }

    // Deferred: only needed by child processes spawned during installation.
    SetInstallerAppNameEnv(metadata.appProductName);

    GuiResourceContext resources;
    const auto tZipStart = PerfClock::now();
    PrepareGuiResources(hInstance, resources, true);
    const auto tZipEnd = PerfClock::now();
    logInstallerInfo("[GUI][Perf] PrepareGuiResources(RES_ZIP read)=" +
                     perfMs(tZipStart, tZipEnd) + "ms");
    const GuiResourceValidationResult validation = ValidateInstallGuiResources(resources);
    if (validation != GuiResourceValidationResult::Ok) {
        return 1;
    }
    ApplyGuiResources(resources, true);

    auto frame = std::make_unique<GUIManager>();
    frame->SetUninstallMode(false);
    frame->SetOverwriteMode(overwriteMode);
    frame->SetInstallConfig(config);
    frame->SetInstallMetadata(metadata);
    if (upgradeMode) {
        const bool autoStartAutoRun = restoredPreviousOptions
                                          ? upgradeOptions.autoStartupEnabled
                                          : config.autoStartup;
        const bool autoStartDesktopIcons = restoredPreviousOptions
                                               ? upgradeOptions.desktopIconsEnabled
                                               : config.desktopIcons;
        const std::wstring autoStartLanguage = restoredPreviousOptions
                                                   ? Utf8ToWide(upgradeOptions.languageCode)
                                                   : config.languageCode;
        frame->SetAutoStartInstallRequest(Utf8ToWide(upgradeInstallDir),
                                          autoStartAutoRun,
                                          autoStartDesktopIcons,
                                          autoStartLanguage,
                                          true);
    }

    const std::wstring title = config.applicationName.empty() ? L"Installer" : config.applicationName;
    return RunGuiWindow(*frame, title, resources, false, WindowSize{800, 600}, instanceKey);
}

int RunSilentUninstallMode(const LaunchContext& context) {
    CliSupport console;
    InstallerPathResolver resolver;
    MetadataParser parser;
    ExtendedInstallationMetadata metadata = parser.parseExtendedEmbeddedMetadata();
    const ExtendedInstallationMetadata* metadataPtr = parser.validateMetadata(metadata) ? &metadata : nullptr;

    UninstallContext uninstallContext;
    ResolveUninstallContext(metadataPtr, resolver, context.args.uninstallManifestPath, uninstallContext);
    if (!uninstallContext.manifestReadable && !uninstallContext.fallbackAllowed) {
        console.showError(uninstallContext.errorMessage.empty()
                              ? "Manifest not found for uninstall."
                              : uninstallContext.errorMessage);
        return INSTALLER_EXIT_FAILED;
    }

    EnsureInstallerLoggingInitialized();
    if (!isRunningAsAdmin()) {
        std::vector<std::wstring> extraArgs;
        if (uninstallContext.manifestReadable && !uninstallContext.manifestPath.empty() &&
            context.args.uninstallManifestPath.empty()) {
            extraArgs.push_back(L"--uninstall-manifest");
            extraArgs.push_back(Utf8ToWide(uninstallContext.manifestPath));
        }
        if (extraArgs.empty() ? relaunchSelfAsAdmin() : relaunchSelfAsAdminWithArguments(extraArgs)) {
            return INSTALLER_EXIT_SUCCESS;
        }
        console.showError("Please run the uninstaller as Administrator.");
        return INSTALLER_EXIT_ADMIN_REQUIRED;
    }

    // 单例：放在提权 relaunch 之后，确保只有最终（已提权）实例持有互斥量，避免父子进程互相误判。
    const std::string uninstallProduct =
        metadataPtr ? metadataPtr->appProductName : uninstallContext.appName;
    SingleInstanceGuard instanceGuard(uninstallProduct + "|uninstaller");
    if (!instanceGuard.acquired()) {
        instanceGuard.activateExistingWindow();
        console.showError("Another uninstaller instance is already running; this run was skipped.");
        logInstallerInfo("[SingleInstance] Uninstaller already running; this silent instance exits.");
        return INSTALLER_EXIT_ALREADY_RUNNING;
    }

    const bool ok = ExecuteUninstallFromContext(uninstallContext, metadataPtr, resolver, console);
    return ok ? INSTALLER_EXIT_SUCCESS : INSTALLER_EXIT_FAILED;
}

int RunGuiUninstallMode(HINSTANCE hInstance, const LaunchContext& context) {
    ScopedComInit com;
    if (!com.ok()) {
        GUIHelpers::ShowErrorDialog(nullptr, L"Error", L"Failed to initialize COM.");
        return 1;
    }

    InstallerPathResolver resolver;
    MetadataParser parser;
    ExtendedInstallationMetadata metadata = parser.parseExtendedEmbeddedMetadata();
    const ExtendedInstallationMetadata* metadataPtr = parser.validateMetadata(metadata) ? &metadata : nullptr;

    UninstallContext uninstallContext;
    ResolveUninstallContext(metadataPtr, resolver, context.args.uninstallManifestPath, uninstallContext);
    if (!uninstallContext.manifestReadable && !uninstallContext.fallbackAllowed) {
        GUIHelpers::ShowErrorDialog(nullptr,
                                    L"Error",
                                    Utf8ToWide(uninstallContext.errorMessage.empty()
                                                   ? "Manifest not found for uninstall."
                                                   : uninstallContext.errorMessage));
        return 1;
    }

    EnsureInstallerLoggingInitialized();
    if (!isRunningAsAdmin()) {
        std::vector<std::wstring> extraArgs;
        if (uninstallContext.manifestReadable && !uninstallContext.manifestPath.empty() &&
            context.args.uninstallManifestPath.empty()) {
            extraArgs.push_back(L"--uninstall-manifest");
            extraArgs.push_back(Utf8ToWide(uninstallContext.manifestPath));
        }
        if (extraArgs.empty() ? relaunchSelfAsAdmin() : relaunchSelfAsAdminWithArguments(extraArgs)) {
            return 0;
        }
        GUIHelpers::ShowWarningDialog(nullptr,
                                      GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L""),
                                      GUIHelpers::GetLocalizedText(L"msg.error.require_admin", L""));
        return 1;
    }

    // 单例：放在提权 relaunch 之后。已有卸载器实例在运行时退出（如为 GUI 则先置顶其窗口）。
    const std::string uninstallInstanceKey =
        (metadataPtr ? metadataPtr->appProductName : uninstallContext.appName) + "|uninstaller";
    SingleInstanceGuard instanceGuard(uninstallInstanceKey);
    if (!instanceGuard.acquired()) {
        if (!instanceGuard.activateExistingWindow()) {
            GUIHelpers::ShowWarningDialog(
                nullptr,
                GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L"提示"),
                GUIHelpers::GetLocalizedText(L"msg.error.already_running",
                                             L"已有一个卸载正在进行，请稍候。"));
        }
        logInstallerInfo("[SingleInstance] Uninstaller already running; this GUI instance exits.");
        return INSTALLER_EXIT_ALREADY_RUNNING;
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
    if (metadataPtr) {
        frame->SetInstallMetadata(*metadataPtr);
    }
    InstallConfig uninstallConfig = uninstallContext.manifestReadable
                                        ? BuildUninstallConfigFromManifest(uninstallContext.manifestPath)
                                        : InstallConfig{};
    if (!uninstallContext.appName.empty()) {
        uninstallConfig.applicationName = Utf8ToWide(uninstallContext.appName);
    }
    if (!uninstallContext.appId.empty()) {
        uninstallConfig.appId = Utf8ToWide(uninstallContext.appId);
    }
    frame->SetInstallConfig(uninstallConfig);
    frame->SetUninstallContext(uninstallContext);
    return RunGuiWindow(*frame,
                        uninstallConfig.applicationName.empty() ? L"Uninstaller" : uninstallConfig.applicationName,
                        resources,
                        true,
                        WindowSize{560, 350},
                        uninstallInstanceKey);
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

    if (binary == LaunchBinary::Installer) {
        context.mode = context.args.silent ? LaunchMode::InstallSilent : LaunchMode::InstallGui;
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

    if (context.binary != LaunchBinary::Installer && context.args.upgrade) {
        console.showError("--upgrade is only supported by installer.exe");
        return INSTALLER_EXIT_FAILED;
    }

    const std::vector<std::wstring> wideArgs = GetWideArgs();
    if (HasFlag(wideArgs, std::wstring(L"--cleanup-self"))) {
        return RunCleanupHelper(wideArgs);
    }
    switch (context.mode) {
        case LaunchMode::InstallGui:
            return RunGuiInstallLikeMode(hInstance, context);
        case LaunchMode::InstallSilent:
            return RunSilentInstallLikeMode(context);
        case LaunchMode::UninstallGui:
            return RunGuiUninstallMode(hInstance, context);
        case LaunchMode::UninstallSilent:
            return RunSilentUninstallMode(context);
        default:
            return INSTALLER_EXIT_FAILED;
    }
}

} // namespace MultiThreadedInstaller

