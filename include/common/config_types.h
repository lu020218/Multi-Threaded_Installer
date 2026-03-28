#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MultiThreadedInstaller {

enum class CompressionAlgorithm {
    LZMA_HIGH,
    ZSTD
};

enum class SpecialDirectoryType {
    INSTALL_DIRECTORY,
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
    std::string sourcePath;
    std::string targetPath;
    std::vector<std::string> files;
    size_t totalSize;

    FolderInfo() : totalSize(0) {}

    FolderInfo(const std::string& source, const std::string& target)
        : sourcePath(source), targetPath(target), totalSize(0) {}
};

struct FolderTargetConfig {
    std::string folderName;
    std::string targetDirectory;
    SpecialDirectoryType dirType;
    bool appendDirectoryName;

    FolderTargetConfig()
        : dirType(SpecialDirectoryType::INSTALL_DIRECTORY),
          appendDirectoryName(true) {}
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

struct PackagerConfiguration {
    std::string version;
    std::string applicationName;
    std::string appId;
    std::string directoryName;
    std::vector<std::string> legacyAppIds;
    std::string defaultInstallDir;
    std::string desktopShortcutName;
    std::unordered_map<std::string, std::string> desktopShortcutNameI18n;
    std::vector<std::string> legacyDesktopShortcutNames;
    std::string iconPath;
    std::string webPageUrl;
    std::string productName;
    std::string fileVersion;
    std::string productVersion;
    std::string companyName;
    std::string fileDescription;
    std::string copyright;
    CompressionAlgorithm compressionAlgorithm;
    int compressionLevel;
    std::vector<FolderTargetConfig> folderTargets;
    std::vector<RegistryEntry> registry;
    std::vector<std::string> installKillProcesses;
    std::vector<ComponentConfig> components;
    UiComponentSelectionConfig componentUi;
    std::vector<UiLinkBinding> uiLinks;
    std::vector<UninstallCleanupRule> uninstallCleanupRules;
    UpgradeCleanupConfig upgradeCleanup;
    bool autoStartup;
    bool desktopIcons;
    bool autoCleanOldInstall;
    bool requireAdmin;
    uint16_t minWindowsMajor;
    uint16_t minWindowsMinor;
    uint32_t minWindowsBuild;
    uint64_t sparseFileThresholdBytes;
    InstallStateConfig installState;

    PackagerConfiguration()
        : version("1.0"),
          applicationName("MyApplication"),
          appId(""),
          directoryName(""),
          defaultInstallDir("%ProgramFiles%"),
          desktopShortcutName(""),
          compressionAlgorithm(CompressionAlgorithm::LZMA_HIGH),
          compressionLevel(-1),
          autoStartup(false),
          desktopIcons(false),
          autoCleanOldInstall(false),
          requireAdmin(false),
          minWindowsMajor(0),
          minWindowsMinor(0),
          minWindowsBuild(0),
          sparseFileThresholdBytes(4 * 1024 * 1024) {}
};

} // namespace MultiThreadedInstaller
