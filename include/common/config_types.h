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

enum class SpecialDirectoryType {
    INSTALL_DIRECTORY,
    CUSTOM,
    PROGRAM_FILES,
    PROGRAM_FILES_X86,
    APPDATA_ROAMING,
    APPDATA_LOCAL,
    PROGRAM_DATA,
    USER_PROFILE
};

enum class InstallStateMode {
    REGISTRY,
    FILE,
    BOTH
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

struct InstallStateConfig {
    InstallStateMode mode;
    std::string registryPath;
    std::string registryKey;
    std::string filePath;
    bool useMutex;
    std::string mutexName;

    InstallStateConfig()
        : mode(InstallStateMode::REGISTRY),
          registryKey("InstallState"),
          useMutex(true) {}
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

struct UpgradeCleanupRegistryConfig {
    bool deleteFromManifest;
    std::vector<RegistryEntry> legacyKeys;

    UpgradeCleanupRegistryConfig() : deleteFromManifest(true) {}
};

struct UpgradeCleanupConfig {
    UpgradeCleanupRegistryConfig registry;
    std::vector<UninstallCleanupRule> extraPaths;
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
    InstallStateConfig installState;

    PackagerInstallConfig()
        : defaultDir("%ProgramFiles%"),
          requireAdmin(false),
          autoCleanOldInstall(false),
          autoStartup(false),
          desktopIcon(false),
          sparseFileThresholdBytes(4 * 1024 * 1024) {}
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

struct LayoutFolderDestination {
    std::string type;
    std::string path;
    bool appendDirectoryName;

    LayoutFolderDestination()
        : type("install"),
          appendDirectoryName(true) {}
};

struct LayoutFolderConfig {
    std::string id;
    std::string source;
    LayoutFolderDestination destination;
};

struct LayoutConfig {
    std::vector<LayoutFolderConfig> folders;
    std::vector<ComponentConfig> components;
};

struct LifecycleCompatibilityConfig {
    std::vector<std::string> legacyAppIds;
    std::vector<std::string> legacyDesktopShortcutNames;
};

struct LifecycleRegistryConfig {
    std::vector<RegistryEntry> onInstall;
};

struct LifecycleCleanupConfig {
    UpgradeCleanupConfig onUpgrade;
    std::vector<UninstallCleanupRule> onUninstallPaths;
};

struct PostSetupAgentConfig {
    bool enabled;
    std::vector<std::string> tasks;

    PostSetupAgentConfig() : enabled(false) {}
};

struct PostSetupConfig {
    PostSetupAgentConfig agent;
};

struct LifecycleConfig {
    LifecycleCompatibilityConfig compatibility;
    LifecycleRegistryConfig registry;
    LifecycleCleanupConfig cleanup;
    PostSetupConfig postSetup;
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
