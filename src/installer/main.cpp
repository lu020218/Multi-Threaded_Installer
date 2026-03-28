#include "installer/metadata_parser.h"
#include "installer/thread_pool_manager.h"
#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include "installer/console_interface.h"
#include "installer/path_resolver.h"
#include "installer/installer_helpers.h"
#include "installer/install_service.h"
#include "common/installer_logger.h"
#include "common/installer_exit_codes.h"
#include "common/utf8_utils.h"
#include "installer/registry_utils.h"
#include "installer/uninstall_manager.h"

#include "gui/gui_manager.h"
#include "gui/gui_helpers.h"
#include "installer/gui_resource_loader.h"
#include "installer/embedded_resources.h"
#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include <iostream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <memory>
#include <io.h>
#include <fcntl.h>

using namespace MultiThreadedInstaller;

int runConsoleInstaller(int argc, char* argv[]);

namespace {

struct FileWriter {
    std::string path;
    uint64_t start;
    uint64_t end;
    std::mutex mutex;
};

struct BlockInfo {
    uint32_t blockId;
    uint64_t compressedOffset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint64_t startOffset;
};

struct BlockMetaHeader {
    uint32_t offset;
    uint32_t compressedSize;
    uint32_t originalSize;
    uint32_t checksum;
};

struct BlockSegment {
    size_t fileIndex;
    uint64_t blockOffset;
    uint64_t fileOffset;
    uint64_t size;
};

struct FolderTiming {
    double totalSec = 0.0;
    double readSec = 0.0;
    double decompressSec = 0.0;
    double writeSec = 0.0;
    double processSec = 0.0;
    bool indexed = false;
    std::string folderName;
};

#ifdef _WIN32
static void ensureConsoleVisible() {
    HWND consoleWnd = GetConsoleWindow();
    if (!consoleWnd) {
        AllocConsole();
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);
    } else {
        ShowWindow(consoleWnd, SW_SHOW);
    }
}

static void hideConsoleWindow() {
    HWND consoleWnd = GetConsoleWindow();
    if (consoleWnd) {
        ShowWindow(consoleWnd, SW_HIDE);
    }
}
#endif

static std::string ToLowerAsciiCopy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

#ifdef _WIN32
static std::wstring ToLowerWideCopy(const std::wstring& value) {
    std::wstring lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return lowered;
}
#endif

template <typename StringT, typename CharT, typename LowerFn>
static bool hasFlagImpl(int argc, CharT** argv, const StringT& flag, LowerFn lowerFn) {
    StringT target = lowerFn(flag);
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) {
            continue;
        }
        StringT current = argv[i];
        current = lowerFn(current);
        if (current == target) {
            return true;
        }
    }
    return false;
}

static bool hasFlag(int argc, char* argv[], const std::string& flag) {
    return hasFlagImpl(argc, argv, flag, ToLowerAsciiCopy);
}

#ifdef _WIN32
static bool hasFlagWide(int argc, wchar_t** argv, const std::wstring& flag) {
    return hasFlagImpl(argc, argv, flag, ToLowerWideCopy);
}

static std::wstring getWideArgValue(int argc, wchar_t** argv, const std::wstring& flag) {
    const std::wstring loweredFlag = ToLowerWideCopy(flag);
    for (int i = 1; i + 1 < argc; ++i) {
        if (!argv[i]) {
            continue;
        }
        std::wstring current = ToLowerWideCopy(argv[i]);
        if (current == loweredFlag) {
            return argv[i + 1] ? std::wstring(argv[i + 1]) : std::wstring();
        }
    }
    return {};
}

static std::vector<std::wstring> collectWideArgValues(int argc,
                                                      wchar_t** argv,
                                                      const std::wstring& flag) {
    std::vector<std::wstring> values;
    const std::wstring loweredFlag = ToLowerWideCopy(flag);
    for (int i = 1; i + 1 < argc; ++i) {
        if (!argv[i]) {
            continue;
        }
        std::wstring current = ToLowerWideCopy(argv[i]);
        if (current == loweredFlag && argv[i + 1]) {
            values.emplace_back(argv[i + 1]);
            ++i;
        }
    }
    return values;
}

static bool removePathWithRetry(const std::filesystem::path& path, bool recursive) {
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
}

static int runCleanupHelperWithWideArgs(int argc, wchar_t** argv) {
    std::wstring parentPidValue = getWideArgValue(argc, argv, L"--cleanup-parent-pid");
    DWORD parentPid = 0;
    if (!parentPidValue.empty()) {
        parentPid = static_cast<DWORD>(std::wcstoul(parentPidValue.c_str(), nullptr, 10));
    }

    if (parentPid != 0) {
        HANDLE parentHandle = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
        if (parentHandle) {
            WaitForSingleObject(parentHandle, 60000);
            CloseHandle(parentHandle);
        } else {
            Sleep(1000);
        }
    }

    const std::wstring cleanupExe = getWideArgValue(argc, argv, L"--cleanup-exe");
    const std::wstring cleanupManifest = getWideArgValue(argc, argv, L"--cleanup-manifest");
    const std::vector<std::wstring> cleanupRoots =
        collectWideArgValues(argc, argv, L"--cleanup-root");

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

static int runConsoleInstallerWithWideArgs() {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvW) {
        return runConsoleInstaller(0, nullptr);
    }

    std::vector<std::string> utf8Args;
    utf8Args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        utf8Args.push_back(WideToUtf8(argvW[i]));
    }
    LocalFree(argvW);

    std::vector<char*> argv;
    argv.reserve(utf8Args.size() + 1);
    for (auto& arg : utf8Args) {
        argv.push_back(arg.empty() ? const_cast<char*>("") : &arg[0]);
    }
    argv.push_back(nullptr);

    return runConsoleInstaller(static_cast<int>(utf8Args.size()), argv.data());
}
#endif

static void EnsureInstallerLoggingInitialized() {
    static std::once_flag once;
    std::call_once(once, []() { initializeInstallerLogging(); });
}

static void LogVerboseInfo(bool verboseLogs, const std::string& message) {
    if (verboseLogs) {
        std::cout << message << std::endl;
    }
    logInstallerInfo(message);
}

static void LogVerboseError(bool verboseLogs, const std::string& message) {
    if (verboseLogs) {
        std::cerr << message << std::endl;
    }
    logInstallerError(message);
}

static std::string deriveAppNameFromExePath(const std::string& exePath,
                                            const std::string& fallback = {}) {
    if (!exePath.empty()) {
        std::filesystem::path exeDir = PathFromUtf8(exePath).parent_path();
        if (!exeDir.empty()) {
            std::string appName = Utf8FromPath(exeDir.filename());
            if (!appName.empty()) {
                return appName;
            }
        }

        std::filesystem::path exeName = PathFromUtf8(exePath).filename();
        std::string stem = Utf8FromPath(exeName.stem());
        if (!stem.empty()) {
            return stem;
        }
    }
    return fallback;
}

static bool startsWithNoCase(const std::string& value, const std::string& prefix) {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

static bool isPathUnderBase(const std::string& path, const std::string& base) {
    if (path.empty() || base.empty()) {
        return false;
    }
    std::string normalizedPath = normalizePathForCompare(path);
    std::string normalizedBase = normalizePathForCompare(base);
    if (normalizedBase.empty()) {
        return false;
    }
    if (normalizedBase.back() != '\\') {
        normalizedBase.push_back('\\');
    }
    if (normalizedPath == normalizedBase.substr(0, normalizedBase.size() - 1)) {
        return true;
    }
    return startsWithNoCase(normalizedPath, normalizedBase);
}

static bool registryPathRequiresAdminLocal(const std::string& path) {
    return startsWithNoCase(path, "HKEY_LOCAL_MACHINE") ||
           startsWithNoCase(path, "HKLM");
}

static bool requiresAdminForUninstall(const std::string& manifestPath,
                                      const std::vector<std::string>& identityCandidates,
                                      InstallerPathResolver& resolver) {
#ifdef _WIN32
    nlohmann::json manifest;
    if (readManifest(manifestPath, manifest)) {
        const std::string installDir = manifest.value("installDir", "");
        const std::string programFiles = resolver.expandEnvironmentVariables("%ProgramFiles%");
        const std::string programFilesX86 = resolver.expandEnvironmentVariables("%ProgramFiles(x86)%");
        const std::string programData = resolver.expandEnvironmentVariables("%ProgramData%");
        if (isPathUnderBase(installDir, programFiles) ||
            isPathUnderBase(installDir, programFilesX86) ||
            isPathUnderBase(installDir, programData)) {
            return true;
        }

        if (manifest.contains("installState") && manifest["installState"].is_object()) {
            const auto& installState = manifest["installState"];
            const std::string registryPath = installState.value("registryPath", "");
            if (registryPathRequiresAdminLocal(registryPath)) {
                return true;
            }
        }

        if (manifest.contains("registry") && manifest["registry"].is_array()) {
            for (const auto& entry : manifest["registry"]) {
                if (entry.is_object() &&
                    registryPathRequiresAdminLocal(entry.value("path", ""))) {
                    return true;
                }
            }
        }
    }

    for (const auto& identity : identityCandidates) {
        if (identity.empty()) {
            continue;
        }
        const std::string keyName = sanitizeRegistryKeyName(identity);
        const std::string hklmPath =
            "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;
        std::string value;
        if (readRegistryStringValue(hklmPath, "UninstallString", value) ||
            readRegistryStringValue(hklmPath, "InstallLocation", value)) {
            return true;
        }
    }
#else
    (void)manifestPath;
    (void)identityCandidates;
    (void)resolver;
#endif
    return false;
}

static bool ensureAdminForUninstallConsole(ConsoleInterface& console) {
#ifdef _WIN32
    if (isRunningAsAdmin()) {
        return true;
    }
    console.showInfo("Administrator privileges required for uninstall.");
    if (relaunchSelfAsAdmin()) {
        return false;
    }
    console.showError("Please run the uninstaller as Administrator.");
    return false;
#else
    (void)console;
    return true;
#endif
}

static bool hasLocalManifest(const std::string& exePath) {
    std::string localManifest = getLocalManifestPath(exePath);
    return !localManifest.empty() && std::filesystem::exists(PathFromUtf8(localManifest));
}

#ifdef _WIN32
static void setInstallerAppNameEnv(const std::string& appName) {
    if (appName.empty()) {
        return;
    }
    std::wstring appNameWide = Utf8ToWide(appName);
    if (!appNameWide.empty()) {
        SetEnvironmentVariableW(L"MTINSTALLER_APPNAME", appNameWide.c_str());
    }
}

static void applyDebugMode(bool debugMode) {
    if (debugMode) {
        ensureConsoleVisible();
        SetEnvironmentVariableW(L"MTINSTALLER_DEBUG", L"1");
    } else {
        SetEnvironmentVariableW(L"MTINSTALLER_DEBUG", nullptr);
    }
}

static void hideConsoleWhenNotDebug(bool debugMode) {
    if (!debugMode) {
        hideConsoleWindow();
    }
}
#endif

static void EnablePerMonitorDpiAwareness() {
#ifdef _WIN32
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
#endif
}

static int GetSystemDpi() {
#ifdef _WIN32
    HDC screen = GetDC(NULL);
    if (!screen) {
        return 96;
    }
    int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    if (dpi <= 0) {
        return 96;
    }
    return dpi;
#else
    return 96;
#endif
}

#ifdef _WIN32
static UINT GetDpiForWindowSafe(HWND hwnd) {
    typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (fn && hwnd) {
            return fn(hwnd);
        }
    }
    return static_cast<UINT>(GetSystemDpi());
}

static void AdjustWindowForDpi(HWND hwnd, int baseWidth, int baseHeight) {
    if (!hwnd) {
        return;
    }
    UINT dpi = GetDpiForWindowSafe(hwnd);
    float scale = static_cast<float>(dpi) / 96.0f;
    int width = static_cast<int>(baseWidth * scale);
    int height = static_cast<int>(baseHeight * scale);
    ::SetWindowPos(hwnd, NULL, 0, 0, width, height,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

static int RunGuiWindow(GUIManager& frame,
                        const std::wstring& title,
                        const GuiResourceContext& context,
                        bool uninstallMode,
                        WindowSize fallbackSize,
                        bool verboseLogs) {
    HWND hwnd = NULL;
    WindowSize baseSize = GetWindowSizeFromResources(
        context.useZip, context.resourcePath, context.skinsPath, uninstallMode, fallbackSize);

    try {
        LogVerboseInfo(verboseLogs, "[GUI] About to call Create().");
        hwnd = frame.Create(
            NULL, title.c_str(), UI_WNDSTYLE_FRAME, 0L, 0, 0, baseSize.width, baseSize.height);
    } catch (const std::exception& e) {
        LogVerboseError(verboseLogs, std::string("[GUI] Exception during Create(): ") + e.what());
        return 1;
    } catch (...) {
        LogVerboseError(verboseLogs, "[GUI] Unknown exception during Create().");
        return 1;
    }

    if (hwnd == NULL) {
        LogVerboseError(verboseLogs,
                        "[GUI] Create() returned NULL. GetLastError()=" +
                            std::to_string(GetLastError()));
        return 1;
    }

#ifdef _WIN32
    AdjustWindowForDpi(hwnd, baseSize.width, baseSize.height);
#endif

    frame.CenterWindow();
    LogVerboseInfo(verboseLogs, "[GUI] Centered window.");
    frame.ShowWindow(true);
    LogVerboseInfo(verboseLogs, "[GUI] Showed window, entering message loop.");
    CPaintManagerUI::MessageLoop();
    LogVerboseInfo(verboseLogs, "[GUI] Message loop exited.");
    CPaintManagerUI::SetResourceZip(_T(""), true);
    return 0;
}

#endif

// NOTE: Comment text normalized to avoid encoding mojibake.
InstallConfig createInstallConfigFromMetadata(const ExtendedInstallationMetadata& metadata) {
    InstallConfig config;
    config.applicationName = Utf8ToWide(metadata.applicationName);
    config.appId = Utf8ToWide(resolveEffectiveAppId(metadata.appId, metadata.applicationName));
    config.directoryName =
        Utf8ToWide(resolveEffectiveDirectoryName(metadata.directoryName, metadata.applicationName));
    config.legacyAppIds.reserve(metadata.legacyAppIds.size());
    for (const auto& legacyId : metadata.legacyAppIds) {
        config.legacyAppIds.push_back(Utf8ToWide(legacyId));
    }
    config.version = Utf8ToWide(metadata.configVersion);
    config.defaultInstallPath = Utf8ToWide(metadata.defaultInstallDir);
    for (const auto& entry : metadata.registry) {
        if (entry.key == "InstallDir") {
            config.registryPath = Utf8ToWide(entry.path);
            config.registryKey = Utf8ToWide(entry.key);
            break;
        }
    }
    config.logoResourceId = L"logo.png"; // NOTE: Comment text normalized to avoid encoding mojibake.
    config.licenseText = L""; // NOTE: Comment text normalized to avoid encoding mojibake.
    config.webPageUrl = Utf8ToWide(metadata.webPageUrl);
    config.executableName = Utf8ToWide(metadata.applicationName + ".exe");
    config.autoStartup = metadata.autoStartup;
    config.desktopIcons = metadata.desktopIcons;

    // NOTE: Comment text normalized to avoid encoding mojibake.
    uint64_t totalSize = 0;
    for (const auto& mapping : metadata.extendedMappings) {
        totalSize += mapping.originalSize;
    }
    config.requiredDiskSpace = totalSize;
    config.installDirectoryAppendName = true;
    for (const auto& mapping : metadata.extendedMappings) {
        if (mapping.targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY) {
            config.installDirectoryAppendName = mapping.appendDirectoryName;
            break;
        }
    }

    return config;
}

} // namespace

int runConsoleInstaller(int argc, char* argv[]) {
    ConsoleInterface console;
    auto startTime = std::chrono::steady_clock::now();
    InstallerPathResolver pathResolver;
    ExtendedInstallationMetadata metadata;

    try {
    // NOTE: Comment text normalized to avoid encoding mojibake.
    auto args = console.parseInstallerArgs(argc, argv);
    if (!args.uninstall) {
        std::filesystem::path exePath = PathFromUtf8(getCurrentExecutablePath());
        std::string exeName = Utf8FromPath(exePath.filename());
        std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
        if (exeName == "uninstall.exe") {
            args.uninstall = true;
        }
    }

    if (args.showHelp) {
        console.showInstallerHelp();
        return INSTALLER_EXIT_SUCCESS;
    }

    if (args.uninstall) {
        console.showInfo("Starting uninstall process...");
#ifdef _WIN32
        if (!isRunningAsAdmin()) {
            if (relaunchSelfAsAdmin()) {
                return INSTALLER_EXIT_SUCCESS;
            }
            console.showError("Please run the uninstaller as Administrator.");
            return INSTALLER_EXIT_ADMIN_REQUIRED;
        }
#endif
        std::string exePath = getCurrentExecutablePath();
        if (hasLocalManifest(exePath)) {
            std::string localManifest = getLocalManifestPath(exePath);
            bool ok = uninstallFromManifest(localManifest, pathResolver, console);
            return ok ? INSTALLER_EXIT_SUCCESS : INSTALLER_EXIT_FAILED;
        }
        std::string fallbackAppName = deriveAppNameFromExePath(exePath);
#ifdef _WIN32
        setInstallerAppNameEnv(fallbackAppName);
#endif
        EnsureInstallerLoggingInitialized();
        if (!fallbackAppName.empty()) {
            std::vector<std::string> fallbackCandidates{fallbackAppName};
            std::string manifestPath = resolveInstalledManifestPath(
                fallbackCandidates, exePath, pathResolver);
            if (!manifestPath.empty()) {
                bool ok = uninstallFromManifest(manifestPath, pathResolver, console);
                return ok ? INSTALLER_EXIT_SUCCESS : INSTALLER_EXIT_FAILED;
            }
        }
        MetadataParser parser;
        metadata = parser.parseExtendedEmbeddedMetadata();
        if (parser.validateMetadata(metadata)) {
            std::vector<std::string> identityCandidates =
                buildIdentityCandidates(metadata.appId, metadata.legacyAppIds, metadata.applicationName);
            std::string manifestPath = resolveInstalledManifestPath(
                identityCandidates, exePath, pathResolver);
            if (!manifestPath.empty()) {
                bool ok = uninstallFromManifest(manifestPath, pathResolver, console);
                return ok ? INSTALLER_EXIT_SUCCESS : INSTALLER_EXIT_FAILED;
            }
        }
        console.showError("Manifest not found for uninstall");
        return INSTALLER_EXIT_FAILED;
    }

    console.showInfo("Starting installation process...");

    // NOTE: Comment text normalized to avoid encoding mojibake.
    MetadataParser parser;
    metadata = parser.parseExtendedEmbeddedMetadata();

    if (!parser.validateMetadata(metadata)) {
        console.showError("Invalid or corrupted installer metadata");
        return INSTALLER_EXIT_FAILED;
    }

#ifdef _WIN32
    setInstallerAppNameEnv(metadata.applicationName);
#endif

#ifdef _WIN32
    if (metadata.requireAdmin && !isRunningAsAdmin()) {
        console.showError("Administrator privileges required by configuration.");
        if (relaunchSelfAsAdmin()) {
            return INSTALLER_EXIT_SUCCESS;
        }
        console.showError("Please run the installer as Administrator.");
        return INSTALLER_EXIT_ADMIN_REQUIRED;
    }
#endif

    EnsureInstallerLoggingInitialized();

    console.showInfo("Found " + std::to_string(metadata.folderCount) + " folders to install");
    console.showInfo("Application: " + metadata.applicationName);

    std::string userSelectedPath;
    bool installPathExplicit = false;
    if (args.defaultDestination.empty() && !args.silent) {
        console.showInstallerMenu();

        std::string defaultPath = pathResolver.expandEnvironmentVariables(metadata.defaultInstallDir);
        console.showInfo("Suggested installation directory: " + defaultPath);

        std::cout << "Enter installation directory (or press Enter to use default): ";
        std::getline(std::cin, userSelectedPath);

        if (userSelectedPath.empty()) {
            userSelectedPath = defaultPath;
        } else {
            installPathExplicit = true;
        }

        console.showInfo("Installing to: " + userSelectedPath);
    } else if (!args.defaultDestination.empty()) {
        userSelectedPath = args.defaultDestination;
        installPathExplicit = true;
    }

    if (args.silent && userSelectedPath.empty()) {
        userSelectedPath = pathResolver.expandEnvironmentVariables(metadata.defaultInstallDir);
        if (userSelectedPath.empty()) {
            console.showError("Silent install requires a valid default install directory.");
            return INSTALLER_EXIT_FAILED;
        }
    }

#ifdef _WIN32
    std::string resolvedInstallRoot = pathResolver.resolveFinalPath(
        userSelectedPath,
        SpecialDirectoryType::INSTALL_DIRECTORY,
        resolveEffectiveDirectoryName(metadata.directoryName, metadata.applicationName)
    );
    std::string adminCheckPath = resolvedInstallRoot.empty() ? userSelectedPath : resolvedInstallRoot;
    if (!adminCheckPath.empty() &&
        requiresAdminForInstall(adminCheckPath, metadata, pathResolver) &&
        !isRunningAsAdmin()) {
        console.showError("Administrator privileges required for selected installation path.");
        if (relaunchSelfAsAdmin()) {
            return INSTALLER_EXIT_SUCCESS;
        }
        console.showError("Please run the installer as Administrator.");
        return INSTALLER_EXIT_ADMIN_REQUIRED;
    }
#endif

    InstallServiceCallbacks serviceCallbacks;
    serviceCallbacks.onEvent = [&console](const InstallServiceEvent& event) {
        switch (event.type) {
            case InstallServiceEventType::Progress: {
                const std::string& display = event.currentFile.empty() ? event.folder : event.currentFile;
                float progress = std::max(0.0f, std::min(1.0f, event.overallProgress));
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

    InstallServiceOptions serviceOptions;
    serviceOptions.installPath = userSelectedPath;
    serviceOptions.installPathExplicit = installPathExplicit;
    serviceOptions.selectedComponentIds = args.selectedComponents;
    serviceOptions.installAllComponents = args.installAllComponents;
    serviceOptions.writeUninstallRegistry = true;

    InstallServiceResult serviceResult = ExecuteInstallService(
        metadata,
        parser,
        pathResolver,
        serviceOptions,
        serviceCallbacks);

    console.showInstallationResult(serviceResult.success, serviceResult.errors);

    {
        std::ostringstream timingSummary;
        timingSummary << std::fixed << std::setprecision(2)
                      << "Timing summary: indexed read "
                      << serviceResult.timing.indexedReadSec
                      << "s, indexed decompress "
                      << serviceResult.timing.indexedDecompressSec
                      << "s, indexed write "
                      << serviceResult.timing.indexedWriteSec
                      << "s, legacy total "
                      << serviceResult.timing.legacyTotalSec << "s";
        std::cout << timingSummary.str() << std::endl;
        logInstallerInfo(std::string("[CLI] ") + timingSummary.str());
    }

    for (const auto& timing : serviceResult.timing.folderTimings) {
        std::ostringstream timingLine;
        timingLine << std::fixed << std::setprecision(2);
        if (timing.indexed) {
            timingLine << "Timing (indexed) " << timing.folderName
                       << ": total " << timing.totalSec
                       << "s, read " << timing.readSec
                       << "s, decompress " << timing.decompressSec
                       << "s, write " << timing.writeSec << "s";
        } else {
            timingLine << "Timing (legacy) " << timing.folderName
                       << ": total " << timing.totalSec
                       << "s, read " << timing.readSec
                       << "s, decompress " << timing.decompressSec
                       << "s, write " << timing.writeSec
                       << "s, process " << timing.processSec << "s";
        }
        std::cout << timingLine.str() << std::endl;
        logInstallerInfo(std::string("[CLI] ") + timingLine.str());
    }

    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    {
        std::ostringstream totalLine;
        totalLine << std::fixed << std::setprecision(2)
                  << "Total time: " << elapsed.count() << " seconds";
        std::cout << totalLine.str() << std::endl;
        logInstallerInfo(std::string("[CLI] ") + totalLine.str());
    }

    if (serviceResult.success) {
        console.showInfo("Installation completed successfully!");
        return INSTALLER_EXIT_SUCCESS;
    }

    console.showError("Installation completed with errors");
    return serviceResult.cancelled ? INSTALLER_EXIT_CANCELLED : INSTALLER_EXIT_FAILED;
    } catch (const std::exception& ex) {
        console.showError(std::string("Unhandled error: ") + ex.what());
        return isCancellationText(ex.what()) ? INSTALLER_EXIT_CANCELLED : INSTALLER_EXIT_FAILED;
    } catch (...) {
        console.showError("Unhandled unknown error.");
        return INSTALLER_EXIT_FAILED;
    }
}
// NOTE: Comment text normalized to avoid encoding mojibake.
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool cleanupMode = argvW ? hasFlagWide(argc, argvW, L"--cleanup-self") : false;
    if (cleanupMode) {
        int exitCode = runCleanupHelperWithWideArgs(argc, argvW);
        if (argvW) {
            LocalFree(argvW);
        }
        return exitCode;
    }
    bool silentMode = argvW ? (hasFlagWide(argc, argvW, L"-s") ||
                               hasFlagWide(argc, argvW, L"--silent")) : false;
    bool debugMode = argvW ? hasFlagWide(argc, argvW, L"--debug") : false;
    bool uninstallMode = argvW ? hasFlagWide(argc, argvW, L"--uninstall") : false;
    EnablePerMonitorDpiAwareness();
    std::string exePathString = getCurrentExecutablePath();
    std::string exeNameString;
    if (!exePathString.empty()) {
        exeNameString = Utf8FromPath(PathFromUtf8(exePathString).filename());
    }
    if (argvW) {
        LocalFree(argvW);
    }

#ifdef _WIN32
    if (!uninstallMode) {
        std::string exeLower = exeNameString;
        std::transform(exeLower.begin(), exeLower.end(), exeLower.begin(), ::tolower);
        if (exeLower == "uninstall.exe" || exeLower.find("uninstall") != std::string::npos) {
            uninstallMode = true;
        } else {
            if (hasLocalManifest(exePathString)) {
                uninstallMode = true;
            }
        }
    }
#endif

    applyDebugMode(debugMode);

    if (silentMode) {
        hideConsoleWhenNotDebug(debugMode);
        return runConsoleInstallerWithWideArgs();
    }

    // NOTE: Comment text normalized to avoid encoding mojibake.
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        GUIHelpers::ShowErrorDialog(
            nullptr,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L""),
            GUIHelpers::GetLocalizedText(L"msg.dialog.com_init_failed", L""));
        return 1;
    }

    if (uninstallMode) {
#ifdef _WIN32
        if (!isRunningAsAdmin()) {
            if (relaunchSelfAsAdmin()) {
                CoUninitialize();
                return 0;
            }
            GUIHelpers::ShowWarningDialog(
                nullptr,
                GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L""),
                GUIHelpers::GetLocalizedText(L"msg.error.require_admin", L""));
            CoUninitialize();
            return 1;
        }
#endif
        std::string appName = deriveAppNameFromExePath(exePathString);
        if (appName.empty()) {
            appName = "Application";
        }

#ifdef _WIN32
        setInstallerAppNameEnv(appName);
#endif
        EnsureInstallerLoggingInitialized();
        logInstallerInfo(std::string("[GUI] Uninstall mode active. exe=") + exeNameString);

        InstallConfig config;
        config.applicationName = Utf8ToWide(appName);
        config.version = L"";
        config.defaultInstallPath.clear();
        config.languageCode.clear();

        InstallerPathResolver pathResolver;
        std::vector<std::string> identityCandidates{appName};
        std::string manifestPath = resolveInstalledManifestPath(identityCandidates, exePathString, pathResolver);
        if (!manifestPath.empty()) {
            nlohmann::json manifest;
            if (readManifest(manifestPath, manifest)) {
                std::string lang = manifest.value("language", "");
                if (!lang.empty()) {
                    config.languageCode = Utf8ToWide(lang);
                }
                std::string manifestAppId = manifest.value("appId", manifest.value("appName", ""));
                if (!manifestAppId.empty()) {
                    config.appId = Utf8ToWide(manifestAppId);
                }
                if (manifest.contains("legacyAppIds") && manifest["legacyAppIds"].is_array()) {
                    for (const auto& legacyId : manifest["legacyAppIds"]) {
                        if (legacyId.is_string()) {
                            config.legacyAppIds.push_back(Utf8ToWide(legacyId.get<std::string>()));
                        }
                    }
                }
            }
        }

        GuiResourceContext resources;
        PrepareGuiResources(hInstance, resources, false);
        GuiResourceValidationResult validation = ValidateInstallGuiResources(resources);
        if (validation == GuiResourceValidationResult::Abort) {
            CoUninitialize();
            return 1;
        }
        if (validation == GuiResourceValidationResult::RunConsoleFallback) {
            CoUninitialize();
            return runConsoleInstallerWithWideArgs();
        }
        ApplyGuiResources(resources, false);

        auto pFrame = std::make_unique<GUIManager>();
        if (!pFrame) {
            CoUninitialize();
            return 1;
        }
        pFrame->SetUninstallMode(true);
        pFrame->SetInstallConfig(config);
        std::wstring uninstallTitle = GUIHelpers::GetLocalizedText(L"msg.title.uninstall", L"");
        int exitCode = RunGuiWindow(
            *pFrame, uninstallTitle, resources, true, WindowSize{ 560, 350 }, false);
        CoUninitialize();
        return exitCode;
    }
    
    // NOTE: Comment text normalized to avoid encoding mojibake.
    MetadataParser parser;
    auto metadata = parser.parseExtendedEmbeddedMetadata();
    
    if (!parser.validateMetadata(metadata)) {
        GUIHelpers::ShowErrorDialog(
            nullptr,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L""),
            GUIHelpers::GetLocalizedText(L"msg.error.metadata_invalid", L""));
        CoUninitialize();
        return 1;
    }

#ifdef _WIN32
    setInstallerAppNameEnv(metadata.applicationName);
#endif

    if (metadata.requireAdmin && !isRunningAsAdmin()) {
        if (relaunchSelfAsAdmin()) {
            CoUninitialize();
            return 0;
        }
        GUIHelpers::ShowWarningDialog(
            nullptr,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L""),
            GUIHelpers::GetLocalizedText(L"msg.error.require_admin", L""));
        CoUninitialize();
        return 1;
    }
    EnsureInstallerLoggingInitialized();
    
    // NOTE: Comment text normalized to avoid encoding mojibake.
    InstallConfig config = createInstallConfigFromMetadata(metadata);
    
    GuiResourceContext resources;
    PrepareGuiResources(hInstance, resources, true);
    GuiResourceValidationResult validation = ValidateInstallGuiResources(resources);
    if (validation == GuiResourceValidationResult::Abort) {
        CoUninitialize();
        return 1;
    }

    if (validation == GuiResourceValidationResult::RunConsoleFallback) {
        CoUninitialize();
        return runConsoleInstallerWithWideArgs();
    }
    ApplyGuiResources(resources, true);
    
    auto pFrame = std::make_unique<GUIManager>();
    if (!pFrame) {
        logInstallerError("[GUI] Failed to create GUIManager.");
        CoUninitialize();
        return 1;
    }
    logInstallerInfo("[GUI] Created GUIManager successfully.");

    pFrame->SetUninstallMode(uninstallMode);

    pFrame->SetInstallConfig(config);
    logInstallerInfo("[GUI] Install config applied.");
    std::wstring installTitle = GUIHelpers::GetLocalizedText(L"msg.title.install", L"");
    int exitCode = RunGuiWindow(
        *pFrame, installTitle, resources, false, WindowSize{ 800, 600 }, true);
    CoUninitialize();
    return exitCode;
}

// NOTE: Comment text normalized to avoid encoding mojibake.
int main(int argc, char* argv[]) {
    bool cleanupMode = hasFlag(argc, argv, "--cleanup-self");
    if (cleanupMode) {
#ifdef _WIN32
        int wideArgc = 0;
        LPWSTR* wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
        if (!wideArgv) {
            return 1;
        }
        int exitCode = runCleanupHelperWithWideArgs(wideArgc, wideArgv);
        LocalFree(wideArgv);
        return exitCode;
#else
        return 0;
#endif
    }
    bool silentMode = hasFlag(argc, argv, "-s") || hasFlag(argc, argv, "--silent");
    bool debugMode = hasFlag(argc, argv, "--debug");

#ifdef _WIN32
    applyDebugMode(debugMode);
#endif

    if (!silentMode) {
#ifdef _WIN32
        hideConsoleWhenNotDebug(debugMode);
#endif
        HINSTANCE hInstance = GetModuleHandle(NULL);
        return wWinMain(hInstance, NULL, GetCommandLineW(), SW_SHOWNORMAL);
    }
    
    // NOTE: Comment text normalized to avoid encoding mojibake.
    return runConsoleInstaller(argc, argv);
}

