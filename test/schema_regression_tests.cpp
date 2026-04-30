#include "common/archive_types.h"
#include "packager/configuration_manager.h"
#include "packager/configuration_loader.h"
#include "packager/metadata_generator.h"
#include "installer/metadata_parser.h"
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
    fs::create_directories(root / "bin");
    fs::create_directories(root / "templates");
    WriteTextFile(root / "packager.yaml", MinimalValidYaml());

    ConfigurationManager manager;
    Require(manager.initialize(root.string()),
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
    fs::create_directories(root / "bin");
    WriteTextFile(root / "packager.yaml", OldSchemaYaml());

    ConfigurationManager manager;
    Require(!manager.initialize(root.string()), "Old schema should be rejected");
    Require(!manager.getLastError().empty(), "Old schema rejection should provide an error message");
}

PackagerConfiguration BuildConfigFixture() {
    PackagerConfiguration config;
    config.app.name = "Sample Desktop App";
    config.app.id = "SampleDesktopApp";
    config.app.version = "1.2.3";
    config.app.directoryName = "SampleDesktopApp";
    config.app.website = "https://example.com";

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
    std::vector<uint8_t> bytes = generator.serializeExtendedMetadata(generated);

    MetadataParser parser;
    ExtendedInstallationMetadata parsed = parser.deserializeExtendedMetadata(bytes);

    Require(parser.validateMetadata(parsed), "Parsed metadata should validate");
    Require(parsed.installInfo.path == config.install.installInfo.path, "installInfo path lost in metadata");
    Require(parsed.installMutexName == config.install.mutexName, "mutex name lost in metadata");
    Require(parsed.extendedPayloadMappings.size() == 1, "Expected one extended payload mapping");
    Require(parsed.extendedPayloadMappings[0].target == "%InstallDir%", "Folder target lost in metadata");
    Require(parsed.lifecycleUpgradeCleanup.installRoots.size() == 1, "Upgrade installRoots lost in metadata");
    Require(parsed.lifecycleUninstallCleanup.processes.size() == 1, "Uninstall processes lost in metadata");
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

int main() {
    const std::vector<std::pair<std::string, void(*)()>> tests = {
        {"load_valid_schema", &TestLoadValidSchema},
        {"reject_old_schema", &TestRejectOldSchema},
        {"metadata_round_trip", &TestMetadataRoundTrip},
        {"path_resolver_expand_environment_variables", &TestPathResolverExpandEnvironmentVariables},
        {"write_manifest_preserves_explicit_cleanup_schema", &TestWriteManifestPreservesExplicitCleanupSchema},
        {"build_install_execution_plan_uses_configured_install_roots",
         &TestBuildInstallExecutionPlanUsesConfiguredInstallRoots},
        {"install_roots_are_only_discovery_source",
         &TestInstallRootsAreOnlyDiscoverySource},
        {"compare_semantic_version",
         &TestCompareSemanticVersion},
        {"append_path_leaf_if_missing",
         &TestAppendPathLeafIfMissing},
        {"cleanup_upgrade_system_artifacts_executes_explicit_rules",
         &TestCleanupUpgradeSystemArtifactsExecutesExplicitRules},
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
