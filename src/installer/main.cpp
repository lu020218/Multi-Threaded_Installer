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
#include "installer/embedded_resources.h"
#include <Windows.h>
#include <Shlwapi.h>
#include "Utils/unzip.h"
#pragma comment(lib, "Shlwapi.lib")

#include <iostream>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iomanip>
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

struct WindowSize {
    int width = 0;
    int height = 0;
};

struct GuiResourceContext {
    EmbeddedResourceManager resourceManager;
    std::string tempResourcePath;
    CDuiString resourcePath;
    CDuiString resourceBasePath;
    CDuiString skinsPath;
    bool useZip = false;
};

static std::string ReadFileToString(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}


static bool ZipEntryExists(const CDuiString& zipPath, const CDuiString& entry) {
    HZIP hz = OpenZip(zipPath.GetData(), 0);
    if (hz == NULL) {
        return false;
    }
    ZIPENTRY ze;
    int index = 0;
    bool found = (FindZipItem(hz, entry.GetData(), true, &index, &ze) == 0);
    CloseZip(hz);
    return found;
}

static void LogZipEntryCheck(const CDuiString& zipPath, const std::vector<CDuiString>& entries) {
    std::string zipPathUtf8 = WideToUtf8(TCharToWide(zipPath.GetData()));
    if (!zipPathUtf8.empty()) {
        std::cout << "Resource zip path: " << zipPathUtf8 << std::endl;
    }
    for (const auto& entry : entries) {
        std::string entryUtf8 = WideToUtf8(TCharToWide(entry.GetData()));
        std::cout << "Zip entry check: " << entryUtf8 << " -> "
                  << (ZipEntryExists(zipPath, entry) ? "found" : "missing")
                  << std::endl;
    }
}

static std::string ReadZipEntryToString(const CDuiString& zipPath, const CDuiString& entry) {
    HZIP hz = OpenZip(zipPath.GetData(), 0);
    if (hz == NULL) {
        return {};
    }
    ZIPENTRY ze;
    int index = 0;
    if (FindZipItem(hz, entry.GetData(), true, &index, &ze) != 0) {
        CloseZip(hz);
        return {};
    }
    std::string buffer(static_cast<size_t>(ze.unc_size), '\0');
    if (UnzipItem(hz, index, buffer.data(), ze.unc_size) != 0) {
        CloseZip(hz);
        return {};
    }
    CloseZip(hz);
    return buffer;
}

static WindowSize ParseWindowSizeFromXml(const std::string& xml, WindowSize fallback) {
    size_t pos = xml.find("size=\"");
    if (pos == std::string::npos) {
        pos = xml.find("size='");
    }
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = xml.find_first_of("\"'", pos);
    if (pos == std::string::npos) {
        return fallback;
    }
    char quote = xml[pos];
    size_t end = xml.find(quote, pos + 1);
    if (end == std::string::npos) {
        return fallback;
    }
    std::string sizeText = xml.substr(pos + 1, end - pos - 1);
    size_t comma = sizeText.find(',');
    if (comma == std::string::npos) {
        return fallback;
    }
    try {
        int w = std::stoi(sizeText.substr(0, comma));
        int h = std::stoi(sizeText.substr(comma + 1));
        if (w > 0 && h > 0) {
            return WindowSize{ w, h };
        }
    } catch (...) {
        return fallback;
    }
    return fallback;
}

static WindowSize GetWindowSizeFromResources(bool useZip,
                                             const CDuiString& resourcePath,
                                             const CDuiString& skinsPath,
                                             bool uninstallMode,
                                             WindowSize fallback) {
    const wchar_t* zipFileName = L"resources.zip";
    const wchar_t* mainFile = uninstallMode ? L"uninstall_main.xml" : L"main.xml";

    if (useZip) {
        CDuiString zipPath = resourcePath + zipFileName;
        std::vector<CDuiString> candidates;
        candidates.emplace_back(CDuiString(_T("skins\\")) + mainFile);
        candidates.emplace_back(CDuiString(_T("skins/")) + mainFile);
        candidates.emplace_back(CDuiString(mainFile));

        for (const auto& entry : candidates) {
            std::string content = ReadZipEntryToString(zipPath, entry);
            if (!content.empty()) {
                return ParseWindowSizeFromXml(content, fallback);
            }
        }
        return fallback;
    }

    std::filesystem::path filePath = PathFromTChar(skinsPath.GetData());
    filePath /= mainFile;
    std::string content = ReadFileToString(filePath);
    if (content.empty()) {
        return fallback;
    }
    return ParseWindowSizeFromXml(content, fallback);
}

static std::vector<CDuiString> BuildResourceZipChecks() {
    std::vector<CDuiString> checks;
    checks.emplace_back(_T("images/bg2.png"));
    checks.emplace_back(_T("../images/bg2.png"));
    checks.emplace_back(_T("images/bg2@150.png"));
    checks.emplace_back(_T("../images/bg2@150.png"));
    checks.emplace_back(_T("images/bg2@200.png"));
    checks.emplace_back(_T("../images/bg2@200.png"));
    checks.emplace_back(_T("images/logo3.png"));
    checks.emplace_back(_T("../images/logo3.png"));
    checks.emplace_back(_T("skins/msgBox.xml"));
    checks.emplace_back(_T("skins\\msgBox.xml"));
    checks.emplace_back(_T("msgBox.xml"));
    return checks;
}

static void PrepareGuiResources(HINSTANCE hInstance,
                                GuiResourceContext& context,
                                bool verboseLogs) {
    context.tempResourcePath = context.resourceManager.extractResources();
    CPaintManagerUI::SetInstance(hInstance);

    if (!context.tempResourcePath.empty()) {
#if defined(UNICODE) || defined(_UNICODE)
        std::wstring wpath = Utf8ToWide(context.tempResourcePath);
        if (!wpath.empty()) {
            context.resourceBasePath = wpath.c_str();
        }
#else
        context.resourceBasePath = context.tempResourcePath.c_str();
#endif
        context.resourcePath = context.resourceBasePath;
        if (!context.resourcePath.IsEmpty()) {
            TCHAR lastChar = context.resourcePath.GetAt(context.resourcePath.GetLength() - 1);
            if (lastChar != _T('\\') && lastChar != _T('/')) {
                context.resourcePath += _T("\\");
            }
        }
        context.skinsPath = context.resourcePath + _T("skins\\");
        if (verboseLogs) {
            std::cout << "Using extracted resources from: " << context.tempResourcePath << std::endl;
        }
    }

    if (context.resourcePath.IsEmpty()) {
        CDuiString instancePath = CPaintManagerUI::GetInstancePath();
        context.resourceBasePath = instancePath + _T("resources\\");
        context.resourcePath = context.resourceBasePath;
        context.skinsPath = context.resourceBasePath + _T("skins\\");
    }

    context.useZip = false;
    if (!context.tempResourcePath.empty()) {
        std::filesystem::path zipPath = PathFromUtf8(context.tempResourcePath) / "resources.zip";
        context.useZip = std::filesystem::exists(zipPath);
    }
    if (!context.useZip && !context.resourceBasePath.IsEmpty()) {
        std::filesystem::path zipPath = PathFromTChar(context.resourceBasePath.GetData()) / "resources.zip";
        context.useZip = std::filesystem::exists(zipPath);
    }
}

enum class GuiResourceValidationResult {
    Ok,
    Abort,
    RunConsoleFallback,
};

static GuiResourceValidationResult ValidateInstallGuiResources(const GuiResourceContext& context) {
    if (!context.tempResourcePath.empty()) {
        return GuiResourceValidationResult::Ok;
    }

    CDuiString instancePath = CPaintManagerUI::GetInstancePath();
    std::wcout << L"Instance path: " << instancePath.GetData() << std::endl;
    std::wcout << L"Resource path: " << context.resourcePath.GetData() << std::endl;
    std::wcout << L"Skin path: " << context.skinsPath.GetData() << std::endl;
    std::wcout << L"Skin path exists: " << (PathFileExists(context.skinsPath) ? L"YES" : L"NO")
               << std::endl;

    if (PathFileExists(context.skinsPath)) {
        return GuiResourceValidationResult::Ok;
    }

    CDuiString mainXmlPath = context.skinsPath + _T("main.xml");
    std::wcout << L"Checking main.xml at: " << mainXmlPath.GetData() << std::endl;
    std::wcout << L"main.xml exists: " << (PathFileExists(mainXmlPath) ? L"YES" : L"NO")
               << std::endl;

    std::wstring resourceMissingSummary =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.summary", L"");
    std::wstring debugHeader =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.debug", L"");
    std::wstring instanceLabel =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.instance_path", L"");
    std::wstring resourceLabel =
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.resource_path", L"");
    std::wstring errorMessage =
        resourceMissingSummary + L"\n\n" + debugHeader + L"\n" + instanceLabel + L": " +
        TCharToWide(instancePath.GetData()) + L"\n" + resourceLabel + L": " +
        TCharToWide(context.resourceBasePath.GetData());

    GUIHelpers::ShowWarningDialog(
        nullptr,
        GUIHelpers::GetLocalizedText(L"msg.dialog.resources_missing.title", L""),
        errorMessage);

    bool debugMode = GetEnvironmentVariableW(L"MTINSTALLER_DEBUG", nullptr, 0) > 0;
    return debugMode ? GuiResourceValidationResult::RunConsoleFallback
                     : GuiResourceValidationResult::Abort;
}

static void ApplyGuiResources(const GuiResourceContext& context, bool verboseLogs) {
    if (context.useZip) {
        CPaintManagerUI::SetResourcePath(context.resourcePath);
        CPaintManagerUI::SetResourceZip(_T("resources.zip"), true);
        CPaintManagerUI::SetResourceType(UILIB_ZIP);
        if (verboseLogs) {
            std::wcout << L"Set resource zip to: " << context.resourcePath.GetData()
                       << L"resources.zip" << std::endl;
        }
        std::cout << "Resource zip enabled: true" << std::endl;
        CDuiString zipPath = context.resourcePath + _T("resources.zip");
        LogZipEntryCheck(zipPath, BuildResourceZipChecks());
        return;
    }

    CPaintManagerUI::SetResourceZip(_T(""));
    CPaintManagerUI::SetResourcePath(context.resourcePath);
    CPaintManagerUI::SetResourceType(UILIB_FILE);
    if (verboseLogs) {
        std::wcout << L"Set resource path to: " << context.resourcePath.GetData() << std::endl;
        std::wcout << L"Set resource type to UILIB_FILE" << std::endl;
    }
    std::cout << "Resource zip enabled: false" << std::endl;
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
        if (verboseLogs) {
            std::wcout << L"About to call Create()..." << std::endl;
        }
        hwnd = frame.Create(
            NULL, title.c_str(), UI_WNDSTYLE_FRAME, 0L, 0, 0, baseSize.width, baseSize.height);
    } catch (const std::exception& e) {
        if (verboseLogs) {
            std::wcout << L"ERROR: Exception during Create(): " << e.what() << std::endl;
        }
        return 1;
    } catch (...) {
        if (verboseLogs) {
            std::wcout << L"ERROR: Unknown exception during Create()" << std::endl;
        }
        return 1;
    }

    if (hwnd == NULL) {
        if (verboseLogs) {
            std::wcout << L"ERROR: Create() returned NULL" << std::endl;
            std::wcout << L"GetLastError() = " << GetLastError() << std::endl;
        }
        return 1;
    }

#ifdef _WIN32
    AdjustWindowForDpi(hwnd, baseSize.width, baseSize.height);
#endif

    frame.CenterWindow();
    if (verboseLogs) {
        std::wcout << L"Centered window" << std::endl;
    }
    frame.ShowWindow(true);
    if (verboseLogs) {
        std::wcout << L"Showed window, entering message loop..." << std::endl;
    }
    CPaintManagerUI::MessageLoop();
    if (verboseLogs) {
        std::wcout << L"Message loop exited" << std::endl;
    }
    CPaintManagerUI::SetResourceZip(_T(""), true);
    return 0;
}

#endif

// NOTE: Comment text normalized to avoid encoding mojibake.
InstallConfig createInstallConfigFromMetadata(const ExtendedInstallationMetadata& metadata) {
    InstallConfig config;
    config.applicationName = Utf8ToWide(metadata.applicationName);
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
            std::string manifestPath = resolveInstalledManifestPath(
                fallbackAppName, exePath, pathResolver);
            if (!manifestPath.empty()) {
                bool ok = uninstallFromManifest(manifestPath, pathResolver, console);
                return ok ? INSTALLER_EXIT_SUCCESS : INSTALLER_EXIT_FAILED;
            }
        }
        MetadataParser parser;
        metadata = parser.parseExtendedEmbeddedMetadata();
        if (parser.validateMetadata(metadata)) {
            std::string manifestPath = resolveInstalledManifestPath(
                metadata.applicationName, exePath, pathResolver);
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

    EnsureInstallerLoggingInitialized();

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

    console.showInfo("Found " + std::to_string(metadata.folderCount) + " folders to install");
    console.showInfo("Application: " + metadata.applicationName);

    std::string userSelectedPath;
    if (args.defaultDestination.empty() && !args.silent) {
        console.showInstallerMenu();

        std::string defaultPath = pathResolver.expandEnvironmentVariables(metadata.defaultInstallDir);
        console.showInfo("Suggested installation directory: " + defaultPath);

        std::cout << "Enter installation directory (or press Enter to use default): ";
        std::getline(std::cin, userSelectedPath);

        if (userSelectedPath.empty()) {
            userSelectedPath = defaultPath;
        }

        console.showInfo("Installing to: " + userSelectedPath);
    } else if (!args.defaultDestination.empty()) {
        userSelectedPath = args.defaultDestination;
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
        metadata.applicationName
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

    std::cout << "Timing summary: indexed read "
              << std::fixed << std::setprecision(2)
              << serviceResult.timing.indexedReadSec << "s, indexed decompress "
              << serviceResult.timing.indexedDecompressSec << "s, indexed write "
              << serviceResult.timing.indexedWriteSec << "s, legacy total "
              << serviceResult.timing.legacyTotalSec << "s" << std::endl;

    for (const auto& timing : serviceResult.timing.folderTimings) {
        if (timing.indexed) {
            std::cout << "Timing (indexed) " << timing.folderName
                      << ": total " << std::fixed << std::setprecision(2) << timing.totalSec
                      << "s, read " << timing.readSec
                      << "s, decompress " << timing.decompressSec
                      << "s, write " << timing.writeSec << "s" << std::endl;
        } else {
            std::cout << "Timing (legacy) " << timing.folderName
                      << ": total " << std::fixed << std::setprecision(2) << timing.totalSec
                      << "s, read " << timing.readSec
                      << "s, decompress " << timing.decompressSec
                      << "s, write " << timing.writeSec
                      << "s, process " << timing.processSec << "s" << std::endl;
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << elapsed.count()
              << " seconds" << std::endl;

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
        std::string appName = deriveAppNameFromExePath(exePathString);
        if (appName.empty()) {
            appName = "Application";
        }

#ifdef _WIN32
        setInstallerAppNameEnv(appName);
#endif
        EnsureInstallerLoggingInitialized();

        std::cout << "Uninstall mode active. exe=" << exeNameString << std::endl;

        InstallConfig config;
        config.applicationName = Utf8ToWide(appName);
        config.version = L"";
        config.defaultInstallPath.clear();
        config.languageCode.clear();

        InstallerPathResolver pathResolver;
        std::string manifestPath = resolveInstalledManifestPath(appName, exePathString, pathResolver);
        if (!manifestPath.empty()) {
            nlohmann::json manifest;
            if (readManifest(manifestPath, manifest)) {
                std::string lang = manifest.value("language", "");
                if (!lang.empty()) {
                    config.languageCode = Utf8ToWide(lang);
                }
            }
        }

        GuiResourceContext resources;
        PrepareGuiResources(hInstance, resources, false);
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

    EnsureInstallerLoggingInitialized();

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
        std::wcout << L"ERROR: Failed to create GUIManager" << std::endl;
        CoUninitialize();
        return 1;
    }
    std::wcout << L"Created GUIManager successfully" << std::endl;

    pFrame->SetUninstallMode(uninstallMode);

    pFrame->SetInstallConfig(config);
    std::wcout << L"Set install config" << std::endl;
    std::wstring installTitle = GUIHelpers::GetLocalizedText(L"msg.title.install", L"");
    int exitCode = RunGuiWindow(
        *pFrame, installTitle, resources, false, WindowSize{ 800, 600 }, true);
    CoUninitialize();
    return exitCode;
}

// NOTE: Comment text normalized to avoid encoding mojibake.
int main(int argc, char* argv[]) {
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

