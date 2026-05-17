#include "packager/package_manifest_builder.h"

#include "common/utf8_utils.h"

#include <filesystem>
#include <unordered_map>

namespace MultiThreadedInstaller {
namespace {

PackageComponentAction BuildInstallAction(const ComponentConfig& component) {
    PackageComponentAction action;
    action.command = component.source.local.installer;
    action.args = component.source.local.args;
    action.workingDirectory = component.source.local.base;
    action.wait = component.source.local.wait;
    action.timeoutSec = component.source.local.timeoutSec;
    return action;
}

PackageComponentAction BuildUninstallAction(const ComponentConfig& component) {
    PackageComponentAction action;
    action.command = component.source.local.uninstall;
    action.wait = true;
    action.timeoutSec = component.source.local.timeoutSec;
    return action;
}

PackageComponentDefinition BuildComponentDefinition(const ComponentConfig& component) {
    PackageComponentDefinition definition;
    definition.id = component.id;
    definition.name = component.name;
    definition.description = component.description;
    definition.required = component.required;
    definition.defaultSelected = component.defaultSelected;
    definition.sizeHintMB = component.sizeHintMB;
    definition.dependsOn = component.dependsOn;
    definition.payloadRefs = component.folders;
    definition.installAction = BuildInstallAction(component);
    definition.uninstallAction = BuildUninstallAction(component);
    return definition;
}

std::string FolderNameFromPath(const std::string& path) {
    return Utf8FromPath(PathFromUtf8(path).filename());
}

} // namespace

PackageManifest PackageManifestBuilder::build(const std::vector<CompressionResult>& results,
                                              const std::vector<FolderInfo>& folderInfos,
                                              const PackagerConfiguration& config) const {
    PackageManifest manifest;

    manifest.identity.appName = config.app.name;
    manifest.identity.appId = config.app.id;
    manifest.identity.appDirectoryName = config.installer.directoryName;
    manifest.identity.appVersion = config.app.version;
    manifest.identity.appWebsite = config.app.website;
    manifest.identity.appPublisher = config.app.publisher;

    manifest.install.defaultDir = config.installer.defaultDir;
    manifest.install.autoStartup = config.installer.defaults.autoStartup;
    manifest.install.desktopIcon = config.installer.defaults.desktopShortcut;
    manifest.install.autoCleanOldInstall = config.install.autoCleanOldInstall;
    manifest.install.requireAdmin = config.installer.requireAdmin;
    manifest.install.minWindowsMajor = config.installer.minWindows.major;
    manifest.install.minWindowsMinor = config.installer.minWindows.minor;
    manifest.install.minWindowsBuild = config.installer.minWindows.build;
    manifest.install.sparseFileThresholdBytes = config.installer.largeFileThresholdBytes;
    manifest.install.useMutex = !config.installer.mutex.empty();
    manifest.install.mutexName = config.installer.mutex;
    manifest.install.killProcesses = config.installer.killBeforeInstall;
    manifest.install.installState.registries = config.installer.installState.registries;
    manifest.install.installState.files = config.installer.installState.files;
    manifest.install.installState.detect = config.installer.installState.detect;
    manifest.install.systemUninstallEntry.scope = config.installer.systemUninstallEntry.scope;
    manifest.install.systemUninstallEntry.displayName = config.installer.systemUninstallEntry.displayName;
    manifest.install.systemUninstallEntry.publisher = config.installer.systemUninstallEntry.publisher;
    manifest.install.cleanup = config.installer.cleanup;
    manifest.install.registryWrite = config.installer.registry.write;

    std::unordered_map<std::string, PayloadConfig> payloadById;
    for (const auto& payload : config.installer.payload) {
        payloadById[payload.id] = payload;
    }

    uint64_t offset = 0;
    const size_t count = std::min(results.size(), folderInfos.size());
    manifest.payload.folders.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& info = folderInfos[i];
        const auto& result = results[i];
        auto payloadIt = payloadById.find(info.id);

        PackagePayloadFolder folder;
        folder.folderId = info.id;
        folder.folderName = FolderNameFromPath(info.sourcePath);
        folder.source = payloadIt == payloadById.end() ? info.sourcePath : payloadIt->second.source;
        folder.target = payloadIt == payloadById.end() ? info.targetPath : payloadIt->second.target;
        folder.required = payloadIt != payloadById.end() && payloadIt->second.required;
        folder.offset = offset;
        folder.compressedSize = static_cast<uint64_t>(result.compressedSize);
        folder.originalSize = static_cast<uint64_t>(result.originalSize);
        folder.checksum = result.checksum;
        folder.algorithm = result.algorithm;
        folder.fileIndex = result.fileIndex;
        manifest.payload.totalCompressedSize += folder.compressedSize;
        offset += folder.compressedSize;
        manifest.payload.folders.push_back(std::move(folder));
    }

    manifest.components.components = config.installer.components;
    for (const auto& component : config.installer.components) {
        manifest.components.definitions.push_back(BuildComponentDefinition(component));
    }

    manifest.ui.desktopShortcutDefaultName = config.installer.ui.desktopShortcut.defaultName.empty()
        ? config.app.name
        : config.installer.ui.desktopShortcut.defaultName;
    manifest.ui.desktopShortcutLocalizedNames = config.installer.ui.desktopShortcut.i18n;
    manifest.ui.componentSelection = config.installer.ui.componentSelection;
    manifest.ui.links = config.installer.ui.links;

    manifest.lifecycle.uninstaller = config.uninstaller;
    return manifest;
}

} // namespace MultiThreadedInstaller
