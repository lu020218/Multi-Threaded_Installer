#include "common/archive_types.h"
#include "packager/configuration_manager.h"
#include "packager/configuration_loader.h"
#include "packager/metadata_generator.h"
#include "packager/package_manifest_builder.h"
#include "packager/version_info_updater.h"
#include "installer/metadata_parser.h"
#include "installer/console_interface.h"
#include "common/package_manifest_codec.h"
#include "common/utf8_utils.h"
#include "installer/package_manifest_validator.h"
#include "installer/install_plan_builder.h"
#include "installer/install_manifest_store.h"
#include "installer/installer_helpers.h"
#include "installer/installed_instance_resolver.h"
#include "installer/path_resolver.h"
#include "installer/registry_utils.h"
#include "installer/upgrade_cleanup.h"
#include "installer/uninstall_manager.h"
#include "common/version_utils.h"

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

std::string MinimalValidYaml() {
    return R"(schemaVersion: 2
app:
  name: Sample Desktop App
  id: SampleDesktopApp
  version: 1.2.3
  directoryName: SampleDesktopApp
  website: https://example.com
package:
  compression:
    algorithm: zstd
    level: 6
    threads: 2
install:
  defaultDir: "%ProgramFiles%\\SampleDesktopApp"
  requireAdmin: true
  autoCleanOldInstall: false
  autoStartup: false
  desktopIcon: true
  useMutex: true
  mutexName: "Global\\SampleDesktopApp_Install"
  minWindows:
    major: 10
    minor: 0
    build: 19045
  sparseFileThresholdBytes: 4194304
  killProcesses:
    - SampleDesktopApp.exe
  installInfo:
    mode: registry
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
ui:
  defaultLanguage: zh_CN
  desktopShortcut:
    defaultName: Sample Desktop App
layout:
  folders:
    - id: app
      source: bin
      target: "%InstallDir%"
    - id: user_templates
      source: templates
      target: "%AppData%\\SampleDesktopApp"
lifecycle:
  registry:
    onInstall:
      - path: HKEY_LOCAL_MACHINE\\Software\\SampleDesktopApp
        key: Publisher
        value: "OpenAI"
        type: string
  cleanup:
    onUpgrade:
      installRoots:
        - path: HKEY_LOCAL_MACHINE\\Software\\SampleDesktopApp
          key: InstallDir
      registry:
        legacyKeys:
          - path: HKEY_CURRENT_USER\\Software\\SampleDesktopAppLegacy
            key: ""
      uninstallEntries:
        entries:
          - name: SampleDesktopApp
            scope: any
      shortcuts:
        - name: Sample Desktop App
      startup:
        - name: SampleDesktopApp
      extraPaths:
        - path: "%LocalAppData%\\SampleDesktopAppLegacy"
          recursive: true
          onlyIfEmpty: false
    onUninstall:
      processes:
        - name: SampleDesktopApp.exe
      registry:
        legacyKeys:
          - path: HKEY_CURRENT_USER\\Software\\SampleDesktopApp
            key: ""
      uninstallEntries:
        entries:
          - name: SampleDesktopApp
            scope: any
      shortcuts:
        - name: Sample Desktop App
      startup:
        - name: SampleDesktopApp
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

void TestLoadValidSchema() {
    fs::path root = CreateTestRoot("valid_schema");
    fs::path inputDir = root / "payload";
    fs::path configDir = root / "config";
    fs::create_directories(inputDir / "bin");
    fs::create_directories(inputDir / "templates");
    fs::create_directories(configDir);
    WriteTextFile(configDir / "packager.yaml", MinimalValidYaml());

    ConfigurationManager manager;
    Require(manager.initialize(inputDir.string(), configDir.string()),
            manager.getLastError().empty() ? "valid schema should initialize" : manager.getLastError());

    const auto& config = manager.getConfiguration();
    Require(config.layout.folders.size() == 2, "Expected two layout folders");
    Require(config.layout.folders[0].target == "%InstallDir%", "Folder target should preserve %InstallDir%");
    Require(config.install.installInfo.path.find("HKEY_LOCAL_MACHINE") != std::string::npos &&
                config.install.installInfo.path.find("SampleDesktopApp") != std::string::npos,
            "installInfo path mismatch");
    Require(config.install.installInfo.values.at("installState").value == "%InstallState%",
            "installState template mismatch");
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
    yaml.insert(position + marker.size(), "  product:\n    icon: app.ico\n");
    WriteTextFile(configDir / "packager.yaml", yaml);

    ConfigurationManager manager;
    Require(manager.initialize(inputDir.string(), configDir.string()),
            manager.getLastError().empty() ? "config directory schema should initialize"
                                           : manager.getLastError());
    Require(manager.getConfigFilePath().find("config") != std::string::npos,
            "Configuration should be loaded from config directory");
    Require(manager.getConfiguration().app.product.iconPath == "app.ico",
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
        std::string yaml = MinimalValidYaml();
        ReplaceAll(yaml, "  requireAdmin: true", "  requireAdmin: false");
        ReplaceAll(yaml, "  defaultDir: \"%ProgramFiles%\\\\SampleDesktopApp\"",
                   "  defaultDir: \"%LocalAppData%\\\\SampleDesktopApp\"");
        WriteTextFile(configDir / "packager.yaml", yaml);

        ConfigurationManager manager;
        Require(!manager.initialize(inputDir.string(), configDir.string()),
                "requireAdmin=false should reject HKLM installInfo path");
    }
    {
        fs::path root = CreateTestRoot("require_admin_false_hklm_on_install");
        fs::path inputDir = root / "payload";
        fs::path configDir = root / "config";
        fs::create_directories(inputDir / "bin");
        fs::create_directories(inputDir / "templates");
        fs::create_directories(configDir);
        std::string yaml = MinimalValidYaml();
        ReplaceAll(yaml, "  requireAdmin: true", "  requireAdmin: false");
        ReplaceAll(yaml, "  defaultDir: \"%ProgramFiles%\\\\SampleDesktopApp\"",
                   "  defaultDir: \"%LocalAppData%\\\\SampleDesktopApp\"");
        ReplaceAll(yaml, "    path: HKEY_LOCAL_MACHINE\\\\Software\\\\SampleDesktopApp",
                   "    path: HKEY_CURRENT_USER\\\\Software\\\\SampleDesktopApp");
        WriteTextFile(configDir / "packager.yaml", yaml);

        ConfigurationManager manager;
        Require(!manager.initialize(inputDir.string(), configDir.string()),
                "requireAdmin=false should reject HKLM lifecycle registry path");
    }
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
    config.app.directoryName = "SampleDesktopApp";
    config.app.website = "https://example.com";
    config.ui.desktopShortcut.defaultName = "Sample Shortcut";
    config.ui.desktopShortcut.i18n["zh_CN"] = "示例应用";
    config.ui.desktopShortcut.i18n["en_US"] = "Sample Shortcut";
    UiLinkBinding link;
    link.control = "websiteLink";
    link.url = "https://example.com";
    config.ui.links.push_back(link);

    config.install.defaultDir = "%ProgramFiles%\\SampleDesktopApp";
    config.install.requireAdmin = true;
    config.install.useMutex = true;
    config.install.mutexName = "Global\\SampleDesktopApp_Install";
    config.install.installInfo.path = "HKEY_LOCAL_MACHINE\\Software\\SampleDesktopApp";

    InstallInfoValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    installDirValue.value = "%InstallDir%";
    installDirValue.type = RegistryValueType::EXPAND_STRING;
    config.install.installInfo.values["installDir"] = installDirValue;

    InstallInfoValueConfig displayNameValue;
    displayNameValue.key = "DisplayName";
    displayNameValue.value = "%AppName%";
    config.install.installInfo.values["displayName"] = displayNameValue;

    InstallInfoValueConfig versionValue;
    versionValue.key = "Version";
    versionValue.value = "%Version%";
    config.install.installInfo.values["displayVersion"] = versionValue;

    InstallInfoValueConfig executableValue;
    executableValue.key = "ExecutablePath";
    executableValue.value = "%InstallDir%\\SampleDesktopApp.exe";
    executableValue.type = RegistryValueType::EXPAND_STRING;
    config.install.installInfo.values["executablePath"] = executableValue;

    InstallInfoValueConfig stateValue;
    stateValue.key = "InstallState";
    stateValue.value = "%InstallState%";
    config.install.installInfo.values["installState"] = stateValue;

    LayoutFolderConfig appFolder;
    appFolder.id = "app";
    appFolder.source = "bin";
    appFolder.target = "%InstallDir%";
    config.layout.folders.push_back(appFolder);

    ComponentConfig component;
    component.id = "core";
    component.name = "Core";
    component.required = true;
    component.defaultSelected = true;
    component.folders.push_back("app");
    component.source.type = ComponentSourceType::EMBEDDED;
    config.layout.components.push_back(component);

    RegistryEntry installRegistry;
    installRegistry.path = "HKEY_LOCAL_MACHINE\\Software\\SampleDesktopApp";
    installRegistry.key = "Publisher";
    installRegistry.value = "OpenAI";
    installRegistry.type = RegistryValueType::STRING;
    config.lifecycle.registry.onInstall.push_back(installRegistry);

    RegistryLookupEntry installRoot;
    installRoot.path = "HKEY_LOCAL_MACHINE\\Software\\SampleDesktopApp";
    installRoot.key = "InstallDir";
    config.lifecycle.cleanup.onUpgrade.installRoots.push_back(installRoot);

    UninstallEntryCleanup uninstallEntry;
    uninstallEntry.name = "SampleDesktopApp";
    uninstallEntry.scope = UninstallEntryScope::ANY;
    config.lifecycle.cleanup.onUpgrade.uninstallEntries.push_back(uninstallEntry);
    config.lifecycle.cleanup.onUninstall.uninstallEntries.push_back(uninstallEntry);

    NamedCleanupEntry shortcut;
    shortcut.name = "Sample Desktop App";
    config.lifecycle.cleanup.onUpgrade.shortcuts.push_back(shortcut);
    config.lifecycle.cleanup.onUninstall.shortcuts.push_back(shortcut);

    NamedCleanupEntry startup;
    startup.name = "SampleDesktopApp";
    config.lifecycle.cleanup.onUpgrade.startup.push_back(startup);
    config.lifecycle.cleanup.onUninstall.startup.push_back(startup);

    NamedCleanupEntry process;
    process.name = "SampleDesktopApp.exe";
    config.lifecycle.cleanup.onUninstall.processes.push_back(process);

    RegistryEntry legacyRegistry;
    legacyRegistry.path = "HKEY_CURRENT_USER\\Software\\SampleDesktopAppLegacy";
    legacyRegistry.key = "";
    config.lifecycle.cleanup.onUpgrade.registry.legacyKeys.push_back(legacyRegistry);
    config.lifecycle.cleanup.onUninstall.registry.legacyKeys.push_back(legacyRegistry);

    UninstallCleanupRule upgradePath;
    upgradePath.path = "%LocalAppData%\\SampleDesktopAppLegacy";
    upgradePath.recursive = true;
    upgradePath.onlyIfEmpty = false;
    config.lifecycle.cleanup.onUpgrade.extraPaths.push_back(upgradePath);

    UninstallCleanupRule uninstallPath;
    uninstallPath.path = "%LocalAppData%\\SampleDesktopApp\\Cache";
    uninstallPath.recursive = true;
    uninstallPath.onlyIfEmpty = false;
    config.lifecycle.cleanup.onUninstall.paths.push_back(uninstallPath);

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

    MetadataGenerator generator;
    ExtendedInstallationMetadata generated =
        generator.generateExtendedMetadata({result}, {folder}, config);
    std::vector<uint8_t> bytes =
        SerializePackageManifest(PackageManifestFromExtendedMetadata(generated));

    MetadataParser parser;
    ExtendedInstallationMetadata parsed = parser.deserializeExtendedMetadata(bytes);

    Require(parser.validateMetadata(parsed), "Parsed metadata should validate");
    Require(parsed.installInfo.path == config.install.installInfo.path, "installInfo path lost in metadata");
    Require(parsed.installMutexName == config.install.mutexName, "mutex name lost in metadata");
    Require(parsed.extendedPayloadMappings.size() == 1, "Expected one extended payload mapping");
    Require(parsed.extendedPayloadMappings[0].target == "%InstallDir%", "Folder target lost in metadata");
    Require(parsed.desktopShortcutDefaultName == "Sample Shortcut",
            "Desktop shortcut default name lost in metadata");
    Require(parsed.desktopShortcutLocalizedNames.at("zh_CN") == "示例应用",
            "Desktop shortcut localized names lost in metadata");
    Require(parsed.uiLinkBindings.size() == 1, "UI links lost in metadata");
    Require(parsed.layoutComponents.size() == 1, "Components lost in metadata");
    Require(parsed.lifecycleUpgradeCleanup.installRoots.size() == 1, "Upgrade installRoots lost in metadata");
    Require(parsed.lifecycleUninstallCleanup.processes.size() == 1, "Uninstall processes lost in metadata");
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
    Require(manifest.payload.folders.size() == 1, "Manifest payload folder count mismatch");
    Require(manifest.payload.folders[0].target == "%InstallDir%", "Manifest folder target mismatch");
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
    Require(parsed.ui.desktopShortcutDefaultName == "Sample Shortcut",
            "Manifest UI default shortcut lost after codec round-trip");
    Require(parsed.components.components.size() == 1,
            "Manifest components lost after codec round-trip");

    MetadataParser parser;
    ExtendedInstallationMetadata projected = parser.deserializeExtendedMetadata(bytes);
    Require(parser.validateMetadata(projected), "Projected manifest should validate as metadata");
    Require(projected.extendedPayloadMappings[0].target == "%InstallDir%",
            "Projected metadata target mismatch");
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

    PackageManifest badDownload = manifest;
    badDownload.components.components[0].source.type = ComponentSourceType::DOWNLOAD;
    badDownload.components.components[0].source.download.url = "http://example.com/setup.exe";
    badDownload.components.components[0].source.download.sha256 = "not-a-sha";
    Require(!ValidatePackageManifest(badDownload, error),
            "Validator should reject invalid download component metadata");
}

void TestPathResolverExpandEnvironmentVariables() {
    InstallerPathResolver resolver;
    std::string expanded = resolver.expandEnvironmentVariables("%AppData%\\SampleDesktopApp");
    Require(!expanded.empty(), "Expanded AppData path should not be empty");
    Require(expanded.find('%') == std::string::npos, "Expanded path should not contain raw % tokens");
    Require(expanded.find("SampleDesktopApp") != std::string::npos,
            "Expanded path should keep suffix");
}

void TestWriteManifestPreservesExplicitCleanupSchema() {
    fs::path root = CreateTestRoot("manifest_schema");
    fs::path manifestPath = root / "install.manifest.json";

    InstallInfoConfig installInfo;
    installInfo.path = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\Manifest";
    InstallInfoValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    installDirValue.value = "%InstallDir%";
    installDirValue.type = RegistryValueType::EXPAND_STRING;
    installInfo.values["installDir"] = installDirValue;

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

    bool ok = writeManifest(manifestPath.string(),
                            "SampleDesktopApp",
                            "Sample Desktop App",
                            "1.2.3",
                            "C:\\Apps\\SampleDesktopApp",
                            {"C:\\Apps\\SampleDesktopApp"},
                            cleanup,
                            {"C:\\Apps\\SampleDesktopApp\\SampleDesktopApp.exe"},
                            {},
                            {"SampleDesktopApp.exe"},
                            true,
                            true,
                            "Sample Desktop App",
                            installInfo,
                            "C:\\Apps\\SampleDesktopApp\\uninstall.exe",
                            "zh_CN",
                            {});
    Require(ok, "writeManifest should succeed");

    nlohmann::json manifest;
    Require(readManifest(manifestPath.string(), manifest), "Manifest should round-trip as JSON");
    Require(manifest.contains("lifecycleUninstallCleanup"),
            "Manifest should contain lifecycleUninstallCleanup");
    Require(manifest["lifecycleUninstallCleanup"]["processes"].size() == 1,
            "Manifest should persist uninstall processes");
    Require(manifest["lifecycleUninstallCleanup"]["uninstallEntries"]["entries"].size() == 1,
            "Manifest should persist uninstall entries");
    Require(manifest["installInfo"]["path"].is_string(),
            "Manifest should contain installInfo.path");
    Require(manifest["installInfo"]["values"]["installDir"]["key"] == "InstallDir",
            "Manifest should persist installInfo values");
}

void TestInstallerArgsParseUpgrade() {
    CliSupport console;
    std::vector<std::string> args = {"installer.exe", "--upgrade", "--silent"};
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }

    CliSupport::InstallerArgs parsed = console.parseInstallerArgs(static_cast<int>(argv.size()), argv.data());
    Require(parsed.upgrade, "Installer args should parse --upgrade");
    Require(parsed.silent, "Installer args should still parse --silent with --upgrade");
}

void TestInstallManifestPersistsPreviousInstallOptions() {
    fs::path root = CreateTestRoot("previous_install_options");
    fs::path manifestPath = root / "install.manifest.json";

    InstallInfoConfig installInfo;
    installInfo.path = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\PreviousOptions";
    InstallInfoValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    installDirValue.value = "%InstallDir%";
    installInfo.values["installDir"] = installDirValue;

    Require(writeManifest(manifestPath.string(),
                          "SampleDesktopApp",
                          "Sample Desktop App",
                          "1.2.3",
                          root.string(),
                          {root.string()},
                          UninstallCleanupConfig{},
                          {},
                          {},
                          {},
                          true,
                          false,
                          "",
                          installInfo,
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

void TestBuildInstallExecutionPlanUsesConfiguredInstallRoots() {
    fs::path root = CreateTestRoot("install_plan_roots");
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
    metadata.installInfo.path = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\CurrentInstall";
    InstallInfoValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    installDirValue.value = "%InstallDir%";
    metadata.installInfo.values["installDir"] = installDirValue;
    RegistryLookupEntry lookup;
    lookup.path = registryPath;
    lookup.key = "InstallDir";
    metadata.lifecycleUpgradeCleanup.installRoots.push_back(lookup);

    InstallServiceOptions options;
    options.installPath = "C:\\NewInstallPath";
    options.installPathExplicit = false;
    InstallerPathResolver resolver;
    InstallExecutionPlan plan;
    std::string error;
    bool built = BuildInstallExecutionPlan(metadata, resolver, options, plan, error);

    deleteRegistryPath(registryPath);

    Require(built, error.empty() ? "BuildInstallExecutionPlan should succeed" : error);
    Require(plan.hasPreviousInstall, "Plan should detect previous install from installRoots");
    Require(normalizePathForCompare(plan.previousInstallDir) ==
                normalizePathForCompare(previousInstallDir.string()),
            "Previous install dir should come from configured installRoots");
    Require(plan.pathDecision.mode == InstallTargetMode::OverwriteInstall,
            "Plan should enter overwrite mode when previous install is found");
    Require(normalizePathForCompare(plan.pathDecision.resolvedInstallRoot) ==
                normalizePathForCompare("C:\\NewInstallPath"),
            "Overwrite install should preserve the requested install root");
    Require(normalizePathForCompare(plan.pathDecision.cleanupTargetInstallRoot) ==
                normalizePathForCompare(previousInstallDir.string()),
            "Overwrite cleanup target should remain the previous install root");
}

void TestBuildInstallExecutionPlanUpgradeUsesInstallInfoRegistry() {
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
            "Failed to seed upgrade installInfo registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Sample Desktop App";
    metadata.appId = "SampleDesktopApp";
    metadata.appDirectoryName = "SampleDesktopApp";
    metadata.installDefaultDir = "%ProgramFiles%\\SampleDesktopApp";
    metadata.installInfo.path = registryPath;
    InstallInfoValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    installDirValue.value = "%InstallDir%";
    metadata.installInfo.values["installDir"] = installDirValue;

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
            "Upgrade install root should come from installInfo registry");
    Require(normalizePathForCompare(plan.pathDecision.cleanupTargetInstallRoot) ==
                normalizePathForCompare(previousInstallDir.string()),
            "Upgrade cleanup target should be previous install root");
}

void TestBuildInstallExecutionPlanUpgradeFailsWithoutManifest() {
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
            "Failed to seed upgrade installInfo registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Sample Desktop App";
    metadata.appId = "SampleDesktopApp";
    metadata.installInfo.path = registryPath;
    InstallInfoValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    metadata.installInfo.values["installDir"] = installDirValue;

    InstallServiceOptions options;
    options.upgradeMode = true;
    InstallerPathResolver resolver;
    InstallExecutionPlan plan;
    std::string error;
    bool built = BuildInstallExecutionPlan(metadata, resolver, options, plan, error);

    deleteRegistryPath(registryPath);

    Require(!built, "Upgrade plan should fail when previous manifest is missing");
    Require(!error.empty(), "Upgrade plan failure should explain the missing previous install state");
}

void TestInstallRootsAreOnlyDiscoverySource() {
    fs::path root = CreateTestRoot("install_roots_only_source");
    fs::path currentInstallDir = root / "CurrentInstall";
    fs::create_directories(currentInstallDir);

    const std::string installInfoPath =
        "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\InstallRootsOnlySource";
    RegistryEntry seededRegistry;
    seededRegistry.path = installInfoPath;
    seededRegistry.key = "InstallDir";
    seededRegistry.value = currentInstallDir.string();
    seededRegistry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(seededRegistry, seededRegistry.value, seededRegistry.type),
            "Failed to seed installInfo registry value");

    ExtendedInstallationMetadata metadata;
    metadata.appName = "Sample Desktop App";
    metadata.appId = "SampleDesktopApp";
    metadata.installDefaultDir = "%ProgramFiles%\\SampleDesktopApp";
    metadata.installInfo.path = installInfoPath;
    InstallInfoValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    installDirValue.value = "%InstallDir%";
    metadata.installInfo.values["installDir"] = installDirValue;

    InstalledInstanceInfo installedInstance;
    const bool found = resolveInstalledInstanceFromInstallRoots(metadata, installedInstance);
    deleteRegistryPath(installInfoPath);

    Require(!found, "Installed instance discovery should ignore installInfo registry path");
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
    metadata.lifecycleUpgradeCleanup.registry.legacyKeys.push_back(legacyRegistry);

    UninstallCleanupRule cleanupRule;
    cleanupRule.path = legacyPath.string();
    cleanupRule.recursive = true;
    cleanupRule.onlyIfEmpty = false;
    metadata.lifecycleUpgradeCleanup.extraPaths.push_back(cleanupRule);

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
    deleteRegistryPath(registryPath);
}

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
void TestUpgradeCleanupWorkerTimeoutIsPartialSuccess() {
    fs::path root = CreateTestRoot("upgrade_cleanup_worker_timeout");
    fs::path previousInstallDir = root / "InstallRoot";
    fs::create_directories(previousInstallDir);
    fs::path staleFile = previousInstallDir / "stale.txt";
    WriteTextFile(staleFile, "stale");
    WriteTextFile(previousInstallDir / "install.manifest.json",
                  R"({"files":["stale.txt"],"appVersion":"1.0.0"})");

    SetEnvironmentVariableW(L"MTINSTALLER_TEST_CLEANUP_WORKER_DELAY_MS", L"2000");
    UpgradeCleanupPolicy policy;
    policy.totalTimeoutMs = 300;
    policy.itemStaleTimeoutMs = 100;
    UpgradeCleanupResult result = runPreviousInstallCleanupWithWatchdog(
        (previousInstallDir / "install.manifest.json").string(),
        previousInstallDir.string(),
        previousInstallDir.string(),
        {},
        {},
        policy);
    SetEnvironmentVariableW(L"MTINSTALLER_TEST_CLEANUP_WORKER_DELAY_MS", nullptr);

    Require(result.success, "Timed-out cleanup should be partial success by default");
    Require(result.partial, "Timed-out cleanup should be marked partial");
    Require(result.timedOut, "Timed-out cleanup should report timeout");
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

    const std::string installInfoPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UninstallInstallInfo";
    const std::string legacyRegistryPath = "HKEY_CURRENT_USER\\Software\\SchemaRegressionTests\\UninstallLegacy";

    InstallInfoConfig installInfo;
    installInfo.path = installInfoPath;

    InstallInfoValueConfig installDirValue;
    installDirValue.key = "InstallDir";
    installDirValue.value = "%InstallDir%";
    installDirValue.type = RegistryValueType::EXPAND_STRING;
    installInfo.values["installDir"] = installDirValue;

    InstallInfoValueConfig displayNameValue;
    displayNameValue.key = "DisplayName";
    displayNameValue.value = "%AppName%";
    displayNameValue.type = RegistryValueType::STRING;
    installInfo.values["displayName"] = displayNameValue;

    InstallInfoValueConfig versionValue;
    versionValue.key = "Version";
    versionValue.value = "%Version%";
    versionValue.type = RegistryValueType::STRING;
    installInfo.values["displayVersion"] = versionValue;

    InstallInfoValueConfig executableValue;
    executableValue.key = "ExecutablePath";
    executableValue.value = "%InstallDir%\\SampleDesktopApp.exe";
    executableValue.type = RegistryValueType::EXPAND_STRING;
    installInfo.values["executablePath"] = executableValue;

    InstallInfoValueConfig stateValue;
    stateValue.key = "InstallState";
    stateValue.value = "%InstallState%";
    stateValue.type = RegistryValueType::STRING;
    installInfo.values["installState"] = stateValue;

    RegistryEntry legacyRegistry;
    legacyRegistry.path = legacyRegistryPath;
    legacyRegistry.key = "LegacyValue";
    legacyRegistry.value = "legacy";
    legacyRegistry.type = RegistryValueType::STRING;
    Require(writeRegistryValue(legacyRegistry, legacyRegistry.value, legacyRegistry.type),
            "Failed to seed uninstall legacy registry value");

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
                          {},
                          false,
                          false,
                          "",
                          installInfo,
                          "",
                          "zh_CN",
                          {}),
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

    std::string installState;
    Require(readRegistryStringValue(installInfoPath, "InstallState", installState),
            "Uninstall should persist final install state");
    Require(installState == "uninstalled",
            "Final install state should be 'uninstalled'");

    deleteRegistryPath(legacyRegistryPath);
    deleteRegistryPath(installInfoPath);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "--upgrade-cleanup-worker") {
        return runUpgradeCleanupWorkerFromTask(argv[2]);
    }

    const std::vector<std::pair<std::string, void(*)()>> tests = {
        {"load_valid_schema", &TestLoadValidSchema},
        {"reject_old_schema", &TestRejectOldSchema},
        {"configuration_loads_only_from_config_directory_and_resolves_icon_there",
         &TestConfigurationLoadsOnlyFromConfigDirectoryAndResolvesIconThere},
        {"require_admin_false_rejects_admin_only_configuration",
         &TestRequireAdminFalseRejectsAdminOnlyConfiguration},
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
        {"write_manifest_preserves_explicit_cleanup_schema", &TestWriteManifestPreservesExplicitCleanupSchema},
        {"install_manifest_persists_previous_install_options",
         &TestInstallManifestPersistsPreviousInstallOptions},
        {"build_install_execution_plan_uses_configured_install_roots",
         &TestBuildInstallExecutionPlanUsesConfiguredInstallRoots},
        {"build_install_execution_plan_upgrade_uses_install_info_registry",
         &TestBuildInstallExecutionPlanUpgradeUsesInstallInfoRegistry},
        {"build_install_execution_plan_upgrade_fails_without_manifest",
         &TestBuildInstallExecutionPlanUpgradeFailsWithoutManifest},
        {"install_roots_are_only_discovery_source",
         &TestInstallRootsAreOnlyDiscoverySource},
        {"compare_semantic_version",
         &TestCompareSemanticVersion},
        {"append_path_leaf_if_missing",
         &TestAppendPathLeafIfMissing},
        {"same_root_upgrade_cleanup_uses_previous_manifest_files",
         &TestSameRootUpgradeCleanupUsesPreviousManifestFiles},
        {"upgrade_cleanup_missing_manifest_removes_safe_directory_contents",
         &TestUpgradeCleanupMissingManifestRemovesSafeDirectoryContents},
        {"cleanup_upgrade_system_artifacts_executes_explicit_rules",
         &TestCleanupUpgradeSystemArtifactsExecutesExplicitRules},
        {"upgrade_extra_path_cleanup_with_watchdog_removes_path",
         &TestUpgradeExtraPathCleanupWithWatchdogRemovesPath},
#ifdef _WIN32
        {"upgrade_cleanup_worker_timeout_is_partial_success",
         &TestUpgradeCleanupWorkerTimeoutIsPartialSuccess},
        {"upgrade_cleanup_skips_reparse_point_targets",
         &TestUpgradeCleanupSkipsReparsePointTargets},
#endif
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
