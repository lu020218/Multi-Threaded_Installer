#include "common/package_manifest.h"

namespace MultiThreadedInstaller {

PackageManifest PackageManifestFromExtendedMetadata(const ExtendedInstallationMetadata& metadata) {
    PackageManifest manifest;
    manifest.version = metadata.version;

    manifest.identity.appName = metadata.appName;
    manifest.identity.appId = metadata.appId;
    manifest.identity.appDirectoryName = metadata.appDirectoryName;
    manifest.identity.appVersion = metadata.appVersion;
    manifest.identity.appWebsite = metadata.appWebsite;

    manifest.install.defaultDir = metadata.installDefaultDir;
    manifest.install.autoStartup = metadata.installAutoStartup;
    manifest.install.desktopIcon = metadata.installDesktopIcon;
    manifest.install.autoCleanOldInstall = metadata.installAutoCleanOldInstall;
    manifest.install.requireAdmin = metadata.installRequireAdmin;
    manifest.install.minWindowsMajor = metadata.installMinWindowsMajor;
    manifest.install.minWindowsMinor = metadata.installMinWindowsMinor;
    manifest.install.minWindowsBuild = metadata.installMinWindowsBuild;
    manifest.install.sparseFileThresholdBytes = metadata.installSparseFileThresholdBytes;
    manifest.install.useMutex = metadata.installUseMutex;
    manifest.install.mutexName = metadata.installMutexName;
    manifest.install.installInfo = metadata.installInfo;
    manifest.install.installRegistry = metadata.lifecycleInstallRegistry;
    manifest.install.killProcesses = metadata.installKillProcesses;

    manifest.payload.totalCompressedSize = metadata.totalPayloadCompressedSize;
    manifest.payload.folders.reserve(metadata.extendedPayloadMappings.size());
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        PackagePayloadFolder folder;
        folder.folderId = mapping.folderId;
        folder.folderName = mapping.folderName;
        folder.target = mapping.target.empty() ? mapping.targetPath : mapping.target;
        folder.offset = mapping.offset;
        folder.compressedSize = mapping.compressedSize;
        folder.originalSize = mapping.originalSize;
        folder.checksum = mapping.checksum;
        folder.algorithm = mapping.algorithm;
        folder.fileIndex = mapping.fileIndex;
        manifest.payload.folders.push_back(std::move(folder));
    }

    manifest.components.components = metadata.layoutComponents;

    manifest.ui.desktopShortcutDefaultName = metadata.desktopShortcutDefaultName;
    manifest.ui.desktopShortcutLocalizedNames = metadata.desktopShortcutLocalizedNames;
    manifest.ui.componentSelection = metadata.uiComponentSelection;
    manifest.ui.links = metadata.uiLinkBindings;

    manifest.lifecycle.uninstallCleanup = metadata.lifecycleUninstallCleanup;
    manifest.lifecycle.upgradeCleanup = metadata.lifecycleUpgradeCleanup;
    return manifest;
}

ExtendedInstallationMetadata PackageManifestToExtendedMetadata(const PackageManifest& manifest) {
    ExtendedInstallationMetadata metadata;
    metadata.version = manifest.version;
    metadata.folderCount = static_cast<uint32_t>(manifest.payload.folders.size());
    metadata.totalPayloadCompressedSize = manifest.payload.totalCompressedSize;

    metadata.appName = manifest.identity.appName;
    metadata.appId = manifest.identity.appId;
    metadata.appDirectoryName = manifest.identity.appDirectoryName;
    metadata.appVersion = manifest.identity.appVersion;
    metadata.appWebsite = manifest.identity.appWebsite;

    metadata.installDefaultDir = manifest.install.defaultDir;
    metadata.installAutoStartup = manifest.install.autoStartup;
    metadata.installDesktopIcon = manifest.install.desktopIcon;
    metadata.installAutoCleanOldInstall = manifest.install.autoCleanOldInstall;
    metadata.installRequireAdmin = manifest.install.requireAdmin;
    metadata.installMinWindowsMajor = manifest.install.minWindowsMajor;
    metadata.installMinWindowsMinor = manifest.install.minWindowsMinor;
    metadata.installMinWindowsBuild = manifest.install.minWindowsBuild;
    metadata.installSparseFileThresholdBytes = manifest.install.sparseFileThresholdBytes;
    metadata.installUseMutex = manifest.install.useMutex;
    metadata.installMutexName = manifest.install.mutexName;
    metadata.installInfo = manifest.install.installInfo;
    metadata.lifecycleInstallRegistry = manifest.install.installRegistry;
    metadata.installKillProcesses = manifest.install.killProcesses;

    metadata.extendedPayloadMappings.reserve(manifest.payload.folders.size());
    metadata.payloadMappings.reserve(manifest.payload.folders.size());
    for (const auto& folder : manifest.payload.folders) {
        ExtendedFolderMapping ext;
        ext.folderId = folder.folderId;
        ext.folderName = folder.folderName;
        ext.target = folder.target;
        ext.targetPath = folder.target;
        ext.offset = folder.offset;
        ext.compressedSize = folder.compressedSize;
        ext.originalSize = folder.originalSize;
        ext.checksum = folder.checksum;
        ext.algorithm = folder.algorithm;
        ext.fileIndex = folder.fileIndex;
        metadata.extendedPayloadMappings.push_back(ext);

        FolderMapping base;
        base.folderId = folder.folderId;
        base.folderName = folder.folderName;
        base.targetPath = folder.target;
        base.offset = folder.offset;
        base.compressedSize = folder.compressedSize;
        base.originalSize = folder.originalSize;
        base.checksum = folder.checksum;
        base.algorithm = folder.algorithm;
        metadata.payloadMappings.push_back(std::move(base));
    }

    metadata.layoutComponents = manifest.components.components;
    metadata.desktopShortcutDefaultName = manifest.ui.desktopShortcutDefaultName;
    metadata.desktopShortcutLocalizedNames = manifest.ui.desktopShortcutLocalizedNames;
    metadata.uiComponentSelection = manifest.ui.componentSelection;
    metadata.uiLinkBindings = manifest.ui.links;
    metadata.lifecycleUninstallCleanup = manifest.lifecycle.uninstallCleanup;
    metadata.lifecycleUpgradeCleanup = manifest.lifecycle.upgradeCleanup;
    return metadata;
}

} // namespace MultiThreadedInstaller
