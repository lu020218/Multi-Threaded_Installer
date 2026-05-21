#include "common/archive_types.h"
#include "packager/configuration_manager.h"
#include "packager/configuration_loader.h"
#include "packager/configuration_validator.h"
#include "packager/package_manifest_builder.h"
#include "packager/version_info_updater.h"
#include "installer/metadata_parser.h"
#include "installer/console_interface.h"
#include "installer/component_launcher.h"
#include "common/package_manifest_codec.h"
#include "common/utf8_utils.h"
#include "installer/package_manifest_validator.h"
#include "installer/install_plan_builder.h"
#include "installer/install_manifest_store.h"
#include "installer/install_state_store.h"
#include "installer/installer_helpers.h"
#include "installer/installed_instance_resolver.h"
#include "installer/path_resolver.h"
#include "installer/registry_utils.h"
#include "installer/installer_task_manager.h"
#include "installer/thread_pool_manager.h"
#include "installer/installer_concurrency_policy.h"
#include "installer/cleanup_delete_executor.h"
#include "installer/upgrade_cleanup.h"
#include "installer/uninstall_manager.h"
#include "common/version_utils.h"
#include "gui/progress_path_formatter.h"
#include "post_setup_agent/post_setup_url_utils.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace fs = std::filesystem;
using namespace MultiThreadedInstaller;

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void WriteTextFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw TestFailure("Failed to write test file: " + path.string());
    }
    out << content;
}

fs::path CreateTestRoot(const std::string& name) {
    fs::path root = fs::temp_directory_path() / "mti_schema_tests" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    return root;
}

void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

void TestInstallerConcurrencyPolicyDefaults() {
    const uint32_t hw = GetInstallerHardwareConcurrency();
    Require(hw >= 1, "Hardware concurrency fallback should be at least 1");

    Require(ResolvePayloadWorkerCount(0) == 0,
            "No payload folders should resolve to zero workers");
    const uint32_t oneFolderWorkers = ResolvePayloadWorkerCount(1);
    Require(oneFolderWorkers == 1,
            "One payload folder should resolve to one worker");
    const uint32_t manyFolderWorkers = ResolvePayloadWorkerCount(1024);
    Require(manyFolderWorkers >= 1 && manyFolderWorkers <= 1024,
            "Payload workers should stay within folder count");
    Require(manyFolderWorkers <= std::max<uint32_t>(1, hw / 2),
            "Payload workers should respect the hardware budget");

    Require(ResolveDecoderThreadCount(1) <= 4,
            "Decoder threads should be capped at four");
    Require(ResolveDecoderThreadCount(hw * 2) == 1,
            "High scheduler concurrency should reduce decoder budget to one");

    Require(ResolveCleanupDeleteConcurrency(CleanupDeleteWorkload::Upgrade) <= 4,
            "Upgrade cleanup workers should be capped at four");
    Require(ResolveCleanupDeleteConcurrency(CleanupDeleteWorkload::Uninstall) <= 4,
            "Uninstall cleanup workers should be capped at four");
}

void TestInstallerTaskManagerExecutesAndWaits() {
    InstallerTaskManager manager(2, "SchemaRegressionTaskManager");
    std::atomic<int> counter{0};

    auto first = manager.enqueue([&counter]() {
        counter.fetch_add(1);
        return 7;
    });
    manager.submit([&counter]() {
        counter.fetch_add(2);
    });

    manager.waitForAll();
    InstallerTaskStats stats = manager.stats();

    Require(first.get() == 7, "InstallerTaskManager future should return task result");
    Require(counter.load() == 3, "InstallerTaskManager should execute all queued tasks");
    Require(stats.pending == 0, "InstallerTaskManager should have no pending tasks after wait");
    Require(stats.active == 0, "InstallerTaskManager should have no active tasks after wait");
    Require(stats.completed == 2, "InstallerTaskManager should track completed tasks");
    Require(manager.totalWorkerCount() == 2, "InstallerTaskManager should keep configured worker count");
}

void TestThreadPoolManagerUsesInstallerTaskManagerCompatibly() {
    ThreadPoolManager pool(2);
    std::atomic<int> counter{0};

    auto future = pool.enqueue([&counter]() {
        counter.fetch_add(1);
        return 11;
    });
    pool.enqueue([&counter]() {
        counter.fetch_add(2);
    });

    pool.waitForAll();

    Require(future.get() == 11, "ThreadPoolManager compatibility wrapper should return futures");
    Require(counter.load() == 3, "ThreadPoolManager compatibility wrapper should execute tasks");
    Require(pool.getActiveThreadCount() == 0,
            "ThreadPoolManager should report no active threads after wait");
    Require(pool.getTotalThreadCount() == 2,
            "ThreadPoolManager should report wrapped task manager worker count");
}

void TestCleanupDeleteExecutorUsesUnifiedConcurrency() {
    fs::path root = CreateTestRoot("cleanup_delete_executor_unified");
    WriteTextFile(root / "a.txt", "a");
    WriteTextFile(root / "b.txt", "b");

    std::atomic<int> finished{0};
    CleanupDeleteCallbacks callbacks;
    callbacks.onItemFinished = [&](const fs::path&,
                                   const std::error_code&,
                                   bool,
                                   uint64_t,
                                   const std::string&) {
        finished.fetch_add(1);
    };

    CleanupDeleteExecutor executor(CleanupDeleteWorkload::Upgrade, std::move(callbacks));
    Require(executor.workerConcurrency() == ResolveCleanupDeleteConcurrency(CleanupDeleteWorkload::Upgrade),
            "CleanupDeleteExecutor should use unified concurrency policy");

    std::vector<fs::path> files{root / "a.txt", root / "b.txt"};
    executor.submit(files);
    executor.finish();

    Require(files.empty(), "CleanupDeleteExecutor should consume submitted file list");
    Require(finished.load() == 2, "CleanupDeleteExecutor should report finished file tasks");
    Require(!fs::exists(root / "a.txt"), "CleanupDeleteExecutor should delete first file");
    Require(!fs::exists(root / "b.txt"), "CleanupDeleteExecutor should delete second file");
}

void TestProgressPathFormatterKeepsShortPath() {
    const std::wstring path = L"E:\\Application\\sample_desktop_app\\bin\\app.exe";
    Require(GUIStatusPresenter::FormatProgressPathForDisplay(path, 90) == path,
            "Short progress path should be unchanged");
}

void TestProgressPathFormatterShortensAbsolutePath() {
    const std::wstring path =
        L"E:\\Application\\sample_desktop_app\\resources\\app\\node_modules\\webpack\\node_modules\\ajv\\dist\\refs\\json-schema-2019-09\\index.js";
    const std::wstring display = GUIStatusPresenter::FormatProgressPathForDisplay(path, 60);

    Require(display.size() <= 60, "Formatted absolute path should respect max chars");
    Require(display.find(L"E:\\") == 0, "Formatted absolute path should keep drive root");
    Require(display.find(L"...\\") != std::wstring::npos,
            "Formatted absolute path should contain middle marker");
    Require(display.find(L"index.js") != std::wstring::npos,
            "Formatted absolute path should keep filename");
}

void TestProgressPathFormatterShortensRelativePath() {
    const std::wstring path =
        L"resources\\app\\node_modules\\webpack-dev-middleware\\node_modules\\ajv\\lib\\compile\\errors.ts";
    const std::wstring display = GUIStatusPresenter::FormatProgressPathForDisplay(path, 50);

    Require(display.size() <= 50, "Formatted relative path should respect max chars");
    Require(display.find(L"resources\\") == 0,
            "Formatted relative path should keep first segment");
    Require(display.find(L"...\\") != std::wstring::npos,
            "Formatted relative path should contain middle marker");
    Require(display.find(L"errors.ts") != std::wstring::npos,
            "Formatted relative path should keep filename");
}

void TestProgressPathFormatterStripsLongPathPrefix() {
    const std::wstring path =
        L"\\\\?\\E:\\Application\\sample_desktop_app\\resources\\app\\node_modules\\semver\\classes\\comparator.js";
    const std::wstring display = GUIStatusPresenter::FormatProgressPathForDisplay(path, 80);

    Require(display.find(L"\\\\?\\") == std::wstring::npos,
            "Formatted path should strip long path prefix");
    Require(display.find(L"E:\\") == 0,
            "Formatted long path should keep drive root after stripping prefix");
    Require(display.find(L"comparator.js") != std::wstring::npos,
            "Formatted long path should keep filename");
}

void TestPostSetupFileUrlDecodesUtf8ChinesePath() {
    const std::string chinese = "\xE4\xB8\xAD\xE6\x96\x87\xE8\xB7\xAF\xE5\xBE\x84";
    const std::string decoded = PercentDecodeUrlPath("%E4%B8%AD%E6%96%87%E8%B7%AF%E5%BE%84");

    Require(decoded == chinese, "URL percent decoder should preserve UTF-8 Chinese bytes");

    const std::string path =
        FileUrlToPath("file:///E:/Application/%E4%B8%AD%E6%96%87%E8%B7%AF%E5%BE%84/payload.exe");
    Require(path == "E:\\Application\\" + chinese + "\\payload.exe",
            "file URL should decode UTF-8 Chinese path and normalize separators");
}

void TestPostSetupFileUrlSupportsExpandedInstallDirDrivePath() {
    const std::string chinese = "\xE4\xB8\xAD\xE6\x96\x87\xE8\xB7\xAF\xE5\xBE\x84";
    const std::string path = FileUrlToPath("file://E:\\Application\\" + chinese + "/payload.exe");

    Require(path == "E:\\Application\\" + chinese + "\\payload.exe",
            "file URL should accept already-expanded drive paths with Chinese characters");
}

std::string MinimalValidYaml() {
    return R"(schemaVersion: 3
app:
  id: SampleDesktopApp
  name: Sample Desktop App
  version: 1.2.3
  publisher: OpenAI
  website: https://example.com
  icon: app.ico
  versionInfo:
    productName: Sample Desktop App
    fileDescription: Sample Desktop App Installer
package:
  compression:
    algorithm: zstd
    level: 6
    threads: 2
installer:
  defaultDir: "%ProgramFiles%\\SampleDesktopApp"
  directoryName: SampleDesktopApp
  requireAdmin: true
  mutex: "Global\\SampleDesktopApp_Install"
  minWindows: "10.0.19045"
  largeFileThresholdBytes: 4194304
  killBeforeInstall:
    - SampleDesktopApp.exe
  defaults:
    autoStartup: false
    desktopShortcut: true
  installState:
    registries:
      - id: mainRegistry
        path: HKEY_LOCAL_MACHINE\\Software\\SampleDesktopApp
        values:
          installDir:
            key: InstallDir
            value: "%InstallDir%"
            type: expand
          displayName:
            key: DisplayName
            value: "%AppName%"
            type: string
          displayVersion:
            key: Version
            value: "%Version%"
            type: string
          executablePath:
            key: ExecutablePath
            value: "%InstallDir%\\SampleDesktopApp.exe"
            type: expand
          installState:
            key: InstallState
            value: "%InstallState%"
            type: string
    files:
      - id: mainFile
        path: "%InstallDir%\\install.state.json"
        format: json
        values:
          installDir:
            name: installDir
            value: "%InstallDir%"
          customValue:
            name: customValue
            value: custom
    detect:
      primary:
        registry: mainRegistry
        value: installDir
      legacy:
        - id: legacy_v2
          path: HKEY_CURRENT_USER\\Software\\SampleDesktopAppLegacy
          installDirValue: InstallPath
  systemUninstallEntry:
    scope: machine
    displayName: Sample Desktop App
    publisher: OpenAI
  cleanup:
    systemUninstallEntry:
      legacyEntries:
        - displayName: SampleDesktopApp
          scope: both
  ui:
    defaultLanguage: zh_CN
    desktopShortcutName:
      default: Sample Desktop App
  payload:
    - id: app
      source: bin
      target: "%InstallDir%"
    - id: user_templates
      source: templates
      target: "%AppData%\\SampleDesktopApp"
  registry:
    write:
      - path: HKEY_LOCAL_MACHINE\\Software\\SampleDesktopApp
        values:
          Publisher:
            value: OpenAI
            type: string
  components:
    - id: core
      name: Core
      required: true
      defaultSelected: true
      payload:
        - app
uninstaller:
  requireAdmin: true
  killBeforeUninstall:
    - SampleDesktopApp.exe
  cleanup:
    installedFiles: manifest
    missingManifestFallback: safeDirectoryFallback
    installState: delete
    autoStartup: auto
    desktopShortcut: auto
    systemUninstallEntry:
      scope: machine
      displayName: Sample Desktop App
      legacyEntries:
        - displayName: SampleDesktopApp
          scope: both
    legacy:
      desktopShortcutNames:
        - Sample Desktop App
      startupNames:
        - SampleDesktopApp
    registry:
      deleteKeys:
        - HKEY_CURRENT_USER\\Software\\SampleDesktopApp
      deleteValues:
        - path: HKEY_CURRENT_USER\\Software\\SampleDesktopApp
          key: LegacyValue
    paths:
      - path: "%LocalAppData%\\SampleDesktopApp\\Cache"
        recursive: true
        onlyIfEmpty: false
)";
}

std::string OldSchemaYaml() {
    return R"(schemaVersion: 2
app:
  name: Legacy App
install:
  installInfo:
    mode: registry
    path: HKEY_CURRENT_USER\\Software\\LegacyApp
    values:
      installDir:
        key: InstallDir
        value: "%InstallDir%"
        type: expand
      displayName:
        key: DisplayName
        value: "%AppName%"
        type: string
      displayVersion:
        key: Version
        value: "%Version%"
        type: string
      executablePath:
        key: ExecutablePath
        value: "%InstallDir%\\LegacyApp.exe"
        type: expand
      installState:
        key: InstallState
        value: "%InstallState%"
        type: string
layout:
  folders:
    - id: app
      source: bin
      destination:
        type: install_directory
lifecycle:
  cleanup:
    onUpgrade: {}
    onUninstall: {}
)";
}

PackagerConfiguration BuildConfigFixture();

InstallStateConfig BuildTestInstallStateConfig(const std::string& registryPath,
                                               const std::string& installDirKey = "InstallDir") {
    InstallStateConfig config;
    InstallStateRegistryStoreConfig store;
    store.id = "main";
    store.path = registryPath;

    InstallStateValueConfig installDirValue;
    installDirValue.key = installDirKey;
    installDirValue.value = "%InstallDir%";
    installDirValue.type = RegistryValueType::EXPAND_STRING;
    store.values["installDir"] = installDirValue;

    InstallStateValueConfig installStateValue;
    installStateValue.key = "InstallState";
    installStateValue.value = "%InstallState%";
    installStateValue.type = RegistryValueType::STRING;
    store.values["installState"] = installStateValue;

    config.registries.push_back(std::move(store));
    return config;
}

void TestLoadValidSchema() {
    fs::path root = CreateTestRoot("valid_schema");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(inputDir / "templates");
    fs::create_directories(configDir);
    WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");
    WriteTextFile(configDir / "packager.yaml", MinimalValidYaml());

    ConfigurationManager manager;
    Require(manager.initialize(inputDir.string(), configDir.string()),
            manager.getLastError().empty() ? "valid schema should initialize" : manager.getLastError());

    const auto& config = manager.getConfiguration();
    Require(config.installer.payload.size() == 2, "Expected two installer payload entries");
    Require(config.installer.payload[0].target == "%InstallDir%", "Payload target should preserve %InstallDir%");
    Require(config.installer.installState.registries.size() == 1, "Expected one installState registry store");
    Require(config.installer.installState.files.size() == 1, "Expected one installState file store");
    Require(config.installer.installState.files[0].values.count("customValue") == 1,
            "installState file store should preserve custom values");
    Require(config.installer.installState.detect.primary.registry == "mainRegistry" &&
                config.installer.installState.detect.primary.value == "installDir",
            "v3 installState primary detection mismatch");
    Require(config.installer.installState.detect.legacy.size() == 1 &&
                config.installer.installState.detect.legacy[0].id == "legacy_v2",
            "v3 legacy installState detection should parse");
    Require(config.installer.installState.registries[0].values.at("installState").value == "%InstallState%",
            "installState template mismatch");
    Require(config.installer.payload.size() == 2, "v3 payload entries should be exposed directly");
    Require(config.installer.cleanup.systemUninstallEntry.legacyEntries[0].scope == UninstallEntryScope::BOTH,
            "installer cleanup legacy system uninstall entry scope should parse");
    Require(config.uninstaller.cleanup.systemUninstallEntry.legacyEntries[0].scope == UninstallEntryScope::BOTH,
            "uninstaller cleanup legacy system uninstall entry scope should parse");
    Require(config.installer.cleanup.systemUninstallEntry.legacyEntries.size() == 1,
            "installer cleanup legacy system uninstall entries should remain in v3 cleanup");
}

void TestRejectOldSchema() {
    fs::path root = CreateTestRoot("old_schema");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(configDir);
    WriteTextFile(configDir / "packager.yaml", OldSchemaYaml());

    ConfigurationManager manager;
    Require(!manager.initialize(inputDir.string(), configDir.string()), "Old schema should be rejected");
    Require(!manager.getLastError().empty(), "Old schema rejection should provide an error message");
}

void TestComponentInstallTypeIsRequired() {
    fs::path root = CreateTestRoot("component_install_type_required");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(inputDir / "templates");
    fs::create_directories(configDir);
    WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");

    std::string yaml = MinimalValidYaml();
    ReplaceAll(yaml,
               "      install:\n        type: local\n        command: addon\\\\install_plugins.bat\n",
               "      install:\n        command: addon\\\\install_plugins.bat\n");
    if (yaml.find("command: addon\\\\install_plugins.bat") == std::string::npos) {
        ReplaceAll(yaml,
                   "      defaultSelected: true\n      payload:\n        - app\n",
                   "      defaultSelected: true\n      payload:\n        - app\n"
                   "      install:\n"
                   "        command: addon\\\\install_plugins.bat\n");
    }
    WriteTextFile(configDir / "packager.yaml", yaml);

    ConfigurationManager manager;
    Require(!manager.initialize(inputDir.string(), configDir.string()),
            "component install object without type should be rejected");
    Require(manager.getLastError().find("installer.components[].install.type is required") != std::string::npos,
            "missing install.type error should be explicit");
}

void TestComponentLocalInstallRequiresCommand() {
    fs::path root = CreateTestRoot("component_local_requires_command");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(inputDir / "templates");
    fs::create_directories(configDir);
    WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");

    std::string yaml = MinimalValidYaml();
    ReplaceAll(yaml,
               "      defaultSelected: true\n      payload:\n        - app\n",
               "      defaultSelected: true\n      payload:\n        - app\n"
               "      install:\n"
               "        type: local\n");
    WriteTextFile(configDir / "packager.yaml", yaml);

    ConfigurationManager manager;
    Require(!manager.initialize(inputDir.string(), configDir.string()),
            "local component install without command should be rejected");
    Require(manager.getLastError().find("installer.components[].install.command") != std::string::npos,
            "missing local command error should be explicit");
}

void TestComponentDownloadInstallValidationAndRoundTrip() {
    fs::path root = CreateTestRoot("component_download_validation");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(inputDir / "templates");
    fs::create_directories(configDir);
    WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");

    const std::string validSha =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    std::string yaml = MinimalValidYaml();
    ReplaceAll(yaml,
               "      defaultSelected: true\n      payload:\n        - app\n",
               "      defaultSelected: true\n      payload:\n        - app\n"
               "      install:\n"
               "        type: download\n"
               "        url: https://example.com/plugin-installer.exe\n"
               "        sha256: " + validSha + "\n"
               "        saveAs: \"%InstallDir%\\\\downloads\\\\plugin-installer.exe\"\n"
               "        args: /quiet\n"
               "        wait: true\n"
               "        showWindow: false\n"
               "        timeoutSec: 120\n");
    WriteTextFile(configDir / "packager.yaml", yaml);

    ConfigurationManager manager;
    Require(manager.initialize(inputDir.string(), configDir.string()),
            manager.getLastError().empty() ? "download component schema should initialize"
                                           : manager.getLastError());
    const auto& component = manager.getConfiguration().installer.components[0];
    Require(component.source.type == ComponentSourceType::DOWNLOAD,
            "download component source type should parse");
    Require(component.source.download.url == "https://example.com/plugin-installer.exe",
            "download URL should parse");
    Require(component.source.download.sha256 == validSha,
            "download SHA256 should parse");
    Require(component.source.download.showWindowConfigured &&
                !component.source.download.showWindow,
            "download showWindow=false should parse");

    FolderInfo folder;
    folder.id = "app";
    folder.sourcePath = "bin";
    folder.targetPath = "%InstallDir%";
    CompressionResult result;
    result.originalSize = 10;
    result.compressedSize = 5;
    result.algorithm = CompressionAlgorithm::LZMA2_XZ;
    PackageManifestBuilder builder;
    PackageManifest manifest = builder.build({result}, {folder}, manager.getConfiguration());
    auto encoded = SerializePackageManifest(manifest);
    PackageManifest decoded;
    std::string error;
    Require(DeserializePackageManifest(encoded, decoded, error),
            error.empty() ? "download manifest should deserialize" : error);
    Require(decoded.components.components[0].source.type == ComponentSourceType::DOWNLOAD,
            "download component source type should round-trip");
    Require(decoded.components.components[0].source.download.saveAs ==
                "%InstallDir%\\downloads\\plugin-installer.exe",
            "download saveAs should round-trip");
    Require(decoded.components.components[0].source.download.showWindowConfigured &&
                !decoded.components.components[0].source.download.showWindow,
            "download showWindow should round-trip");

    ReplaceAll(yaml, "https://example.com/plugin-installer.exe", "http://example.com/plugin-installer.exe");
    WriteTextFile(configDir / "packager.yaml", yaml);
    ConfigurationManager badHttp;
    Require(!badHttp.initialize(inputDir.string(), configDir.string()),
            "http download URL should be rejected");

    ReplaceAll(yaml, "http://example.com/plugin-installer.exe", "https://example.com/plugin-installer.exe");
    ReplaceAll(yaml, validSha, "not-a-sha");
    WriteTextFile(configDir / "packager.yaml", yaml);
    ConfigurationManager badSha;
    Require(!badSha.initialize(inputDir.string(), configDir.string()),
            "invalid download SHA256 should be rejected");

    ReplaceAll(yaml, "        sha256: not-a-sha\n", "");
    WriteTextFile(configDir / "packager.yaml", yaml);
    ConfigurationManager noSha;
    Require(noSha.initialize(inputDir.string(), configDir.string()),
            noSha.getLastError().empty() ? "download component without SHA256 should initialize"
                                         : noSha.getLastError());
    Require(noSha.getConfiguration().installer.components[0].source.download.sha256.empty(),
            "download SHA256 should remain empty when omitted");
}

void TestComponentLauncherBuildsExpectedCommands() {
    {
        ComponentLaunchCommand command =
            BuildComponentLaunchCommand(fs::path("C:\\Program Files\\Vendor App\\setup.exe"), "/S");
        Require(command.type == ComponentLauncherType::Direct,
                "exe should use direct launcher");
        Require(WideToUtf8(command.commandLine) ==
                    "\"C:\\Program Files\\Vendor App\\setup.exe\" /S",
                "exe command line should quote path and append args");
        Require(!command.hideByDefault, "exe should not hide by default");
    }
    {
        ComponentLaunchCommand command =
            BuildComponentLaunchCommand(fs::path("C:\\Program Files\\Vendor App\\install.bat"), "/quiet");
        Require(command.type == ComponentLauncherType::Batch,
                "bat should use batch launcher");
        Require(WideToUtf8(command.commandLine) ==
                    "cmd.exe /c \"C:\\Program Files\\Vendor App\\install.bat\" /quiet",
                "bat command line should use cmd.exe /c");
        Require(command.hideByDefault, "batch scripts should keep hidden default");
    }
    {
        ComponentLaunchCommand command =
            BuildComponentLaunchCommand(fs::path("C:\\Program Files\\Vendor App\\install.cmd"), "");
        Require(command.type == ComponentLauncherType::Batch,
                "cmd should use batch launcher");
        Require(WideToUtf8(command.commandLine) ==
                    "cmd.exe /c \"C:\\Program Files\\Vendor App\\install.cmd\"",
                "cmd command line should use cmd.exe /c without trailing space");
    }
    {
        ComponentLaunchCommand command =
            BuildComponentLaunchCommand(fs::path("C:\\Program Files\\Vendor App\\install.ps1"), "-Mode Silent");
        Require(command.type == ComponentLauncherType::PowerShell,
                "ps1 should use PowerShell launcher");
        Require(WideToUtf8(command.commandLine) ==
                    "powershell.exe -NoProfile -ExecutionPolicy Bypass -File "
                    "\"C:\\Program Files\\Vendor App\\install.ps1\" -Mode Silent",
                "ps1 command line should use powershell.exe -File");
        Require(!command.hideByDefault, "ps1 should not hide by default");
    }
    {
        ComponentLaunchCommand command =
            BuildComponentLaunchCommand(fs::path("C:\\Program Files\\Vendor App\\package.msi"),
                                        "/qn /norestart");
        Require(command.type == ComponentLauncherType::Msi,
                "msi should use msiexec launcher");
        Require(WideToUtf8(command.commandLine) ==
                    "msiexec.exe /i \"C:\\Program Files\\Vendor App\\package.msi\" /qn /norestart",
                "msi command line should use msiexec.exe /i");
        Require(!command.hideByDefault, "msi should not hide by default");
    }
}

void TestRejectUninstallerDetectField() {
    fs::path root = CreateTestRoot("reject_uninstaller_detect");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(inputDir / "templates");
    fs::create_directories(configDir);
    WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");

    std::string yaml = MinimalValidYaml();
    const std::string marker = "uninstaller:\n  requireAdmin: true\n";
    const std::string unsupported =
        "uninstaller:\n"
        "  requireAdmin: true\n"
        "  detect:\n"
        "    installState:\n"
        "      path: HKEY_CURRENT_USER\\\\Software\\\\OldApp\n"
        "      installDirValue: InstallDir\n";
    const size_t pos = yaml.find(marker);
    Require(pos != std::string::npos, "Minimal yaml should contain uninstaller marker");
    yaml.replace(pos, marker.size(), unsupported);
    WriteTextFile(configDir / "packager.yaml", yaml);

    ConfigurationManager manager;
    Require(!manager.initialize(inputDir.string(), configDir.string()),
            "uninstaller.detect should be rejected");
    Require(manager.getLastError().find("Unsupported field 'uninstaller.detect'") != std::string::npos,
            "uninstaller.detect rejection should point to installer.installState.detect");
}

void TestRejectLegacySystemUninstallEntryFields() {
    fs::path root = CreateTestRoot("reject_legacy_system_uninstall_fields");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(inputDir / "templates");
    fs::create_directories(configDir);
    WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");

    {
        std::string yaml = MinimalValidYaml();
        ReplaceAll(yaml, "  systemUninstallEntry:\n    scope: machine",
                   "  systemUninstallEntry:\n    enabled: true\n    scope: machine");
        WriteTextFile(configDir / "packager.yaml", yaml);
        ConfigurationManager manager;
        Require(!manager.initialize(inputDir.string(), configDir.string()),
                "installer.systemUninstallEntry.enabled should be rejected");
    }
    {
        std::string yaml = MinimalValidYaml();
        ReplaceAll(yaml,
                   "    systemUninstallEntry:\n      scope: machine\n      displayName: Sample Desktop App",
                   "    systemUninstallEntry: auto");
        WriteTextFile(configDir / "packager.yaml", yaml);
        ConfigurationManager manager;
        Require(!manager.initialize(inputDir.string(), configDir.string()),
                "uninstaller.cleanup.systemUninstallEntry string should be rejected");
    }
    {
        std::string yaml = MinimalValidYaml();
        ReplaceAll(yaml,
                   "      startupNames:\n        - SampleDesktopApp\n",
                   "      startupNames:\n        - SampleDesktopApp\n      uninstallEntries:\n        - name: SampleDesktopApp\n          scope: both\n");
        WriteTextFile(configDir / "packager.yaml", yaml);
        ConfigurationManager manager;
        Require(!manager.initialize(inputDir.string(), configDir.string()),
                "uninstaller.cleanup.legacy.uninstallEntries should be rejected");
    }
}

void TestConfigurationLoadsOnlyFromConfigDirectoryAndResolvesIconThere() {
    fs::path root = CreateTestRoot("config_directory_source");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(inputDir / "templates");
    fs::create_directories(configDir);

    WriteTextFile(inputDir / "packager.yaml", OldSchemaYaml());
    WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");

    std::string yaml = MinimalValidYaml();
    const std::string marker = "  website: https://example.com\n";
    size_t position = yaml.find(marker);
    Require(position != std::string::npos, "Minimal YAML marker should exist");
    WriteTextFile(configDir / "packager.yaml", yaml);

    ConfigurationManager manager;
    Require(manager.initialize(inputDir.string(), configDir.string()),
            manager.getLastError().empty() ? "config directory schema should initialize"
                                           : manager.getLastError());
    Require(manager.getConfigFilePath().find("config") != std::string::npos,
            "Configuration should be loaded from config directory");
    Require(manager.getConfiguration().app.icon == "app.ico",
            "Icon path should be read from config directory packager.yaml");
}

void TestRequireAdminFalseRejectsAdminOnlyConfiguration() {
    {
        fs::path root = CreateTestRoot("require_admin_false_program_files");
        fs::path inputDir = root / "payload";
        fs::path configDir = root / "config";
        fs::create_directories(inputDir / "bin");
        fs::create_directories(inputDir / "templates");
        fs::create_directories(configDir);
        WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");
        std::string yaml = MinimalValidYaml();
        ReplaceAll(yaml, "  requireAdmin: true", "  requireAdmin: false");
        WriteTextFile(configDir / "packager.yaml", yaml);

        ConfigurationManager manager;
        Require(!manager.initialize(inputDir.string(), configDir.string()),
                "requireAdmin=false should reject Program Files defaultDir");
    }
    {
        fs::path root = CreateTestRoot("require_admin_false_hklm_install_info");
        fs::path inputDir = root / "payload";
        fs::path configDir = root / "config";
        fs::create_directories(inputDir / "bin");
        fs::create_directories(inputDir / "templates");
        fs::create_directories(configDir);
        WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");
        std::string yaml = MinimalValidYaml();
        ReplaceAll(yaml, "  requireAdmin: true", "  requireAdmin: false");
        ReplaceAll(yaml, "  defaultDir: \"%ProgramFiles%\\\\SampleDesktopApp\"",
                   "  defaultDir: \"%LocalAppData%\\\\SampleDesktopApp\"");
        WriteTextFile(configDir / "packager.yaml", yaml);

        ConfigurationManager manager;
        Require(!manager.initialize(inputDir.string(), configDir.string()),
                "requireAdmin=false should reject HKLM installState path");
    }
    {
        fs::path root = CreateTestRoot("require_admin_false_hklm_on_install");
        fs::path inputDir = root / "payload";
        fs::path configDir = root / "config";
        fs::create_directories(inputDir / "bin");
        fs::create_directories(inputDir / "templates");
        fs::create_directories(configDir);
        WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");
        std::string yaml = MinimalValidYaml();
        ReplaceAll(yaml, "  requireAdmin: true", "  requireAdmin: false");
        ReplaceAll(yaml, "  defaultDir: \"%ProgramFiles%\\\\SampleDesktopApp\"",
                   "  defaultDir: \"%LocalAppData%\\\\SampleDesktopApp\"");
        ReplaceAll(yaml, "        path: HKEY_LOCAL_MACHINE\\\\Software\\\\SampleDesktopApp",
                   "        path: HKEY_CURRENT_USER\\\\Software\\\\SampleDesktopApp");
        WriteTextFile(configDir / "packager.yaml", yaml);

        ConfigurationManager manager;
        Require(!manager.initialize(inputDir.string(), configDir.string()),
                "requireAdmin=false should reject HKLM lifecycle registry path");
    }
}

void TestInstallStateDetectValidationSupportsLegacySources() {
    fs::path root = CreateTestRoot("detect_validation");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(configDir);
    WriteTextFile(configDir / "app.ico", "not-a-real-ico-but-validator-only-checks-path");

    PackagerConfiguration config = BuildConfigFixture();
    config.installer.defaultDir = "%LocalAppData%\\SampleDesktopApp";
    config.installer.requireAdmin = false;
    config.installer.systemUninstallEntry.scope = UninstallEntryScope::CURRENT_USER;
    config.installer.registry.write.clear();
    config.installer.installState.registries[0].path =
        "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\DetectValidation";
    config.installer.payload.clear();
    PayloadConfig payload;
    payload.id = "app";
    payload.source = "bin";
    payload.target = "%InstallDir%";
    config.installer.payload.push_back(payload);
    config.installer.components.clear();
    config.installer.installState.detect.primary = InstalledInstanceDetectPrimaryConfig{};

    ConfigurationValidator validator;
    auto valid = validator.validate(config, inputDir.string(), configDir.string());
    Require(valid.isValid, valid.errors.empty() ? "legacy-only detect should validate" : valid.errors.front());

    config.installer.installState.detect.legacy.push_back(config.installer.installState.detect.legacy.front());
    auto duplicate = validator.validate(config, inputDir.string(), configDir.string());
    Require(!duplicate.isValid, "Duplicate legacy detect ids should fail validation");

    config.installer.installState.detect.legacy.clear();
    auto missing = validator.validate(config, inputDir.string(), configDir.string());
    Require(!missing.isValid, "Empty installer.installState.detect should fail validation");
}

#ifdef _WIN32
std::string ReadManifestResourceText(const fs::path& exePath) {
    HMODULE module = LoadLibraryExW(exePath.wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!module) {
        throw TestFailure("Failed to load test executable as data file");
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), RT_MANIFEST);
    if (!resource) {
        FreeLibrary(module);
        throw TestFailure("Manifest resource not found");
    }

    HGLOBAL loaded = LoadResource(module, resource);
    DWORD size = SizeofResource(module, resource);
    const char* data = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    if (!data || size == 0) {
        FreeLibrary(module);
        throw TestFailure("Manifest resource is empty");
    }

    std::string text(data, data + size);
    FreeLibrary(module);
    return text;
}

fs::path ResolveBuiltInstallerTemplateForTest() {
    fs::path currentExe = PathFromUtf8(getCurrentExecutablePath());
    fs::path buildRoot = currentExe.parent_path().parent_path().parent_path();
    return buildRoot / "Release" / "installer.exe";
}

void TestUpdateInstallerExecutionLevelWritesManifest() {
    fs::path source = ResolveBuiltInstallerTemplateForTest();
    Require(fs::exists(source), "Built installer template should exist for manifest update test");

    fs::path root = CreateTestRoot("execution_level_manifest");
    fs::path elevated = root / "elevated.exe";
    fs::path asInvoker = root / "as_invoker.exe";
    fs::copy_file(source, elevated, fs::copy_options::overwrite_existing);
    fs::copy_file(source, asInvoker, fs::copy_options::overwrite_existing);

    std::string error;
    Require(UpdateInstallerExecutionLevel(elevated.string(), true, error),
            error.empty() ? "requireAdministrator manifest update should succeed" : error);
    Require(UpdateInstallerExecutionLevel(asInvoker.string(), false, error),
            error.empty() ? "asInvoker manifest update should succeed" : error);

    Require(ReadManifestResourceText(elevated).find("requireAdministrator") != std::string::npos,
            "Elevated installer manifest should contain requireAdministrator");
    Require(ReadManifestResourceText(asInvoker).find("asInvoker") != std::string::npos,
            "Non-elevated installer manifest should contain asInvoker");
}
#endif

void TestPackagerArgsNamedOrderIndependent() {
    CliSupport console;
    const char* argv[] = {
        "packager.exe",
        "--output", "dist\\installer.exe",
        "-c", "build-config",
        "--input", "payload",
    };
    auto args = console.parsePackagerArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                          const_cast<char**>(argv));

    Require(args.error.empty(), "Named packager args should parse without error");
    Require(args.inputPath == "payload", "Named packager args should capture input");
    Require(args.configPath == "build-config", "Named packager args should capture config");
    Require(args.outputPath == "dist\\installer.exe", "Named packager args should capture output");
}

void TestPackagerArgsRejectLegacyAndPositional() {
    CliSupport console;
    {
        const char* argv[] = {"packager.exe", "--algorithm", "xz"};
        auto args = console.parsePackagerArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                              const_cast<char**>(argv));
        Require(!args.error.empty(), "Legacy packager option should be rejected");
    }
    {
        const char* argv[] = {"packager.exe", "payload", "config", "dist\\installer.exe"};
        auto args = console.parsePackagerArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                              const_cast<char**>(argv));
        Require(!args.error.empty(), "Positional packager args should be rejected");
    }
    {
        const char* argv[] = {"packager.exe", "--input"};
        auto args = console.parsePackagerArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])),
                                              const_cast<char**>(argv));
        Require(!args.error.empty(), "Missing named packager arg value should be rejected");
    }
}

PackagerConfiguration BuildConfigFixture() {
    PackagerConfiguration config;
    config.app.name = "Sample Desktop App";
    config.app.id = "SampleDesktopApp";
    config.app.version = "1.2.3";
    config.app.website = "https://example.com";
    config.app.publisher = "OpenAI";
    config.ui.desktopShortcut.defaultName = "Sample Shortcut";
    config.ui.desktopShortcut.i18n["zh_CN"] = "示例应用";
    config.ui.desktopShortcut.i18n["en_US"] = "Sample Shortcut";
    UiLinkBinding link;
    link.control = "websiteLink";
    link.url = "https://example.com";
    config.ui.links.push_back(link);

    config.installer.defaultDir = "%ProgramFiles%\\SampleDesktopApp";
    config.installer.directoryName = "SampleDesktopApp";
    config.installer.requireAdmin = true;
    config.installer.mutex = "Global\\SampleDesktopApp_Install";
    config.installer.defaults.autoStartup = false;
    config.installer.defaults.desktopShortcut = true;
    config.installer.minWindows.major = 10;
    config.installer.minWindows.minor = 0;
    config.installer.minWindows.build = 19045;
    config.installer.largeFileThresholdBytes = 4 * 1024 * 1024;
    config.installer.killBeforeInstall.push_back("SampleDesktopApp.exe");

    InstallStateRegistryStoreConfig registryStore;
    registryStore.id = "mainRegistry";
    registryStore.path = "HKEY_LOCAL_MACHINE\\Software\\SampleDesktopApp";
    InstallStateValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    installDirValue.value = "%InstallDir%";
    installDirValue.type = RegistryValueType::EXPAND_STRING;
    registryStore.values["installDir"] = installDirValue;
    InstallStateValueConfig displayNameValue;
    displayNameValue.key = "DisplayName";
    displayNameValue.value = "%AppName%";
    registryStore.values["displayName"] = displayNameValue;
    InstallStateValueConfig versionValue;
    versionValue.key = "Version";
    versionValue.value = "%Version%";
    registryStore.values["displayVersion"] = versionValue;
    InstallStateValueConfig executableValue;
    executableValue.key = "ExecutablePath";
    executableValue.value = "%InstallDir%\\SampleDesktopApp.exe";
    executableValue.type = RegistryValueType::EXPAND_STRING;
    registryStore.values["executablePath"] = executableValue;
    InstallStateValueConfig stateValue;
    stateValue.key = "InstallState";
    stateValue.value = "%InstallState%";
    registryStore.values["installState"] = stateValue;
    InstallStateValueConfig customRegistryValue;
    customRegistryValue.key = "Channel";
    customRegistryValue.value = "stable";
    customRegistryValue.type = RegistryValueType::STRING;
    registryStore.values["channel"] = customRegistryValue;
    config.installer.installState.registries.push_back(registryStore);

    InstallStateFileStoreConfig fileStore;
    fileStore.id = "mainFile";
    fileStore.path = "%InstallDir%\\install.state.json";
    fileStore.format = "json";
    InstallStateValueConfig fileInstallDir;
    fileInstallDir.name = "installDir";
    fileInstallDir.value = "%InstallDir%";
    fileStore.values["installDir"] = fileInstallDir;
    InstallStateValueConfig customFileValue;
    customFileValue.name = "customValue";
    customFileValue.value = "custom";
    fileStore.values["customValue"] = customFileValue;
    config.installer.installState.files.push_back(fileStore);

    config.installer.systemUninstallEntry.scope = UninstallEntryScope::LOCAL_MACHINE;
    config.installer.systemUninstallEntry.displayName = "Sample Desktop App";
    config.installer.systemUninstallEntry.publisher = "OpenAI";

    PayloadConfig payload;
    payload.id = "app";
    payload.source = "bin";
    payload.target = "%InstallDir%";
    payload.required = true;
    config.installer.payload.push_back(payload);

    ComponentConfig component;
    component.id = "core";
    component.name = "Core";
    component.required = true;
    component.defaultSelected = true;
    component.folders.push_back("app");
    component.source.type = ComponentSourceType::EMBEDDED;
    config.installer.components.push_back(component);

    RegistryEntry installRegistry;
    installRegistry.path = "HKEY_LOCAL_MACHINE\\Software\\SampleDesktopApp";
    installRegistry.key = "Publisher";
    installRegistry.value = "OpenAI";
    installRegistry.type = RegistryValueType::STRING;
    InstallerRegistryWriteGroup registryWrite;
    registryWrite.path = installRegistry.path;
    InstallStateValueConfig publisherValue;
    publisherValue.value = installRegistry.value;
    publisherValue.type = installRegistry.type;
    registryWrite.values["Publisher"] = publisherValue;
    config.installer.registry.write.push_back(std::move(registryWrite));

    SystemUninstallEntryCleanupItem installerLegacyUninstallEntry;
    installerLegacyUninstallEntry.displayName = "SampleDesktopApp";
    installerLegacyUninstallEntry.scope = UninstallEntryScope::BOTH;
    config.installer.cleanup.systemUninstallEntry.legacyEntries.push_back(installerLegacyUninstallEntry);

    RegistryEntry legacyRegistry;
    legacyRegistry.path = "HKEY_CURRENT_USER\\Software\\SampleDesktopAppLegacy";
    legacyRegistry.key = "";

    UninstallCleanupRule uninstallPath;
    uninstallPath.path = "%LocalAppData%\\SampleDesktopApp\\Cache";
    uninstallPath.recursive = true;
    uninstallPath.onlyIfEmpty = false;

    config.installer.ui = config.ui;
    config.uninstaller.requireAdmin = true;
    config.installer.installState.detect.primary.registry = "mainRegistry";
    config.installer.installState.detect.primary.value = "installDir";
    InstalledInstanceDetectInstallStateConfig legacyDetect;
    legacyDetect.id = "legacy_v2";
    legacyDetect.path = "HKEY_CURRENT_USER\\Software\\SampleDesktopAppLegacy";
    legacyDetect.installDirValue = "InstallPath";
    config.installer.installState.detect.legacy.push_back(legacyDetect);
    config.uninstaller.killBeforeUninstall.push_back("SampleDesktopApp.exe");
    config.uninstaller.cleanup.installedFiles = "manifest";
    config.uninstaller.cleanup.missingManifestFallback = "safeDirectoryFallback";
    config.uninstaller.cleanup.installState = "delete";
    config.uninstaller.cleanup.autoStartup = "auto";
    config.uninstaller.cleanup.desktopShortcut = "auto";
    config.uninstaller.cleanup.systemUninstallEntry.displayName = "Sample Desktop App";
    config.uninstaller.cleanup.systemUninstallEntry.scope = UninstallEntryScope::LOCAL_MACHINE;
    SystemUninstallEntryCleanupItem uninstallerLegacyUninstallEntry;
    uninstallerLegacyUninstallEntry.displayName = "SampleDesktopApp";
    uninstallerLegacyUninstallEntry.scope = UninstallEntryScope::CURRENT_USER;
    config.uninstaller.cleanup.systemUninstallEntry.legacyEntries.push_back(uninstallerLegacyUninstallEntry);
    config.uninstaller.cleanup.legacy.desktopShortcutNames.push_back("Sample Desktop App");
    config.uninstaller.cleanup.legacy.startupNames.push_back("SampleDesktopApp");
    config.uninstaller.cleanup.registry.deleteKeys.push_back("HKEY_CURRENT_USER\\Software\\SampleDesktopAppLegacy");
    config.uninstaller.cleanup.registry.deleteValues.push_back(legacyRegistry);
    config.uninstaller.cleanup.paths.push_back(uninstallPath);
    config.uninstaller.ui.defaultLanguage = "zh_CN";
    config.uninstaller.ui.title = "Uninstall Sample Desktop App";

    return config;
}

void TestMetadataRoundTrip() {
    PackagerConfiguration config = BuildConfigFixture();

    FolderInfo folder;
    folder.id = "app";
    folder.sourcePath = "bin";
    folder.targetPath = "%InstallDir%";

    CompressionResult result;
    result.originalSize = 123;
    result.compressedSize = 45;
    result.checksum = 0x12345678;
    result.algorithm = CompressionAlgorithm::LZMA2_XZ;
    result.fileIndex.push_back({"SampleDesktopApp.exe", 0, 123});

    PackageManifestBuilder builder;
    PackageManifest manifest = builder.build({result}, {folder}, config);
    std::vector<uint8_t> bytes = SerializePackageManifest(manifest);

    MetadataParser parser;
    ExtendedInstallationMetadata parsed = parser.deserializeExtendedMetadata(bytes);

    Require(parser.validateMetadata(parsed), "Parsed metadata should validate");
    Require(parsed.installState.detect.primary.registry == "mainRegistry" &&
                parsed.installState.detect.primary.value == "installDir",
            "v3 installState detection lost in metadata");
    Require(parsed.installState.detect.legacy.size() == 1 &&
                parsed.installState.detect.legacy[0].id == "legacy_v2",
            "v3 legacy installState detection lost in metadata");
    Require(parsed.installerCleanup.systemUninstallEntry.legacyEntries.size() == 1 &&
                parsed.installerCleanup.systemUninstallEntry.legacyEntries[0].displayName == "SampleDesktopApp",
            "v3 legacy uninstall entries should remain in installer cleanup metadata");
    Require(parsed.installState.registries.size() == 1, "v3 installState registry store lost in metadata");
    Require(parsed.installMutexName == config.installer.mutex, "mutex name lost in metadata");
    Require(parsed.extendedPayloadMappings.size() == 1, "Expected one extended payload mapping");
    Require(parsed.extendedPayloadMappings[0].target == "%InstallDir%", "Folder target lost in metadata");
    Require(parsed.desktopShortcutDefaultName == "Sample Shortcut",
            "Desktop shortcut default name lost in metadata");
    Require(parsed.desktopShortcutLocalizedNames.at("zh_CN") == "示例应用",
            "Desktop shortcut localized names lost in metadata");
    Require(parsed.uiLinkBindings.size() == 1, "UI links lost in metadata");
    Require(parsed.layoutComponents.size() == 1, "Components lost in metadata");
    Require(parsed.uninstallerCleanup.missingManifestFallback == "safeDirectoryFallback",
            "v3 uninstall cleanup policy lost in metadata");
    Require(parsed.uninstallerKillBeforeUninstall.size() == 1,
            "Uninstall process policy should remain in v3 uninstaller metadata");
}

void TestPackageManifestBuilderAndCodecRoundTrip() {
    PackagerConfiguration config = BuildConfigFixture();

    FolderInfo folder;
    folder.id = "app";
    folder.sourcePath = "bin";
    folder.targetPath = "%InstallDir%";

    CompressionResult result;
    result.originalSize = 123;
    result.compressedSize = 45;
    result.checksum = 0x12345678;
    result.algorithm = CompressionAlgorithm::ZSTD;
    result.fileIndex.push_back({"SampleDesktopApp.exe", 0, 123});

    PackageManifestBuilder builder;
    PackageManifest manifest = builder.build({result}, {folder}, config);
    Require(manifest.identity.appName == config.app.name, "Manifest identity app name mismatch");
    Require(manifest.identity.appPublisher == "OpenAI", "Manifest app publisher mismatch");
    Require(manifest.install.defaultDir == config.installer.defaultDir,
            "Manifest installer defaultDir mismatch");
    Require(manifest.install.installState.registries.size() == 1,
            "Manifest installState registry store lost");
    Require(manifest.install.installState.files.size() == 1,
            "Manifest installState file store lost");
    Require(manifest.install.installState.registries[0].values.at("channel").value == "stable",
            "Manifest custom registry installState value lost");
    Require(manifest.install.installState.files[0].values.at("customValue").value == "custom",
            "Manifest custom file installState value lost");
    Require(manifest.install.systemUninstallEntry.displayName == "Sample Desktop App",
            "Manifest system uninstall entry lost");
    Require(manifest.install.registryWrite.size() == 1,
            "Manifest registry.write lost");
    Require(manifest.payload.folders.size() == 1, "Manifest payload folder count mismatch");
    Require(manifest.payload.folders[0].source == "bin", "Manifest payload source mismatch");
    Require(manifest.payload.folders[0].target == "%InstallDir%", "Manifest folder target mismatch");
    Require(manifest.payload.folders[0].required, "Manifest payload required flag mismatch");
    Require(manifest.components.definitions.size() == 1,
            "Manifest v3 component definitions lost");
    Require(manifest.components.definitions[0].payloadRefs[0] == "app",
            "Manifest component payload refs lost");
    Require(manifest.install.installState.detect.primary.registry == "mainRegistry" &&
                manifest.install.installState.detect.primary.value == "installDir",
            "Manifest installState detect policy lost");
    Require(manifest.install.installState.detect.legacy.size() == 1,
            "Manifest legacy installState detect policy lost");
    Require(manifest.ui.desktopShortcutLocalizedNames.at("zh_CN") == "示例应用",
            "Manifest UI localized shortcut mismatch");

    std::string validationError;
    Require(ValidatePackageManifest(manifest, validationError),
            validationError.empty() ? "Manifest should validate" : validationError);

    std::vector<uint8_t> bytes = SerializePackageManifest(manifest);
    PackageManifest parsed;
    std::string parseError;
    Require(DeserializePackageManifest(bytes, parsed, parseError),
            parseError.empty() ? "Manifest should deserialize" : parseError);
    Require(parsed.payload.folders[0].algorithm == CompressionAlgorithm::ZSTD,
            "Manifest payload algorithm lost after codec round-trip");
    Require(parsed.payload.folders[0].source == "bin",
            "Manifest payload source lost after codec round-trip");
    Require(parsed.payload.folders[0].required,
            "Manifest payload required flag lost after codec round-trip");
    Require(parsed.install.installState.registries[0].values.at("channel").value == "stable",
            "Manifest registry installState value lost after codec round-trip");
    Require(parsed.install.installState.files[0].values.at("customValue").name == "customValue",
            "Manifest file installState value name lost after codec round-trip");
    Require(parsed.install.systemUninstallEntry.scope == UninstallEntryScope::LOCAL_MACHINE,
            "Manifest system uninstall entry lost after codec round-trip");
    Require(parsed.install.registryWrite.size() == 1,
            "Manifest registry.write lost after codec round-trip");
    Require(parsed.install.registryWrite[0].values.at("Publisher").value == "OpenAI",
            "Manifest registry.write lost after codec round-trip");
    Require(parsed.ui.desktopShortcutDefaultName == "Sample Shortcut",
            "Manifest UI default shortcut lost after codec round-trip");
    Require(parsed.components.components.size() == 1,
            "Manifest components lost after codec round-trip");
    Require(parsed.components.definitions[0].installAction.command.empty(),
            "Manifest component install action should round-trip");
    Require(parsed.lifecycle.uninstaller.cleanup.registry.deleteKeys.size() == 1,
            "Manifest uninstaller cleanup policy lost after codec round-trip");
    Require(parsed.install.installState.detect.legacy[0].installDirValue == "InstallPath",
            "Manifest legacy detect policy lost after codec round-trip");

    MetadataParser parser;
    ExtendedInstallationMetadata projected = parser.deserializeExtendedMetadata(bytes);
    Require(parser.validateMetadata(projected), "Projected manifest should validate as metadata");
    Require(projected.extendedPayloadMappings[0].target == "%InstallDir%",
            "Projected metadata target mismatch");
    Require(projected.installState.registries[0].values.at("channel").value == "stable",
            "Projected metadata should preserve v3 installState registry store");
    Require(projected.installerRegistryWrite.size() == 1 &&
                projected.installerRegistryWrite[0].values.at("Publisher").value == "OpenAI",
            "Projected metadata should expose v3 registry.write for current runtime");
    Require(projected.systemUninstallEntry.displayName == "Sample Desktop App" &&
                projected.systemUninstallEntry.scope == UninstallEntryScope::LOCAL_MACHINE,
            "Projected metadata should include v3 system uninstall entry");
    Require(projected.installState.detect.primary.registry == "mainRegistry" &&
                projected.installState.detect.primary.value == "installDir",
            "Projected metadata should include v3 installState detection");
    Require(projected.installState.detect.legacy.size() == 1 &&
                projected.installState.detect.legacy[0].id == "legacy_v2",
            "Projected metadata should include v3 legacy installState detection");
    Require(projected.uninstallerCleanup.missingManifestFallback == "safeDirectoryFallback",
            "Projected metadata should include v3 cleanup fallback policy");
}

void TestPackageManifestValidatorRejectsInvalidPayloadAndComponents() {
    PackagerConfiguration config = BuildConfigFixture();
    FolderInfo folder;
    folder.id = "app";
    folder.sourcePath = "bin";
    folder.targetPath = "%InstallDir%";
    CompressionResult result;
    result.originalSize = 10;
    result.compressedSize = 5;
    result.algorithm = CompressionAlgorithm::LZMA2_XZ;

    PackageManifestBuilder builder;
    PackageManifest manifest = builder.build({result}, {folder}, config);
    std::string error;
    Require(ValidatePackageManifest(manifest, error), "Baseline manifest should validate");

    PackageManifest badRange = manifest;
    badRange.payload.folders[0].offset = 10;
    badRange.payload.folders[0].compressedSize = 10;
    Require(!ValidatePackageManifest(badRange, error),
            "Validator should reject payload range outside data area");

    PackageManifest badFolderRef = manifest;
    badFolderRef.components.components[0].folders[0] = "missing";
    Require(!ValidatePackageManifest(badFolderRef, error),
            "Validator should reject unknown component folder reference");

    PackageManifest badDependency = manifest;
    ComponentConfig extra = badDependency.components.components[0];
    extra.id = "addon";
    extra.dependsOn.push_back("addon");
    badDependency.components.components.push_back(extra);
    Require(!ValidatePackageManifest(badDependency, error),
            "Validator should reject component dependency cycle");

    PackageManifest badSourceType = manifest;
    badSourceType.components.components[0].source.type = static_cast<ComponentSourceType>(99);
    Require(!ValidatePackageManifest(badSourceType, error),
            "Validator should reject invalid component source type");
}

void TestPathResolverExpandEnvironmentVariables() {
    InstallerPathResolver resolver;
    std::string expanded = resolver.expandEnvironmentVariables("%AppData%\\SampleDesktopApp");
    Require(!expanded.empty(), "Expanded AppData path should not be empty");
    Require(expanded.find('%') == std::string::npos, "Expanded path should not contain raw % tokens");
    Require(expanded.find("SampleDesktopApp") != std::string::npos,
            "Expanded path should keep suffix");
}

void TestInstallStateStoreApplyAndCleanup() {
    fs::path root = CreateTestRoot("install_state_store");
    const std::string registryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\InstallStateStore";
    deleteRegistryPath(registryPath);

    InstallStateConfig config;
    InstallStateRegistryStoreConfig registryStore;
    registryStore.id = "main";
    registryStore.path = registryPath;
    InstallStateValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    installDirValue.value = "%InstallDir%";
    installDirValue.type = RegistryValueType::EXPAND_STRING;
    registryStore.values["installDir"] = installDirValue;
    InstallStateValueConfig stateValue;
    stateValue.key = "InstallState";
    stateValue.value = "%InstallState%";
    registryStore.values["installState"] = stateValue;
    InstallStateValueConfig appIdValue;
    appIdValue.key = "AppId";
    appIdValue.value = "%AppId%";
    registryStore.values["appId"] = appIdValue;
    config.registries.push_back(registryStore);

    InstallStateFileStoreConfig fileStore;
    fileStore.id = "file";
    fileStore.path = (root / "state" / "install-state.json").string();
    fileStore.format = "json";
    InstallStateValueConfig customFileValue;
    customFileValue.name = "custom";
    customFileValue.value = "%AppId%:%InstallState%";
    fileStore.values["customValue"] = customFileValue;
    config.files.push_back(fileStore);

    InstallerPathResolver resolver;
    InstallStateContext context;
    context.installDir = (root / "App").string();
    context.version = "1.2.3";
    context.appName = "Sample Desktop App";
    context.appId = "SampleDesktopApp";
    context.installSource = "C:\\Installer\\setup.exe";
    context.state = "installed";
    context.userName = "schema-user";

    Require(ApplyInstallState(config, context, resolver), "ApplyInstallState should succeed");
    std::string value;
    Require(readRegistryStringValue(registryPath, "InstallDir", value),
            "InstallDir registry value should be written");
    Require(value == context.installDir, "InstallDir token should expand");
    Require(readRegistryStringValue(registryPath, "AppId", value),
            "AppId registry value should be written");
    Require(value == "SampleDesktopApp", "AppId token should expand");

    nlohmann::json fileJson;
    Require(readManifest(fileStore.path, fileJson), "InstallState file store should be readable JSON");
    Require(fileJson["custom"] == "SampleDesktopApp:installed",
            "File store custom value should expand tokens");

    Require(CleanupInstallState(config, "markUninstalled", context, resolver),
            "markUninstalled cleanup should succeed");
    Require(readRegistryStringValue(registryPath, "InstallState", value),
            "InstallState registry value should still exist after markUninstalled");
    Require(value == "uninstalled", "markUninstalled should update install state");

    Require(CleanupInstallState(config, "keep", context, resolver),
            "keep cleanup should succeed");
    Require(fs::exists(fileStore.path), "keep cleanup should not remove file store");

    Require(CleanupInstallState(config, "delete", context, resolver),
            "delete cleanup should succeed");
    Require(!readRegistryStringValue(registryPath, "InstallDir", value),
            "delete cleanup should remove registry values");
    Require(!fs::exists(fileStore.path), "delete cleanup should remove file store");
    deleteRegistryPath(registryPath);
}

void TestWriteManifestPreservesExplicitCleanupSchema() {
    fs::path root = CreateTestRoot("manifest_schema");
    fs::path manifestPath = root / "install.manifest.json";

    const std::string stateRegistryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\Manifest";
    InstallStateConfig installState = BuildTestInstallStateConfig(stateRegistryPath);

    UninstallCleanupConfig cleanup;
    NamedCleanupEntry process;
    process.name = "SampleDesktopApp.exe";
    cleanup.processes.push_back(process);
    RegistryEntry registryEntry;
    registryEntry.path = "HKEY_CURRENT_USER\\Software\\SampleDesktopApp";
    registryEntry.key = "";
    cleanup.registry.legacyKeys.push_back(registryEntry);
    UninstallEntryCleanup uninstallEntry;
    uninstallEntry.name = "SampleDesktopApp";
    uninstallEntry.scope = UninstallEntryScope::ANY;
    cleanup.uninstallEntries.push_back(uninstallEntry);
    NamedCleanupEntry shortcut;
    shortcut.name = "Sample Desktop App";
    cleanup.shortcuts.push_back(shortcut);
    NamedCleanupEntry startup;
    startup.name = "SampleDesktopApp";
    cleanup.startup.push_back(startup);
    UninstallCleanupRule pathRule;
    pathRule.path = "%LocalAppData%\\SampleDesktopApp\\Cache";
    pathRule.recursive = true;
    pathRule.onlyIfEmpty = false;
    cleanup.paths.push_back(pathRule);
    UninstallerCleanupConfigV3 uninstallerCleanupV3;
    uninstallerCleanupV3.paths.push_back(pathRule);

    bool ok = writeManifest(manifestPath.string(),
                            "SampleDesktopApp",
                            "Sample Desktop App",
                            "1.2.3",
                            "C:\\Apps\\SampleDesktopApp",
                            {"C:\\Apps\\SampleDesktopApp"},
                            cleanup,
                            {"C:\\Apps\\SampleDesktopApp\\SampleDesktopApp.exe"},
                            {"SampleDesktopApp.exe"},
                            true,
                            true,
                            "Sample Desktop App",
                            installState,
                            "delete",
                            "C:\\Apps\\SampleDesktopApp\\uninstall.exe",
                            "zh_CN",
                            {},
                            {},
                            false,
                            {},
                            {},
                            uninstallerCleanupV3);
    Require(ok, "writeManifest should succeed");

    nlohmann::json manifest;
    Require(readManifest(manifestPath.string(), manifest), "Manifest should round-trip as JSON");
    Require(!manifest.contains("lifecycleUninstallCleanup"),
            "Manifest should not persist legacy lifecycleUninstallCleanup");
    Require(manifest["systemUninstallEntry"]["writtenEntries"].size() == 1,
            "Manifest should persist system uninstall entry snapshot");
    Require(manifest["installState"]["registries"].size() == 1,
            "Manifest should persist v3 installState registries");
    Require(manifest["installState"]["registries"][0]["path"] == stateRegistryPath,
            "Manifest should persist v3 installState registry path");
    Require(manifest["uninstaller"]["cleanup"]["installState"] == "delete",
            "Manifest should persist installState cleanup mode");
    Require(manifest["app"]["id"] == "SampleDesktopApp",
            "Manifest should persist v3 app snapshot");
    Require(manifest["installer"]["payload"]["files"].is_array(),
            "Manifest should persist v3 payload file snapshot");
    Require(manifest["uninstaller"]["cleanup"]["paths"].size() == 1,
            "Manifest should persist v3 uninstall cleanup paths");
    Require(manifest["uninstaller"]["cleanup"]["actual"]["systemUninstallEntry"].size() == 1,
            "Manifest should persist actual system uninstall entries");
}

void TestInstallerArgsParseUpgrade() {
    CliSupport console;
    std::vector<std::string> args = {"installer.exe", "--upgrade", "--silent",
                                     "--uninstall-manifest", "C:\\Apps\\Sample\\install.manifest.json"};
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }

    CliSupport::InstallerArgs parsed = console.parseInstallerArgs(static_cast<int>(argv.size()), argv.data());
    Require(parsed.upgrade, "Installer args should parse --upgrade");
    Require(parsed.silent, "Installer args should still parse --silent with --upgrade");
    Require(parsed.uninstallManifestPath == "C:\\Apps\\Sample\\install.manifest.json",
            "Installer args should parse hidden --uninstall-manifest");
}

void TestInstallManifestPersistsPreviousInstallOptions() {
    fs::path root = CreateTestRoot("previous_install_options");
    fs::path manifestPath = root / "install.manifest.json";

    InstallStateConfig installState =
        BuildTestInstallStateConfig("HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\PreviousOptions");

    Require(writeManifest(manifestPath.string(),
                          "SampleDesktopApp",
                          "Sample Desktop App",
                          "1.2.3",
                          root.string(),
                          {root.string()},
                          UninstallCleanupConfig{},
                          {},
                          {},
                          true,
                          false,
                          "",
                          installState,
                          "markUninstalled",
                          "",
                          "zh_CN",
                          {},
                          {"core", "tools"},
                          true),
            "writeManifest should persist previous install options");

    PreviousInstallOptions options;
    std::string error;
    Require(loadPreviousInstallOptions(manifestPath.string(), options, error),
            error.empty() ? "loadPreviousInstallOptions should succeed" : error);
    Require(options.autoStartup, "Previous autoStartup should round-trip");
    Require(!options.desktopIcon, "Previous desktopIcon should round-trip");
    Require(options.installAllComponents, "Previous installAllComponents should round-trip");
    Require(options.languageCode == "zh_CN", "Previous language should round-trip");
    Require(options.selectedComponentIds.size() == 2 &&
                options.selectedComponentIds[0] == "core" &&
                options.selectedComponentIds[1] == "tools",
            "Previous selected components should round-trip");

    WriteTextFile(manifestPath, R"({"installAutoStartup":true,"installDesktopIcon":true,"language":"zh_CN"})");
    Require(!loadPreviousInstallOptions(manifestPath.string(), options, error),
            "Missing selectedComponentIds/installAllComponents should fail previous option loading");
}

void TestBuildInstallExecutionPlanUsesV3InstallStateDiscovery() {
    fs::path root = CreateTestRoot("install_plan_v3_state");
    fs::path previousInstallDir = root / "OldInstall";
    fs::create_directories(previousInstallDir);
    WriteTextFile(previousInstallDir / "install.manifest.json", R"({"desktopShortcutDisplayName":"Old Shortcut","installDir":"PLACEHOLDER"})");

    const std::string registryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\InstallPlan";
    RegistryEntry entry;
    entry.path = registryPath;
    entry.key = "InstallDir";
    entry.value = previousInstallDir.string();
    entry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(entry, previousInstallDir.string(), RegistryValueType::STRING),
            "Failed to seed install root registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Sample Desktop App";
    metadata.appId = "SampleDesktopApp";
    metadata.appDirectoryName = "SampleDesktopApp";
    metadata.installDefaultDir = "%ProgramFiles%\\SampleDesktopApp";
    metadata.installState = BuildTestInstallStateConfig(registryPath);
    metadata.installState.detect.primary.registry = "main";
    metadata.installState.detect.primary.value = "installDir";

    InstallServiceOptions options;
    options.installPath = "C:\\NewInstallPath";
    options.installPathExplicit = false;
    InstallerPathResolver resolver;
    InstallExecutionPlan plan;
    std::string error;
    bool built = BuildInstallExecutionPlan(metadata, resolver, options, plan, error);

    deleteRegistryPath(registryPath);

    Require(built, error.empty() ? "BuildInstallExecutionPlan should succeed" : error);
    Require(plan.hasPreviousInstall, "Plan should detect previous install from v3 installState");
    Require(normalizePathForCompare(plan.previousInstallDir) ==
                normalizePathForCompare(previousInstallDir.string()),
            "Previous install dir should come from configured v3 installState");
    Require(plan.pathDecision.mode == InstallTargetMode::OverwriteInstall,
            "Plan should enter overwrite mode when previous install is found");
    Require(normalizePathForCompare(plan.pathDecision.resolvedInstallRoot) ==
                normalizePathForCompare("C:\\NewInstallPath"),
            "Overwrite install should preserve the requested install root");
    Require(normalizePathForCompare(plan.pathDecision.cleanupTargetInstallRoot) ==
                normalizePathForCompare(previousInstallDir.string()),
            "Overwrite cleanup target should remain the previous install root");
}

void TestBuildInstallExecutionPlanUpgradeUsesV3InstallStateRegistry() {
    fs::path root = CreateTestRoot("upgrade_plan_registry");
    fs::path previousInstallDir = root / "PreviousInstall";
    fs::create_directories(previousInstallDir);
    WriteTextFile(previousInstallDir / "install.manifest.json",
                  R"({"installAutoStartup":true,"installDesktopIcon":false,"language":"zh_CN","installAllComponents":false,"selectedComponentIds":["core"]})");

    const std::string registryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UpgradePlan";
    RegistryEntry entry;
    entry.path = registryPath;
    entry.key = "InstallDir";
    entry.value = previousInstallDir.string();
    entry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(entry, previousInstallDir.string(), RegistryValueType::STRING),
            "Failed to seed upgrade installState registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Sample Desktop App";
    metadata.appId = "SampleDesktopApp";
    metadata.appDirectoryName = "SampleDesktopApp";
    metadata.installDefaultDir = "%ProgramFiles%\\SampleDesktopApp";
    metadata.installState = BuildTestInstallStateConfig(registryPath);
    metadata.installState.detect.primary.registry = "main";
    metadata.installState.detect.primary.value = "installDir";

    InstallServiceOptions options;
    options.upgradeMode = true;
    options.installPath = "C:\\IgnoredDestination";
    options.installPathExplicit = true;
    InstallerPathResolver resolver;
    InstallExecutionPlan plan;
    std::string error;
    bool built = BuildInstallExecutionPlan(metadata, resolver, options, plan, error);

    deleteRegistryPath(registryPath);

    Require(built, error.empty() ? "Upgrade plan should succeed" : error);
    Require(plan.hasPreviousInstall, "Upgrade plan should require previous install");
    Require(plan.pathDecision.mode == InstallTargetMode::UpgradeInstall,
            "Upgrade plan should use UpgradeInstall mode");
    Require(normalizePathForCompare(plan.pathDecision.resolvedInstallRoot) ==
                normalizePathForCompare(previousInstallDir.string()),
            "Upgrade install root should come from v3 installState registry");
    Require(normalizePathForCompare(plan.pathDecision.cleanupTargetInstallRoot) ==
                normalizePathForCompare(previousInstallDir.string()),
            "Upgrade cleanup target should be previous install root");
}

void TestBuildInstallExecutionPlanUpgradeAllowsMissingManifest() {
    fs::path root = CreateTestRoot("upgrade_plan_missing_manifest");
    fs::path previousInstallDir = root / "PreviousInstall";
    fs::create_directories(previousInstallDir);

    const std::string registryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UpgradePlanMissingManifest";
    RegistryEntry entry;
    entry.path = registryPath;
    entry.key = "InstallDir";
    entry.value = previousInstallDir.string();
    entry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(entry, previousInstallDir.string(), RegistryValueType::STRING),
            "Failed to seed upgrade installState registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Sample Desktop App";
    metadata.appId = "SampleDesktopApp";
    metadata.installState = BuildTestInstallStateConfig(registryPath);
    metadata.installState.detect.primary.registry = "main";
    metadata.installState.detect.primary.value = "installDir";

    InstallServiceOptions options;
    options.upgradeMode = true;
    InstallerPathResolver resolver;
    InstallExecutionPlan plan;
    std::string error;
    bool built = BuildInstallExecutionPlan(metadata, resolver, options, plan, error);

    deleteRegistryPath(registryPath);

    Require(built, error.empty() ? "Upgrade plan should allow missing previous manifest" : error);
    Require(plan.previousManifest.empty(), "Upgrade plan should leave previous manifest empty when it is missing");
    Require(normalizePathForCompare(plan.previousInstallDir) == normalizePathForCompare(previousInstallDir.string()),
            "Upgrade plan should still fix install dir to detected previous install dir");
}

void TestInstallStateResolverUsesLegacyDetectInOrder() {
    fs::path root = CreateTestRoot("detect_legacy_order");
    fs::path primaryDir = root / "PrimaryInstall";
    fs::path legacyDir = root / "LegacyInstall";
    fs::create_directories(legacyDir);
    WriteTextFile(legacyDir / "install.manifest.json", R"({"appVersion":"1.0.0"})");

    const std::string primaryPath =
        "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\DetectPrimaryMissing";
    const std::string legacyPath =
        "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\DetectLegacy";
    RegistryEntry entry;
    entry.path = primaryPath;
    entry.key = "InstallDir";
    entry.value = primaryDir.string();
    entry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(entry, entry.value, entry.type),
            "Failed to seed primary detect registry value");
    entry.path = legacyPath;
    entry.key = "LegacyPath";
    entry.value = legacyDir.string();
    Require(writeRegistryValue(entry, entry.value, entry.type),
            "Failed to seed legacy detect registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Sample Desktop App";
    metadata.appId = "SampleDesktopApp";
    metadata.installState = BuildTestInstallStateConfig(primaryPath);
    metadata.installState.detect.primary.registry = "main";
    metadata.installState.detect.primary.value = "installDir";
    InstalledInstanceDetectInstallStateConfig legacyDetect;
    legacyDetect.id = "v2";
    legacyDetect.path = legacyPath;
    legacyDetect.installDirValue = "LegacyPath";
    metadata.installState.detect.legacy.push_back(legacyDetect);

    InstallerPathResolver resolver;
    InstalledInstanceInfo instance;
    std::string error;
    const bool found = resolveInstalledInstanceFromInstallState(metadata, resolver, instance, &error);
    deleteRegistryPath(primaryPath);
    deleteRegistryPath(legacyPath);

    Require(found, error.empty() ? "Legacy detect should resolve install dir" : error);
    Require(normalizePathForCompare(instance.installDir) == normalizePathForCompare(legacyDir.string()),
            "Legacy detect should be used after invalid primary detect");
    Require(instance.detectSource == "legacy:v2",
            "Resolver should report legacy detect source");
    Require(!instance.manifestPath.empty(), "Resolver should return legacy manifest path");
}

void TestBuildInstallExecutionPlanUpgradeUsesLegacyDetectRegistry() {
    fs::path root = CreateTestRoot("upgrade_plan_legacy_detect");
    fs::path previousInstallDir = root / "PreviousInstall";
    fs::create_directories(previousInstallDir);
    WriteTextFile(previousInstallDir / "install.manifest.json",
                  R"({"installAutoStartup":true,"installDesktopIcon":false,"language":"zh_CN","installAllComponents":false,"selectedComponentIds":["core"]})");

    const std::string registryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UpgradePlanLegacyDetect";
    RegistryEntry entry;
    entry.path = registryPath;
    entry.key = "InstallPath";
    entry.value = previousInstallDir.string();
    entry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(entry, entry.value, entry.type),
            "Failed to seed legacy upgrade detect registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Sample Desktop App";
    metadata.appId = "SampleDesktopApp";
    metadata.installDefaultDir = "%ProgramFiles%\\SampleDesktopApp";
    InstalledInstanceDetectInstallStateConfig legacyDetect;
    legacyDetect.id = "legacy_v2";
    legacyDetect.path = registryPath;
    legacyDetect.installDirValue = "InstallPath";
    metadata.installState.detect.legacy.push_back(legacyDetect);

    InstallServiceOptions options;
    options.upgradeMode = true;
    InstallerPathResolver resolver;
    InstallExecutionPlan plan;
    std::string error;
    const bool built = BuildInstallExecutionPlan(metadata, resolver, options, plan, error);
    deleteRegistryPath(registryPath);

    Require(built, error.empty() ? "Upgrade plan should succeed through legacy detect" : error);
    Require(plan.hasPreviousInstall, "Upgrade plan should detect previous install through legacy detect");
    Require(normalizePathForCompare(plan.previousInstallDir) ==
                normalizePathForCompare(previousInstallDir.string()),
            "Upgrade install root should come from legacy detect registry");
}

void TestCompareSemanticVersion() {
    Require(compareSemanticVersion("1.2.4", "1.2.3") > 0, "Higher patch version should compare greater");
    Require(compareSemanticVersion("1.2.3", "1.2.3") == 0, "Equal versions should compare equal");
    Require(compareSemanticVersion("1.2.3", "1.2.4") < 0, "Lower patch version should compare smaller");
}

void TestAppendPathLeafIfMissing() {
    Require(normalizePathForCompare(
                appendPathLeafIfMissing("C:\\Apps", "SampleDesktopApp")) ==
                normalizePathForCompare("C:\\Apps\\SampleDesktopApp"),
            "Selected install directory should be completed with app id when leaf differs");
    Require(normalizePathForCompare(
                appendPathLeafIfMissing("C:\\Apps\\SampleDesktopApp", "SampleDesktopApp")) ==
                normalizePathForCompare("C:\\Apps\\SampleDesktopApp"),
            "Selected install directory should not duplicate app id when leaf already matches");
}

void TestSameRootUpgradeCleanupUsesPreviousManifestFiles() {
    fs::path root = CreateTestRoot("upgrade_cleanup_same_root");
    fs::path previousInstallDir = root / "InstallRoot";
    fs::create_directories(previousInstallDir);

    fs::path staleFile = previousInstallDir / "stale.txt";
    fs::path keptFile = previousInstallDir / "user.dat";
    WriteTextFile(staleFile, "stale");
    WriteTextFile(keptFile, "keep");
    WriteTextFile(previousInstallDir / "install.manifest.json",
                  R"({"files":["stale.txt"],"appVersion":"1.0.0"})");

    CliSupport console;
    bool ok = cleanupPreviousInstallForUpgrade((previousInstallDir / "install.manifest.json").string(),
                                               previousInstallDir.string(),
                                               previousInstallDir.string(),
                                               console);

    Require(ok, "Same-root upgrade cleanup should succeed");
    Require(fs::exists(previousInstallDir), "Same-root cleanup should keep the install root");
    Require(!fs::exists(staleFile), "Same-root cleanup should remove files listed by previous manifest");
    Require(fs::exists(keptFile), "Same-root cleanup should not remove files missing from manifest");
    Require(!fs::exists(previousInstallDir / "install.manifest.json"),
            "Same-root cleanup should remove the previous manifest");
}

#ifdef _WIN32
void TestSameRootUpgradeCleanupIsolatesPayloadSubdirectories() {
    fs::path root = CreateTestRoot("upgrade_cleanup_isolate_subdirs");
    fs::path previousInstallDir = root / "InstallRoot";
    fs::create_directories(previousInstallDir / "resources" / "node_modules");
    WriteTextFile(previousInstallDir / "resources" / "node_modules" / "a.js", "stale");
    WriteTextFile(previousInstallDir / "root-stale.dll", "stale");
    WriteTextFile(previousInstallDir / "user.dat", "keep");
    WriteTextFile(previousInstallDir / "install.manifest.json",
                  R"({"files":["resources/node_modules/a.js","root-stale.dll"],"appVersion":"1.0.0"})");

    UpgradeCleanupResult result = runPreviousInstallCleanupWithWatchdog(
        (previousInstallDir / "install.manifest.json").string(),
        previousInstallDir.string(),
        previousInstallDir.string(),
        {previousInstallDir.string()});

    bool pendingFound = false;
    for (const auto& entry : fs::directory_iterator(previousInstallDir)) {
        if (entry.is_directory() &&
            entry.path().filename().string().rfind(".mti_delete_pending_resources_", 0) == 0) {
            pendingFound = true;
            break;
        }
    }

    Require(result.success, "Synchronous same-root isolated cleanup should succeed");
    Require(!result.partial, "Synchronous isolated cleanup should complete before installation continues");
    Require(fs::exists(previousInstallDir), "Synchronous cleanup must not rename the install root");
    Require(!fs::exists(previousInstallDir / "resources"), "Payload child directory should be isolated");
    Require(!fs::exists(previousInstallDir / "root-stale.dll"),
            "Isolated cleanup should still remove root-level files listed by manifest");
    Require(fs::exists(previousInstallDir / "user.dat"), "Root-level user file should be kept");
    Require(!pendingFound, "Pending payload child should be removed before cleanup returns");
}

void TestSameRootUpgradeCleanupRemovesManifestFilesOutsideReplacementTarget() {
    fs::path root = CreateTestRoot("upgrade_cleanup_outside_replacement_target");
    fs::path previousInstallDir = root / "InstallRoot";
    fs::create_directories(previousInstallDir / "resources");
    fs::create_directories(previousInstallDir / "legacy");
    WriteTextFile(previousInstallDir / "resources" / "old.js", "stale");
    WriteTextFile(previousInstallDir / "legacy" / "old.dat", "stale");
    WriteTextFile(previousInstallDir / "user.dat", "keep");
    WriteTextFile(previousInstallDir / "install.manifest.json",
                  R"({"files":["resources/old.js","legacy/old.dat"],"appVersion":"1.0.0"})");

    UpgradeCleanupResult result = runPreviousInstallCleanupWithWatchdog(
        (previousInstallDir / "install.manifest.json").string(),
        previousInstallDir.string(),
        previousInstallDir.string(),
        {(previousInstallDir / "resources").string()});

    Require(result.success, "Same-root cleanup with replacement target should succeed");
    Require(!fs::exists(previousInstallDir / "resources"),
            "Replacement target directory should be isolated and cleaned");
    Require(!fs::exists(previousInstallDir / "legacy" / "old.dat"),
            "Manifest file outside replacement target should still be removed");
    Require(!fs::exists(previousInstallDir / "legacy"),
            "Empty legacy directory should be removed after manifest file cleanup");
    Require(fs::exists(previousInstallDir / "user.dat"),
            "Files not listed by manifest should be kept");
}
#endif

void TestUpgradeCleanupMissingManifestRemovesSafeDirectoryContents() {
    fs::path root = CreateTestRoot("upgrade_cleanup_missing_manifest");
    fs::path previousInstallDir = root / "InstallRoot";
    fs::create_directories(previousInstallDir / "subdir");

    WriteTextFile(previousInstallDir / "stale.txt", "stale");
    WriteTextFile(previousInstallDir / "subdir" / "nested.txt", "nested");

    CliSupport console;
    bool ok = cleanupPreviousInstallForUpgrade((previousInstallDir / "missing.manifest.json").string(),
                                               previousInstallDir.string(),
                                               previousInstallDir.string(),
                                               console);

    Require(ok, "Missing-manifest upgrade cleanup should clean safe install root contents");
    Require(fs::exists(previousInstallDir), "Same-root missing-manifest cleanup should keep the root");
    Require(!fs::exists(previousInstallDir / "stale.txt"),
            "Missing-manifest cleanup should remove direct files");
    Require(!fs::exists(previousInstallDir / "subdir"),
            "Missing-manifest cleanup should remove child directories");
}

void TestCleanupUpgradeSystemArtifactsExecutesExplicitRules() {
    fs::path root = CreateTestRoot("upgrade_cleanup_execute");
    fs::path previousInstallDir = root / "PreviousInstall";
    fs::create_directories(previousInstallDir);

    fs::path legacyPath = root / "LegacyData";
    WriteTextFile(legacyPath / "stale.txt", "legacy");

    const std::string registryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UpgradeCleanup";
    const std::string legacyUninstallKey = "SchemaRegressionTestsUpgradeCleanupLegacyUninstall";
    const std::string legacyUninstallDisplayName = "Schema Regression Upgrade Cleanup Legacy";
    const std::string legacyUninstallPath =
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + legacyUninstallKey;
    RegistryEntry seededRegistry;
    seededRegistry.path = registryPath;
    seededRegistry.key = "LegacyValue";
    seededRegistry.value = "stale";
    seededRegistry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(seededRegistry, seededRegistry.value, seededRegistry.type),
            "Failed to seed upgrade cleanup registry value");

    ExtendedInstallationMetadata metadata;
    RegistryEntry legacyRegistry;
    legacyRegistry.path = registryPath;
    legacyRegistry.key = "";
    metadata.installerCleanup.registry.deleteKeys.push_back(registryPath);
    SystemUninstallEntryCleanupItem legacyUninstallEntry;
    legacyUninstallEntry.displayName = legacyUninstallDisplayName;
    legacyUninstallEntry.scope = UninstallEntryScope::CURRENT_USER;
    metadata.installerCleanup.systemUninstallEntry.legacyEntries.push_back(legacyUninstallEntry);
#ifdef _WIN32
    deleteUninstallRegistryEntry(legacyUninstallKey, false);
    Require(writeUninstallRegistryEntry(legacyUninstallKey,
                                        legacyUninstallDisplayName,
                                        "1.0.0",
                                        previousInstallDir.string(),
                                        (previousInstallDir / "legacy_uninstall.exe").string(),
                                        false,
                                        "Schema Tests"),
            "Failed to seed upgrade cleanup legacy system uninstall entry");
#endif

    UninstallCleanupRule cleanupRule;
    cleanupRule.path = legacyPath.string();
    cleanupRule.recursive = true;
    cleanupRule.onlyIfEmpty = false;
    metadata.installerCleanup.paths.push_back(cleanupRule);

    InstallerPathResolver resolver;
    CliSupport console;
    bool progressCompleted = false;
    bool ok = cleanupUpgradeSystemArtifacts("",
                                            previousInstallDir.string(),
                                            metadata,
                                            resolver,
                                            console,
                                            [&](const UpgradeCleanupProgressInfo& info) {
                                                if (info.progress >= 1.0f) {
                                                    progressCompleted = true;
                                                }
                                            });

    Require(ok, "cleanupUpgradeSystemArtifacts should succeed");
    Require(progressCompleted, "Upgrade cleanup should report completion");
    Require(!fs::exists(legacyPath), "Upgrade cleanup should remove configured extra path");

    std::string registryValue;
    Require(!readRegistryStringValue(registryPath, "LegacyValue", registryValue),
            "Upgrade cleanup should remove configured registry path");
#ifdef _WIN32
    Require(!readRegistryStringValue(legacyUninstallPath, "DisplayName", registryValue),
            "Upgrade cleanup should remove configured legacy system uninstall entry");
#endif
    deleteRegistryPath(registryPath);
}

#ifdef _WIN32
void TestDeleteUninstallEntryMatchesDisplayName() {
    const std::string keyName = "SchemaRegressionTestsLegacyKey";
    const std::string displayName = "Schema Regression Legacy Display";
    const std::string uninstallPath =
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;

    deleteUninstallRegistryEntry(keyName, false);
    Require(writeUninstallRegistryEntry(keyName,
                                        displayName,
                                        "1.0.0",
                                        "C:\\SchemaRegressionTests\\Legacy",
                                        "C:\\SchemaRegressionTests\\Legacy\\uninstall.exe",
                                        false,
                                        "Schema Tests"),
            "Failed to seed uninstall registry entry");

    std::string value;
    Require(readRegistryStringValue(uninstallPath, "DisplayName", value),
            "Seeded uninstall registry entry should exist");
    Require(deleteSystemUninstallEntryByDisplayName(displayName, UninstallEntryScope::CURRENT_USER),
            "Uninstall registry deletion should match DisplayName when key differs");
    Require(!readRegistryStringValue(uninstallPath, "DisplayName", value),
            "Uninstall registry entry should be removed by DisplayName");
}

void TestDeleteUninstallEntryBothScopeAttemptsAllViews() {
    const std::string userKeyName = "SchemaRegressionTestsBothUserKey";
    const std::string machineKeyName = "SchemaRegressionTestsBothMachineKey";
    const std::string displayName = "Schema Regression Both Scope Display";
    const std::string userUninstallPath =
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + userKeyName;
    const std::string machineUninstallPath =
        "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + machineKeyName;

    deleteUninstallRegistryEntry(userKeyName, false);
    deleteUninstallRegistryEntry(machineKeyName, true);
    Require(writeUninstallRegistryEntry(userKeyName,
                                        displayName,
                                        "1.0.0",
                                        "C:\\SchemaRegressionTests\\BothUser",
                                        "C:\\SchemaRegressionTests\\BothUser\\uninstall.exe",
                                        false,
                                        "Schema Tests"),
            "Failed to seed current-user uninstall registry entry");
    const bool machineSeeded = writeUninstallRegistryEntry(machineKeyName,
                                                          displayName,
                                                          "1.0.0",
                                                          "C:\\SchemaRegressionTests\\BothMachine",
                                                          "C:\\SchemaRegressionTests\\BothMachine\\uninstall.exe",
                                                          true,
                                                          "Schema Tests");

    std::string value;
    Require(readRegistryStringValue(userUninstallPath, "DisplayName", value),
            "Seeded current-user uninstall entry should exist");
    Require(deleteSystemUninstallEntryByDisplayName(displayName, UninstallEntryScope::BOTH),
            "Both-scope uninstall registry deletion should remove at least one matching entry");
    Require(!readRegistryStringValue(userUninstallPath, "DisplayName", value),
            "Both-scope deletion should remove current-user uninstall entry");
    if (machineSeeded) {
        Require(!readRegistryStringValue(machineUninstallPath, "DisplayName", value),
                "Both-scope deletion should remove local-machine uninstall entry without short-circuiting");
    }
}
#endif

void TestUpgradeExtraPathCleanupWithWatchdogRemovesPath() {
    fs::path root = CreateTestRoot("upgrade_extra_path_worker");
    fs::path previousInstallDir = root / "PreviousInstall";
    fs::path cleanupPath = root / "LegacyData";
    fs::create_directories(previousInstallDir);
    fs::create_directories(cleanupPath);
    WriteTextFile(cleanupPath / "stale.txt", "legacy");

    UninstallCleanupRule rule;
    rule.path = cleanupPath.string();
    rule.recursive = true;
    rule.onlyIfEmpty = false;

    InstallerPathResolver resolver;
    UpgradeCleanupPolicy policy;
    policy.totalTimeoutMs = 30000;
    UpgradeCleanupResult result = runUpgradeExtraPathCleanupWithWatchdog({rule},
                                                                         previousInstallDir.string(),
                                                                         resolver,
                                                                         {},
                                                                         {},
                                                                         policy);
    Require(result.success, "Extra path cleanup worker should succeed");
    Require(!fs::exists(cleanupPath), "Extra path cleanup worker should remove recursive path");
}

#ifdef _WIN32
void TestUpgradeCleanupRunsInProcessAndCompletes() {
    fs::path root = CreateTestRoot("upgrade_cleanup_in_process");
    fs::path previousInstallDir = root / "InstallRoot";
    fs::create_directories(previousInstallDir);
    fs::path staleFile = previousInstallDir / "stale.txt";
    WriteTextFile(staleFile, "stale");
    WriteTextFile(previousInstallDir / "install.manifest.json",
                  R"({"files":["stale.txt"],"appVersion":"1.0.0"})");

    UpgradeCleanupPolicy policy;
    policy.totalTimeoutMs = 300;
    policy.itemStaleTimeoutMs = 100;
    UpgradeCleanupResult result = runPreviousInstallCleanupWithWatchdog(
        (previousInstallDir / "install.manifest.json").string(),
        previousInstallDir.string(),
        previousInstallDir.string(),
        {},
        {},
        {},
        policy);

    Require(result.success, "In-process cleanup should succeed");
    Require(!result.timedOut, "In-process cleanup should not rely on worker-process timeout");
    Require(!fs::exists(staleFile), "In-process cleanup should remove stale file");
}

void TestUpgradeCleanupSkipsReparsePointTargets() {
    fs::path root = CreateTestRoot("upgrade_cleanup_reparse_skip");
    fs::path previousInstallDir = root / "InstallRoot";
    fs::path externalTarget = root / "ExternalTarget";
    fs::create_directories(previousInstallDir);
    fs::create_directories(externalTarget);
    WriteTextFile(externalTarget / "keep.txt", "keep");

    fs::path linkPath = previousInstallDir / "linked_target";
    DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
    flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (!CreateSymbolicLinkW(linkPath.wstring().c_str(), externalTarget.wstring().c_str(), flags)) {
        return;
    }

    CliSupport console;
    bool ok = cleanupPreviousInstallForUpgrade((previousInstallDir / "missing.manifest.json").string(),
                                               previousInstallDir.string(),
                                               previousInstallDir.string(),
                                               console);

    Require(ok, "Cleanup should succeed when reparse point is skipped");
    Require(fs::exists(externalTarget / "keep.txt"),
            "Cleanup should not follow reparse point into external target");
}
#endif

void TestUninstallFromManifestExecutesExplicitCleanup() {
    fs::path root = CreateTestRoot("uninstall_execute");
    fs::path installDir = root / "InstallRoot";
    fs::create_directories(installDir);

    fs::path installedFile = installDir / "SampleDesktopApp.exe";
    WriteTextFile(installedFile, "binary");

    fs::path cleanupPath = root / "UserCache";
    WriteTextFile(cleanupPath / "cache.dat", "cache");

    const std::string installStatePath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UninstallState";
    const std::string legacyRegistryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UninstallLegacy";
    const std::string legacyUninstallKey = "SchemaRegressionTestsLegacyUninstallKey";
    const std::string legacyUninstallDisplayName = "Schema Regression Legacy Uninstall";
    const std::string legacyUninstallPath =
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + legacyUninstallKey;

    InstallStateConfig installStateConfig = BuildTestInstallStateConfig(installStatePath);

    RegistryEntry legacyRegistry;
    legacyRegistry.path = legacyRegistryPath;
    legacyRegistry.key = "LegacyValue";
    legacyRegistry.value = "legacy";
    legacyRegistry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(legacyRegistry, legacyRegistry.value, legacyRegistry.type),
            "Failed to seed uninstall legacy registry value");
#ifdef _WIN32
    deleteUninstallRegistryEntry(legacyUninstallKey, false);
    Require(writeUninstallRegistryEntry(legacyUninstallKey,
                                        legacyUninstallDisplayName,
                                        "1.0.0",
                                        installDir.string(),
                                        (installDir / "legacy_uninstall.exe").string(),
                                        false,
                                        "Schema Tests"),
            "Failed to seed uninstall legacy system uninstall entry");
#endif

    UninstallCleanupConfig uninstallCleanup;
    NamedCleanupEntry process;
    process.name = "DefinitelyNotRunning_MTI_Test.exe";
    uninstallCleanup.processes.push_back(process);

    RegistryEntry legacyCleanupEntry;
    legacyCleanupEntry.path = legacyRegistryPath;
    legacyCleanupEntry.key = "";
    uninstallCleanup.registry.legacyKeys.push_back(legacyCleanupEntry);

    UninstallCleanupRule pathRule;
    pathRule.path = cleanupPath.string();
    pathRule.recursive = true;
    pathRule.onlyIfEmpty = false;
    uninstallCleanup.paths.push_back(pathRule);

    UninstallerCleanupConfigV3 uninstallerCleanupV3;
    uninstallerCleanupV3.systemUninstallEntry.displayName = "Sample Desktop App";
    uninstallerCleanupV3.systemUninstallEntry.scope = UninstallEntryScope::CURRENT_USER;
    uninstallerCleanupV3.registry.deleteKeys.push_back(legacyRegistryPath);
    uninstallerCleanupV3.paths.push_back(pathRule);
    SystemUninstallEntryCleanupItem legacyUninstallEntry;
    legacyUninstallEntry.displayName = legacyUninstallDisplayName;
    legacyUninstallEntry.scope = UninstallEntryScope::CURRENT_USER;
    uninstallerCleanupV3.systemUninstallEntry.legacyEntries.push_back(legacyUninstallEntry);

    fs::path manifestPath = installDir / "install.manifest.json";
    Require(writeManifest(manifestPath.string(),
                          "SampleDesktopApp",
                          "Sample Desktop App",
                          "1.2.3",
                          installDir.string(),
                          {installDir.string()},
                          uninstallCleanup,
                          {installedFile.string()},
                          {},
                          false,
                          false,
                          "",
                          installStateConfig,
                          "markUninstalled",
                          "",
                          "zh_CN",
                          {},
                          {},
                          false,
                          {},
                          {},
                          uninstallerCleanupV3),
            "Failed to write uninstall manifest fixture");

    InstallerPathResolver resolver;
    CliSupport console;
    bool progressCompleted = false;
    bool ok = uninstallFromManifest(manifestPath.string(),
                                    resolver,
                                    console,
                                    [&](const UninstallProgressInfo& info) {
                                        if (info.progress >= 1.0f) {
                                            progressCompleted = true;
                                        }
                                    });

    Require(ok, "uninstallFromManifest should succeed");
    Require(progressCompleted, "Uninstall should report completion");
    Require(!fs::exists(installedFile), "Uninstall should remove listed installed files");
    Require(!fs::exists(cleanupPath), "Uninstall should remove configured cleanup path");
    Require(!fs::exists(manifestPath), "Uninstall should remove manifest file");

    std::string registryValue;
    Require(!readRegistryStringValue(legacyRegistryPath, "LegacyValue", registryValue),
            "Uninstall should remove configured legacy registry path");
#ifdef _WIN32
    Require(!readRegistryStringValue(legacyUninstallPath, "DisplayName", registryValue),
            "Uninstall should remove configured legacy system uninstall entry");
#endif

    std::string installState;
    Require(readRegistryStringValue(installStatePath, "InstallState", installState),
            "Uninstall should persist final install state");
    Require(installState == "uninstalled",
            "Final install state should be 'uninstalled'");

    deleteRegistryPath(legacyRegistryPath);
    deleteRegistryPath(installStatePath);
}

void TestUninstallContextExplicitManifestHasPriority() {
    fs::path root = CreateTestRoot("uninstall_context_explicit");
    fs::path installDir = root / "InstallRoot";
    fs::create_directories(installDir);
    fs::path manifestPath = installDir / "install.manifest.json";
    nlohmann::json manifest;
    manifest["schemaVersion"] = 3;
    manifest["app"] = {{"id", "ExplicitApp"}, {"name", "Explicit Display"}, {"version", "1.0.0"}};
    manifest["installDir"] = installDir.string();
    manifest["installer"] = {
        {"installState", nlohmann::json::object({{"registries", nlohmann::json::array()}, {"files", nlohmann::json::array()}})},
        {"payload", nlohmann::json::object({{"files", nlohmann::json::array()}, {"roots", nlohmann::json::array({installDir.string()})}})}
    };
    manifest["uninstaller"] = {
        {"cleanup", nlohmann::json::object({{"installedFiles", "manifest"}, {"installState", "delete"}})}
    };
    WriteTextFile(manifestPath, manifest.dump());

    InstallerPathResolver resolver;
    UninstallContext context;
    Require(ResolveUninstallContext(nullptr, resolver, manifestPath.string(), context),
            "Explicit uninstall manifest should resolve context");
    Require(context.manifestReadable, "Explicit manifest should be readable");
    Require(context.manifestPath == manifestPath.string(),
            "Explicit manifest path should be used as highest priority");
    Require(context.appId == "ExplicitApp", "Context should read appId from explicit manifest");
    Require(context.appName == "Explicit Display", "Context should prefer displayName from explicit manifest");
}

void TestUninstallContextFallsBackFromV3InstallStateRegistry() {
    fs::path root = CreateTestRoot("uninstall_context_v3_fallback");
    fs::path installDir = root / "InstallRoot";
    fs::create_directories(installDir);
    WriteTextFile(installDir / "app.exe", "payload");

    const std::string registryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UninstallFallback";
    RegistryEntry entry;
    entry.path = registryPath;
    entry.key = "InstallDir";
    entry.value = installDir.string();
    entry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(entry, installDir.string(), RegistryValueType::STRING),
            "Failed to seed uninstall fallback registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Fallback App";
    metadata.appId = "FallbackApp";
    metadata.installState = BuildTestInstallStateConfig(registryPath);
    metadata.installState.detect.primary.registry = "main";
    metadata.installState.detect.primary.value = "installDir";
    metadata.uninstallerCleanup.missingManifestFallback = "safeDirectoryFallback";

    InstallerPathResolver resolver;
    UninstallContext context;
    bool resolved = ResolveUninstallContext(&metadata, resolver, "", context);

    deleteRegistryPath(registryPath);

    Require(resolved, "Missing manifest should resolve fallback uninstall context from v3 installState registry");
    Require(!context.manifestReadable, "Fallback context should not claim manifest is readable");
    Require(context.fallbackAllowed, "Fallback context should be allowed for a safe install root");
    Require(normalizePathForCompare(context.installDir) == normalizePathForCompare(installDir.string()),
            "Fallback installDir should come from v3 installState registry");
}

void TestUninstallContextFallsBackFromLegacyDetectRegistry() {
    fs::path root = CreateTestRoot("uninstall_context_legacy_detect");
    fs::path installDir = root / "LegacyInstallRoot";
    fs::create_directories(installDir);
    WriteTextFile(installDir / "app.exe", "payload");

    const std::string registryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UninstallLegacyDetect";
    RegistryEntry entry;
    entry.path = registryPath;
    entry.key = "InstallPath";
    entry.value = installDir.string();
    entry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(entry, entry.value, entry.type),
            "Failed to seed legacy uninstall detect registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Fallback App";
    metadata.appId = "FallbackApp";
    metadata.uninstallerCleanup.missingManifestFallback = "safeDirectoryFallback";
    InstalledInstanceDetectInstallStateConfig legacyDetect;
    legacyDetect.id = "legacy_uninstall";
    legacyDetect.path = registryPath;
    legacyDetect.installDirValue = "InstallPath";
    metadata.installState.detect.legacy.push_back(legacyDetect);

    InstallerPathResolver resolver;
    UninstallContext context;
    bool resolved = ResolveUninstallContext(&metadata, resolver, "", context);
    deleteRegistryPath(registryPath);

    Require(resolved, "Missing manifest should resolve fallback uninstall context from legacy detect registry");
    Require(context.detectSource == "legacy:legacy_uninstall",
            "Fallback context should record legacy detect source");
    Require(context.fallbackAllowed, "Legacy detect fallback context should be allowed for a safe install root");
    Require(normalizePathForCompare(context.installDir) == normalizePathForCompare(installDir.string()),
            "Fallback installDir should come from legacy detect registry");
}

void TestUninstallFallbackRemovesSafeInstallDirectoryContents() {
    fs::path root = CreateTestRoot("uninstall_fallback_execute");
    fs::path installDir = root / "InstallRoot";
    fs::create_directories(installDir / "bin");
    WriteTextFile(installDir / "bin" / "app.exe", "payload");
    WriteTextFile(installDir / "data.txt", "data");
    const std::string registryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UninstallFallbackExecute";
    RegistryEntry entry;
    entry.path = registryPath;
    entry.key = "InstallDir";
    entry.value = installDir.string();
    entry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(entry, installDir.string(), RegistryValueType::STRING),
            "Failed to seed fallback execute registry value");

    UninstallContext context;
    context.installDir = installDir.string();
    context.appId = "FallbackApp";
    context.appName = "Fallback App";
    context.hasEmbeddedUninstallerCleanup = true;
    context.embeddedUninstallerCleanup.registry.deleteKeys.push_back(registryPath);
    context.fallbackAllowed = true;
    context.manifestReadable = false;

    InstallerPathResolver resolver;
    CliSupport console;
    bool ok = ExecuteUninstallFromContext(context, nullptr, resolver, console);

    Require(ok, "Fallback uninstall should succeed for safe install directory");
    Require(!fs::exists(installDir / "bin" / "app.exe"), "Fallback uninstall should remove files");
    Require(!fs::exists(installDir / "data.txt"), "Fallback uninstall should remove root files");
    std::string registryValue;
    Require(!readRegistryStringValue(registryPath, "InstallDir", registryValue),
            "Fallback uninstall should remove detected installState registry path");
}

void TestUninstallFallbackRejectsDangerousRoot() {
    fs::path dangerousRoot = fs::temp_directory_path().root_path();
    UninstallContext context;
    context.installDir = dangerousRoot.string();
    context.fallbackAllowed = true;
    context.manifestReadable = false;

    InstallerPathResolver resolver;
    CliSupport console;
    bool ok = ExecuteUninstallFromContext(context, nullptr, resolver, console);

    Require(!ok, "Fallback uninstall should reject a volume root");
}

void TestUninstallV3ManifestMissingRequiredSnapshotFails() {
    fs::path root = CreateTestRoot("uninstall_v3_manifest_missing_required");
    fs::path manifestPath = root / "install.manifest.json";
    WriteTextFile(manifestPath,
                  R"({
                    "schemaVersion": 3,
                    "app": {"id":"BrokenApp","name":"Broken App","version":"1.0.0"},
                    "installer": {"installState": {"registries":[],"files":[]}},
                    "uninstaller": {"cleanup": {"installedFiles":"manifest","installState":"delete"}}
                  })");

    InstallerPathResolver resolver;
    CliSupport console;
    Require(!uninstallFromManifest(manifestPath.string(), resolver, console),
            "v3 manifest missing installer.payload.files should fail instead of falling back");
}

void TestUninstallFallbackPolicyFailRejectsDirectoryCleanup() {
    fs::path root = CreateTestRoot("uninstall_fallback_policy_fail");
    fs::path installDir = root / "InstallRoot";
    fs::create_directories(installDir);
    WriteTextFile(installDir / "stale.txt", "keep");

    const std::string registryPath =
        "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UninstallFallbackPolicyFail";
    RegistryEntry entry;
    entry.path = registryPath;
    entry.key = "InstallDir";
    entry.value = installDir.string();
    entry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(entry, entry.value, entry.type),
            "Failed to seed v3 fallback detection registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Fallback Policy App";
    metadata.appId = "FallbackPolicyApp";
    metadata.installState = BuildTestInstallStateConfig(registryPath);
    metadata.installState.detect.primary.registry = "main";
    metadata.installState.detect.primary.value = "installDir";
    metadata.uninstallerCleanup.missingManifestFallback = "fail";

    InstallerPathResolver resolver;
    UninstallContext context;
    bool resolved = ResolveUninstallContext(&metadata, resolver, "", context);
    deleteRegistryPath(registryPath);

    Require(!resolved, "fallback policy fail should reject missing-manifest uninstall");
    Require(!context.fallbackAllowed, "fallback policy fail should not allow directory cleanup");
    Require(fs::exists(installDir / "stale.txt"),
            "fallback policy fail should not delete install directory contents");
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    const std::vector<std::pair<std::string, void(*)()>> tests = {
        {"load_valid_schema", &TestLoadValidSchema},
        {"reject_old_schema", &TestRejectOldSchema},
        {"component_install_type_is_required", &TestComponentInstallTypeIsRequired},
        {"component_local_install_requires_command", &TestComponentLocalInstallRequiresCommand},
        {"component_download_install_validation_and_round_trip",
         &TestComponentDownloadInstallValidationAndRoundTrip},
        {"component_launcher_builds_expected_commands",
         &TestComponentLauncherBuildsExpectedCommands},
        {"reject_uninstaller_detect_field", &TestRejectUninstallerDetectField},
        {"reject_legacy_system_uninstall_entry_fields", &TestRejectLegacySystemUninstallEntryFields},
        {"configuration_loads_only_from_config_directory_and_resolves_icon_there",
         &TestConfigurationLoadsOnlyFromConfigDirectoryAndResolvesIconThere},
        {"require_admin_false_rejects_admin_only_configuration",
         &TestRequireAdminFalseRejectsAdminOnlyConfiguration},
        {"install_state_detect_validation_supports_legacy_sources",
         &TestInstallStateDetectValidationSupportsLegacySources},
#ifdef _WIN32
        {"update_installer_execution_level_writes_manifest",
         &TestUpdateInstallerExecutionLevelWritesManifest},
#endif
        {"packager_args_named_order_independent", &TestPackagerArgsNamedOrderIndependent},
        {"packager_args_reject_legacy_and_positional", &TestPackagerArgsRejectLegacyAndPositional},
        {"installer_args_parse_upgrade", &TestInstallerArgsParseUpgrade},
        {"metadata_round_trip", &TestMetadataRoundTrip},
        {"package_manifest_builder_and_codec_round_trip", &TestPackageManifestBuilderAndCodecRoundTrip},
        {"package_manifest_validator_rejects_invalid_payload_and_components",
         &TestPackageManifestValidatorRejectsInvalidPayloadAndComponents},
        {"path_resolver_expand_environment_variables", &TestPathResolverExpandEnvironmentVariables},
        {"install_state_store_apply_and_cleanup", &TestInstallStateStoreApplyAndCleanup},
        {"write_manifest_preserves_explicit_cleanup_schema", &TestWriteManifestPreservesExplicitCleanupSchema},
        {"install_manifest_persists_previous_install_options",
         &TestInstallManifestPersistsPreviousInstallOptions},
        {"build_install_execution_plan_uses_v3_install_state_discovery",
         &TestBuildInstallExecutionPlanUsesV3InstallStateDiscovery},
        {"build_install_execution_plan_upgrade_uses_v3_install_state_registry",
         &TestBuildInstallExecutionPlanUpgradeUsesV3InstallStateRegistry},
        {"build_install_execution_plan_upgrade_allows_missing_manifest",
         &TestBuildInstallExecutionPlanUpgradeAllowsMissingManifest},
        {"install_state_resolver_uses_legacy_detect_in_order",
         &TestInstallStateResolverUsesLegacyDetectInOrder},
        {"build_install_execution_plan_upgrade_uses_legacy_detect_registry",
         &TestBuildInstallExecutionPlanUpgradeUsesLegacyDetectRegistry},
        {"compare_semantic_version",
         &TestCompareSemanticVersion},
        {"append_path_leaf_if_missing",
         &TestAppendPathLeafIfMissing},
        {"installer_concurrency_policy_defaults",
         &TestInstallerConcurrencyPolicyDefaults},
        {"installer_task_manager_executes_and_waits",
         &TestInstallerTaskManagerExecutesAndWaits},
        {"thread_pool_manager_uses_installer_task_manager_compatibly",
         &TestThreadPoolManagerUsesInstallerTaskManagerCompatibly},
        {"cleanup_delete_executor_uses_unified_concurrency",
         &TestCleanupDeleteExecutorUsesUnifiedConcurrency},
        {"progress_path_formatter_keeps_short_path",
         &TestProgressPathFormatterKeepsShortPath},
        {"progress_path_formatter_shortens_absolute_path",
         &TestProgressPathFormatterShortensAbsolutePath},
        {"progress_path_formatter_shortens_relative_path",
         &TestProgressPathFormatterShortensRelativePath},
        {"progress_path_formatter_strips_long_path_prefix",
         &TestProgressPathFormatterStripsLongPathPrefix},
        {"post_setup_file_url_decodes_utf8_chinese_path",
         &TestPostSetupFileUrlDecodesUtf8ChinesePath},
        {"post_setup_file_url_supports_expanded_install_dir_drive_path",
         &TestPostSetupFileUrlSupportsExpandedInstallDirDrivePath},
        {"same_root_upgrade_cleanup_uses_previous_manifest_files",
         &TestSameRootUpgradeCleanupUsesPreviousManifestFiles},
#ifdef _WIN32
        {"same_root_upgrade_cleanup_isolates_payload_subdirectories",
         &TestSameRootUpgradeCleanupIsolatesPayloadSubdirectories},
        {"same_root_upgrade_cleanup_removes_manifest_files_outside_replacement_target",
         &TestSameRootUpgradeCleanupRemovesManifestFilesOutsideReplacementTarget},
#endif
        {"upgrade_cleanup_missing_manifest_removes_safe_directory_contents",
         &TestUpgradeCleanupMissingManifestRemovesSafeDirectoryContents},
        {"cleanup_upgrade_system_artifacts_executes_explicit_rules",
         &TestCleanupUpgradeSystemArtifactsExecutesExplicitRules},
#ifdef _WIN32
        {"delete_uninstall_entry_matches_display_name",
         &TestDeleteUninstallEntryMatchesDisplayName},
        {"delete_uninstall_entry_both_scope_attempts_all_views",
         &TestDeleteUninstallEntryBothScopeAttemptsAllViews},
#endif
        {"upgrade_extra_path_cleanup_with_watchdog_removes_path",
         &TestUpgradeExtraPathCleanupWithWatchdogRemovesPath},
#ifdef _WIN32
        {"upgrade_cleanup_runs_in_process_and_completes",
         &TestUpgradeCleanupRunsInProcessAndCompletes},
        {"upgrade_cleanup_skips_reparse_point_targets",
         &TestUpgradeCleanupSkipsReparsePointTargets},
#endif
        {"uninstall_context_explicit_manifest_has_priority",
         &TestUninstallContextExplicitManifestHasPriority},
        {"uninstall_context_falls_back_from_v3_install_state_registry",
         &TestUninstallContextFallsBackFromV3InstallStateRegistry},
        {"uninstall_context_falls_back_from_legacy_detect_registry",
         &TestUninstallContextFallsBackFromLegacyDetectRegistry},
        {"uninstall_fallback_removes_safe_install_directory_contents",
         &TestUninstallFallbackRemovesSafeInstallDirectoryContents},
        {"uninstall_fallback_rejects_dangerous_root",
         &TestUninstallFallbackRejectsDangerousRoot},
        {"uninstall_v3_manifest_missing_required_snapshot_fails",
         &TestUninstallV3ManifestMissingRequiredSnapshotFails},
        {"uninstall_fallback_policy_fail_rejects_directory_cleanup",
         &TestUninstallFallbackPolicyFailRejectsDirectoryCleanup},
        {"uninstall_from_manifest_executes_explicit_cleanup",
         &TestUninstallFromManifestExecutesExplicitCleanup},
    };

    int failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << "\n";
        } catch (const TestFailure& failure) {
            ++failed;
            std::cerr << "[FAIL] " << test.first << ": " << failure.what() << "\n";
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << "[FAIL] " << test.first << ": unexpected exception: " << ex.what() << "\n";
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.first << ": unknown exception\n";
        }
    }

    if (failed != 0) {
        std::cerr << failed << " test(s) failed\n";
        return 1;
    }
    return 0;
}
