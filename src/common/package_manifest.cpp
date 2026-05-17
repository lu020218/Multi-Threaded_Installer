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
    manifest.identity.appPublisher = metadata.appPublisher;

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
    manifest.install.installState.registries = metadata.installState.registries;
    manifest.install.installState.files = metadata.installState.files;
    manifest.install.installState.detect = metadata.installState.detect;
    manifest.install.systemUninstallEntry.scope = metadata.systemUninstallEntry.scope;
    manifest.install.systemUninstallEntry.displayName = metadata.systemUninstallEntry.displayName;
    manifest.install.systemUninstallEntry.publisher = metadata.systemUninstallEntry.publisher;
    manifest.install.cleanup = metadata.installerCleanup;
    manifest.install.registryWrite = metadata.installerRegistryWrite;
    manifest.install.killProcesses = metadata.installKillProcesses;

    manifest.payload.totalCompressedSize = metadata.totalPayloadCompressedSize;
    manifest.payload.folders.reserve(metadata.extendedPayloadMappings.size());
    for (const auto& mapping : metadata.extendedPayloadMappings) {
        PackagePayloadFolder folder;
        folder.folderId = mapping.folderId;
        folder.folderName = mapping.folderName;
        folder.source = mapping.folderName;
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
    for (const auto& component : metadata.layoutComponents) {
        PackageComponentDefinition definition;
        definition.id = component.id;
        definition.name = component.name;
        definition.description = component.description;
        definition.required = component.required;
        definition.defaultSelected = component.defaultSelected;
        definition.sizeHintMB = component.sizeHintMB;
        definition.dependsOn = component.dependsOn;
        definition.payloadRefs = component.folders;
        definition.installAction.command = component.source.local.installer;
        definition.installAction.args = component.source.local.args;
        definition.installAction.workingDirectory = component.source.local.base;
        definition.installAction.wait = component.source.local.wait;
        definition.installAction.timeoutSec = component.source.local.timeoutSec;
        definition.uninstallAction.command = component.source.local.uninstall;
        manifest.components.definitions.push_back(std::move(definition));
    }

    manifest.ui.desktopShortcutDefaultName = metadata.desktopShortcutDefaultName;
    manifest.ui.desktopShortcutLocalizedNames = metadata.desktopShortcutLocalizedNames;
    manifest.ui.componentSelection = metadata.uiComponentSelection;
    manifest.ui.links = metadata.uiLinkBindings;

    manifest.lifecycle.uninstaller.cleanup = metadata.uninstallerCleanup;
    manifest.lifecycle.uninstaller.cleanup.installState =
        metadata.installStateCleanupMode.empty() ? "delete" : metadata.installStateCleanupMode;
    manifest.lifecycle.uninstaller.killBeforeUninstall = metadata.uninstallerKillBeforeUninstall;
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
    metadata.appPublisher = manifest.identity.appPublisher;

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
    metadata.installState.registries = manifest.install.installState.registries;
    metadata.installState.files = manifest.install.installState.files;
    metadata.installState.detect = manifest.install.installState.detect;
    metadata.systemUninstallEntry.scope = manifest.install.systemUninstallEntry.scope;
    metadata.systemUninstallEntry.displayName = manifest.install.systemUninstallEntry.displayName;
    metadata.systemUninstallEntry.publisher = manifest.install.systemUninstallEntry.publisher;
    metadata.installerCleanup = manifest.install.cleanup;
    metadata.installerRegistryWrite = manifest.install.registryWrite;
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
    if (metadata.layoutComponents.empty() && !manifest.components.definitions.empty()) {
        for (const auto& definition : manifest.components.definitions) {
            ComponentConfig component;
            component.id = definition.id;
            component.name = definition.name;
            component.description = definition.description;
            component.required = definition.required;
            component.defaultSelected = definition.defaultSelected;
            component.sizeHintMB = definition.sizeHintMB;
            component.dependsOn = definition.dependsOn;
            component.folders = definition.payloadRefs;
            component.source.type = ComponentSourceType::LOCAL;
            component.source.local.installer = definition.installAction.command;
            component.source.local.args = definition.installAction.args;
            component.source.local.base = definition.installAction.workingDirectory;
            component.source.local.wait = definition.installAction.wait;
            component.source.local.timeoutSec = definition.installAction.timeoutSec;
            component.source.local.uninstall = definition.uninstallAction.command;
            metadata.layoutComponents.push_back(std::move(component));
        }
    }
    metadata.desktopShortcutDefaultName = manifest.ui.desktopShortcutDefaultName;
    metadata.desktopShortcutLocalizedNames = manifest.ui.desktopShortcutLocalizedNames;
    metadata.uiComponentSelection = manifest.ui.componentSelection;
    metadata.uiLinkBindings = manifest.ui.links;
    metadata.uninstallerCleanup = manifest.lifecycle.uninstaller.cleanup;
    metadata.uninstallerKillBeforeUninstall = manifest.lifecycle.uninstaller.killBeforeUninstall;
    metadata.installStateCleanupMode = manifest.lifecycle.uninstaller.cleanup.installState.empty()
        ? "delete"
        : manifest.lifecycle.uninstaller.cleanup.installState;
    return metadata;
}

} // namespace MultiThreadedInstaller
