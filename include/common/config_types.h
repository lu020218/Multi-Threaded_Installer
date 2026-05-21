#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MultiThreadedInstaller {

enum class CompressionAlgorithm {
    LZMA2_XZ,
    ZSTD
};

enum class InstallStateMode {
    REGISTRY
};

enum class RegistryValueType {
    STRING,
    DWORD,
    EXPAND_STRING
};

enum class ComponentSourceType : uint8_t {
    EMBEDDED = 0,
    LOCAL = 1,
    DOWNLOAD = 2
};

struct FolderInfo {
    std::string id;
    std::string sourcePath;
    std::string targetPath;
    std::vector<std::string> files;
    size_t totalSize;

    FolderInfo() : totalSize(0) {}

    FolderInfo(const std::string& source, const std::string& target)
        : sourcePath(source), targetPath(target), totalSize(0) {}
};

struct RegistryEntry {
    std::string path;
    std::string key;
    std::string value;
    RegistryValueType type;

    RegistryEntry()
        : type(RegistryValueType::STRING) {}
};

struct RegistryLookupEntry {
    std::string path;
    std::string key;
};

enum class UninstallEntryScope : uint8_t {
    CURRENT_USER = 0,
    LOCAL_MACHINE = 1,
    WOW6432 = 2,
    ANY = 3,
    BOTH = 4
};

struct UninstallEntryCleanup {
    std::string name;
    UninstallEntryScope scope;

    UninstallEntryCleanup()
        : scope(UninstallEntryScope::ANY) {}
};

struct SystemUninstallEntryCleanupItem {
    std::string displayName;
    UninstallEntryScope scope;

    SystemUninstallEntryCleanupItem()
        : scope(UninstallEntryScope::ANY) {}
};

struct NamedCleanupEntry {
    std::string name;
};

struct InstallInfoValueConfig {
    std::string key;
    std::string value;
    RegistryValueType type;

    InstallInfoValueConfig()
        : type(RegistryValueType::STRING) {}
};

struct InstallInfoConfig {
    InstallStateMode mode;
    std::string path;
    std::unordered_map<std::string, InstallInfoValueConfig> values;

    InstallInfoConfig()
        : mode(InstallStateMode::REGISTRY) {}
};

struct LocalInstallerConfig {
    std::string base;
    std::string installer;
    std::string args;
    bool wait;
    uint32_t timeoutSec;
    std::string uninstall;
    bool showWindow;
    bool showWindowConfigured;

    LocalInstallerConfig()
        : wait(true), timeoutSec(900), showWindow(false), showWindowConfigured(false) {}
};

struct DownloadInstallerConfig {
    std::string url;
    std::string sha256;
    std::string saveAs;
    std::string args;
    bool wait;
    uint32_t timeoutSec;
    bool showWindow;
    bool showWindowConfigured;

    DownloadInstallerConfig()
        : wait(true), timeoutSec(900), showWindow(false), showWindowConfigured(false) {}
};

struct ComponentSourceConfig {
    ComponentSourceType type;
    LocalInstallerConfig local;
    DownloadInstallerConfig download;

    ComponentSourceConfig()
        : type(ComponentSourceType::EMBEDDED) {}
};

struct ComponentConfig {
    std::string id;
    std::string name;
    std::string description;
    bool required;
    bool defaultSelected;
    uint32_t sizeHintMB;
    std::vector<std::string> dependsOn;
    std::vector<std::string> folders;
    ComponentSourceConfig source;

    ComponentConfig()
        : required(false),
          defaultSelected(true),
          sizeHintMB(0) {}
};

struct UiComponentBindingPage {
    std::string skin;
    std::vector<std::string> controls;
};

struct UiLinkBinding {
    std::string control;
    std::string url;
};

struct UninstallCleanupRule {
    std::string path;
    bool recursive;
    bool onlyIfEmpty;

    UninstallCleanupRule() : recursive(true), onlyIfEmpty(false) {}
};

struct CleanupRegistryConfig {
    std::vector<RegistryEntry> legacyKeys;
};

struct UpgradeCleanupConfig {
    std::vector<RegistryLookupEntry> installRoots;
    CleanupRegistryConfig registry;
    std::vector<UninstallEntryCleanup> uninstallEntries;
    std::vector<NamedCleanupEntry> shortcuts;
    std::vector<NamedCleanupEntry> startup;
    std::vector<UninstallCleanupRule> extraPaths;
};

struct UninstallCleanupConfig {
    std::vector<NamedCleanupEntry> processes;
    CleanupRegistryConfig registry;
    std::vector<UninstallEntryCleanup> uninstallEntries;
    std::vector<NamedCleanupEntry> shortcuts;
    std::vector<NamedCleanupEntry> startup;
    std::vector<UninstallCleanupRule> paths;
};

struct UiComponentSelectionConfig {
    std::string mode;
    std::string strategy;
    std::string tokenPrefix;
    std::vector<UiComponentBindingPage> pages;

    UiComponentSelectionConfig()
        : mode("dedicatedPage"),
          strategy("xml_userdata"),
          tokenPrefix("component:") {}
};

struct AppProductInfo {
    std::string iconPath;
    std::string productName;
    std::string fileVersion;
    std::string productVersion;
    std::string companyName;
    std::string fileDescription;
    std::string copyright;
};

struct AppConfig {
    std::string name;
    std::string id;
    std::string version;
    std::string directoryName;
    std::string website;
    std::string publisher;
    std::string icon;
    AppProductInfo versionInfo;
    AppProductInfo product;

    AppConfig()
        : name("MyApplication"),
          version("1.0") {}
};

struct PackageCompressionConfig {
    CompressionAlgorithm algorithm;
    int level;
    int threads;

    PackageCompressionConfig()
        : algorithm(CompressionAlgorithm::LZMA2_XZ),
          level(-1),
          threads(0) {}
};

struct PackageConfig {
    PackageCompressionConfig compression;
};

struct MinWindowsConfig {
    uint16_t major;
    uint16_t minor;
    uint32_t build;

    MinWindowsConfig() : major(0), minor(0), build(0) {}
};

struct PackagerInstallConfig {
    std::string defaultDir;
    bool requireAdmin;
    bool autoCleanOldInstall;
    bool autoStartup;
    bool desktopIcon;
    MinWindowsConfig minWindows;
    uint64_t sparseFileThresholdBytes;
    std::vector<std::string> killProcesses;
    bool useMutex;
    std::string mutexName;
    InstallInfoConfig installInfo;

    PackagerInstallConfig()
        : defaultDir("%ProgramFiles%"),
          requireAdmin(false),
          autoCleanOldInstall(false),
          autoStartup(false),
          desktopIcon(false),
          sparseFileThresholdBytes(4 * 1024 * 1024),
          useMutex(true) {}
};

struct UiDesktopShortcutConfig {
    std::string defaultName;
    std::unordered_map<std::string, std::string> i18n;
};

struct UiConfig {
    std::string defaultLanguage;
    UiDesktopShortcutConfig desktopShortcut;
    std::vector<UiLinkBinding> links;
    UiComponentSelectionConfig componentSelection;
};

struct LayoutFolderConfig {
    std::string id;
    std::string source;
    std::string target;
};

struct LayoutConfig {
    std::vector<LayoutFolderConfig> folders;
    std::vector<ComponentConfig> components;
};

struct InstallStateValueConfig {
    std::string key;
    std::string name;
    std::string value;
    RegistryValueType type;

    InstallStateValueConfig()
        : type(RegistryValueType::STRING) {}
};

struct InstallStateRegistryStoreConfig {
    std::string id;
    std::string path;
    std::unordered_map<std::string, InstallStateValueConfig> values;
};

struct InstallStateFileStoreConfig {
    std::string id;
    std::string path;
    std::string format;
    std::unordered_map<std::string, InstallStateValueConfig> values;

    InstallStateFileStoreConfig()
        : format("json") {}
};

struct InstalledInstanceDetectPrimaryConfig {
    std::string registry;
    std::string value;
};

struct InstalledInstanceDetectInstallStateConfig {
    std::string id;
    std::string path;
    std::string installDirValue;
};

struct InstalledInstanceDetectConfig {
    InstalledInstanceDetectPrimaryConfig primary;
    std::vector<InstalledInstanceDetectInstallStateConfig> legacy;
};

struct InstallStateConfig {
    std::vector<InstallStateRegistryStoreConfig> registries;
    std::vector<InstallStateFileStoreConfig> files;
    InstalledInstanceDetectConfig detect;
};

struct InstallerDefaultsConfig {
    bool autoStartup;
    bool desktopShortcut;

    InstallerDefaultsConfig()
        : autoStartup(false),
          desktopShortcut(false) {}
};

struct SystemUninstallEntryConfig {
    UninstallEntryScope scope;
    std::string displayName;
    std::string publisher;

    SystemUninstallEntryConfig()
        : scope(UninstallEntryScope::ANY) {}
};

struct SystemUninstallEntryCleanupConfig {
    UninstallEntryScope scope;
    std::string displayName;
    std::vector<SystemUninstallEntryCleanupItem> legacyEntries;

    SystemUninstallEntryCleanupConfig()
        : scope(UninstallEntryScope::ANY) {}
};

struct CleanupLegacyNamesConfig {
    std::vector<std::string> desktopShortcutNames;
    std::vector<std::string> startupNames;
};

struct RegistryCleanupConfig {
    std::vector<std::string> deleteKeys;
    std::vector<RegistryEntry> deleteValues;
};

struct InstallerCleanupConfig {
    SystemUninstallEntryCleanupConfig systemUninstallEntry;
    CleanupLegacyNamesConfig legacy;
    RegistryCleanupConfig registry;
    std::vector<UninstallCleanupRule> paths;
};

struct PayloadConfig {
    std::string id;
    std::string source;
    std::string target;
    bool required;

    PayloadConfig()
        : required(false) {}
};

struct InstallerRegistryWriteGroup {
    std::string path;
    std::unordered_map<std::string, InstallStateValueConfig> values;
};

struct InstallerRegistryConfig {
    std::vector<InstallerRegistryWriteGroup> write;
};

struct InstallerConfig {
    bool requireAdmin;
    std::string defaultDir;
    std::string directoryName;
    MinWindowsConfig minWindows;
    uint64_t largeFileThresholdBytes;
    std::string mutex;
    std::vector<std::string> killBeforeInstall;
    InstallerDefaultsConfig defaults;
    InstallStateConfig installState;
    SystemUninstallEntryConfig systemUninstallEntry;
    InstallerCleanupConfig cleanup;
    UiConfig ui;
    std::vector<PayloadConfig> payload;
    std::vector<ComponentConfig> components;
    InstallerRegistryConfig registry;

    InstallerConfig()
        : requireAdmin(false),
          defaultDir("%ProgramFiles%"),
          largeFileThresholdBytes(4 * 1024 * 1024) {}
};

struct UninstallerLegacyCleanupConfig {
    std::vector<std::string> desktopShortcutNames;
    std::vector<std::string> startupNames;
};

struct UninstallerRegistryCleanupConfig {
    std::vector<std::string> deleteKeys;
    std::vector<RegistryEntry> deleteValues;
};

struct UninstallerCleanupConfigV3 {
    std::string installedFiles;
    std::string missingManifestFallback;
    std::string installState;
    std::string autoStartup;
    std::string desktopShortcut;
    SystemUninstallEntryCleanupConfig systemUninstallEntry;
    CleanupLegacyNamesConfig legacy;
    RegistryCleanupConfig registry;
    std::vector<UninstallCleanupRule> paths;

    UninstallerCleanupConfigV3()
        : installedFiles("manifest"),
          missingManifestFallback("safeDirectoryFallback"),
          installState("delete"),
          autoStartup("auto"),
          desktopShortcut("auto") {}
};

struct UninstallerUiConfig {
    std::string defaultLanguage;
    std::string title;
    std::string confirmMessage;
};

struct UninstallerConfig {
    bool requireAdmin;
    std::vector<std::string> killBeforeUninstall;
    UninstallerCleanupConfigV3 cleanup;
    UninstallerUiConfig ui;

    UninstallerConfig()
        : requireAdmin(false) {}
};

struct LifecycleRegistryConfig {
    std::vector<RegistryEntry> onInstall;
};

struct LifecycleCleanupConfig {
    UpgradeCleanupConfig onUpgrade;
    UninstallCleanupConfig onUninstall;
};

struct LifecycleConfig {
    LifecycleRegistryConfig registry;
    LifecycleCleanupConfig cleanup;
};

struct PackagerConfiguration {
    uint32_t schemaVersion;
    AppConfig app;
    PackageConfig package;
    InstallerConfig installer;
    UninstallerConfig uninstaller;
    PackagerInstallConfig install;
    UiConfig ui;
    LayoutConfig layout;
    LifecycleConfig lifecycle;

    PackagerConfiguration()
        : schemaVersion(3) {}
};

} // namespace MultiThreadedInstaller
