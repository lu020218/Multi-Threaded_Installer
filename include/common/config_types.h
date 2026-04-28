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
    ANY = 3
};

struct UninstallEntryCleanup {
    std::string name;
    UninstallEntryScope scope;

    UninstallEntryCleanup()
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

    LocalInstallerConfig()
        : wait(true), timeoutSec(900) {}
};

struct DownloadInstallerConfig {
    std::string url;
    std::string sha256;
    std::string saveAs;
    std::string args;
    bool wait;
    uint32_t timeoutSec;
    std::string uninstall;

    DownloadInstallerConfig()
        : wait(true), timeoutSec(1800) {}
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
    std::vector<RegistryEntry> registry;
    std::vector<std::string> killProcesses;
    bool createDesktopShortcut;
    bool autoStartup;

    ComponentConfig()
        : required(false),
          defaultSelected(true),
          sizeHintMB(0),
          createDesktopShortcut(false),
          autoStartup(false) {}
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
    PackagerInstallConfig install;
    UiConfig ui;
    LayoutConfig layout;
    LifecycleConfig lifecycle;

    PackagerConfiguration()
        : schemaVersion(2) {}
};

} // namespace MultiThreadedInstaller
