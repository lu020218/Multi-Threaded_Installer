#pragma once

#include "common/archive_types.h"

namespace MultiThreadedInstaller {

struct PackageIdentity {
    std::string appName;
    std::string appId;
    std::string appDirectoryName;
    std::string appVersion;
    std::string appWebsite;
    std::string appPublisher;
};

struct PackageInstallStateManifest {
    std::vector<InstallStateRegistryStoreConfig> registries;
    std::vector<InstallStateFileStoreConfig> files;
    InstalledInstanceDetectConfig detect;
};

struct PackageSystemUninstallEntryManifest {
    UninstallEntryScope scope = UninstallEntryScope::ANY;
    std::string displayName;
    std::string publisher;
};

struct PackageInstallPolicy {
    std::string defaultDir;
    bool autoStartup = false;
    bool desktopIcon = false;
    bool autoCleanOldInstall = false;
    bool requireAdmin = false;
    uint16_t minWindowsMajor = 0;
    uint16_t minWindowsMinor = 0;
    uint32_t minWindowsBuild = 0;
    uint64_t sparseFileThresholdBytes = 4 * 1024 * 1024;
    bool useMutex = true;
    std::string mutexName;
    PackageInstallStateManifest installState;
    PackageSystemUninstallEntryManifest systemUninstallEntry;
    InstallerCleanupConfig cleanup;
    std::vector<InstallerRegistryWriteGroup> registryWrite;
    std::vector<std::string> killProcesses;
};

struct PackagePayloadFolder {
    std::string folderId;
    std::string folderName;
    std::string source;
    std::string target;
    bool required = false;
    uint64_t offset = 0;
    uint64_t compressedSize = 0;
    uint64_t originalSize = 0;
    uint32_t checksum = 0;
    CompressionAlgorithm algorithm = CompressionAlgorithm::LZMA2_XZ;
    std::vector<FileIndexEntry> fileIndex;
};

struct PackagePayloadManifest {
    uint64_t totalCompressedSize = 0;
    std::vector<PackagePayloadFolder> folders;
};

struct PackageComponentAction {
    std::string command;
    std::string args;
    std::string workingDirectory;
    bool wait = true;
    uint32_t timeoutSec = 900;
};

struct PackageComponentDefinition {
    std::string id;
    std::string name;
    std::string description;
    bool required = false;
    bool defaultSelected = true;
    uint32_t sizeHintMB = 0;
    std::vector<std::string> dependsOn;
    std::vector<std::string> payloadRefs;
    PackageComponentAction installAction;
    PackageComponentAction uninstallAction;
};

struct PackageComponentManifest {
    std::vector<ComponentConfig> components;
    std::vector<PackageComponentDefinition> definitions;
};

struct PackageUiManifest {
    std::string desktopShortcutDefaultName;
    std::unordered_map<std::string, std::string> desktopShortcutLocalizedNames;
    UiComponentSelectionConfig componentSelection;
    std::vector<UiLinkBinding> links;
};

struct PackageLifecyclePolicy {
    UninstallCleanupConfig uninstallCleanup;
    UpgradeCleanupConfig upgradeCleanup;
    UninstallerConfig uninstaller;
};

struct PackageManifest {
    uint32_t version = Constants::VERSION;
    PackageIdentity identity;
    PackageInstallPolicy install;
    PackagePayloadManifest payload;
    PackageComponentManifest components;
    PackageUiManifest ui;
    PackageLifecyclePolicy lifecycle;
};

PackageManifest PackageManifestFromExtendedMetadata(const ExtendedInstallationMetadata& metadata);
ExtendedInstallationMetadata PackageManifestToExtendedMetadata(const PackageManifest& manifest);

} // namespace MultiThreadedInstaller
