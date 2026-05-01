#pragma once

#include "common/archive_types.h"

namespace MultiThreadedInstaller {

struct PackageIdentity {
    std::string appName;
    std::string appId;
    std::string appDirectoryName;
    std::string appVersion;
    std::string appWebsite;
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
    InstallInfoConfig installInfo;
    std::vector<RegistryEntry> installRegistry;
    std::vector<std::string> killProcesses;
};

struct PackagePayloadFolder {
    std::string folderId;
    std::string folderName;
    std::string target;
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

struct PackageComponentManifest {
    std::vector<ComponentConfig> components;
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
