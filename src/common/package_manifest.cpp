#include "common/package_manifest.h"

namespace MultiThreadedInstaller {
namespace {

PackageHook HookFromScript(const HookScript& script) {
    PackageHook hook;
    hook.present = script.present;
    hook.scriptName = script.scriptName;
    hook.content = script.content;
    hook.args = script.args;
    hook.onFailure = script.onFailure == 1 ? HookOnFailure::CONTINUE : HookOnFailure::ABORT;
    hook.timeoutSec = script.timeoutSec;
    hook.auxFiles = script.auxFiles;
    hook.keep = script.keep;
    hook.keepDir = script.keepDir;
    return hook;
}

HookScript ScriptFromHook(const PackageHook& hook) {
    HookScript script;
    script.present = hook.present;
    script.scriptName = hook.scriptName;
    script.content = hook.content;
    script.args = hook.args;
    script.onFailure = hook.onFailure == HookOnFailure::CONTINUE ? 1 : 0;
    script.timeoutSec = hook.timeoutSec;
    script.auxFiles = hook.auxFiles;
    script.keep = hook.keep;
    script.keepDir = hook.keepDir;
    return script;
}

} // namespace

PackageManifest PackageManifestFromExtendedMetadata(const ExtendedInstallationMetadata& metadata) {
    PackageManifest manifest;
    manifest.version = metadata.version;

    manifest.identity.productName = metadata.appProductName;
    manifest.identity.appName = metadata.appName;
    manifest.identity.appId = metadata.appId;
    manifest.identity.publisher = metadata.appPublisher;
    manifest.identity.version = metadata.appVersion;
    manifest.identity.defaultDir = metadata.appDefaultDir;

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
        folder.framed = mapping.framed;
        folder.fileIndex = mapping.fileIndex;
        manifest.payload.folders.push_back(std::move(folder));
    }

    manifest.hooks.preInstall.reserve(metadata.preInstall.size());
    for (const auto& script : metadata.preInstall) {
        manifest.hooks.preInstall.push_back(HookFromScript(script));
    }
    manifest.hooks.postInstall.reserve(metadata.postInstall.size());
    for (const auto& script : metadata.postInstall) {
        manifest.hooks.postInstall.push_back(HookFromScript(script));
    }
    return manifest;
}

ExtendedInstallationMetadata PackageManifestToExtendedMetadata(const PackageManifest& manifest) {
    ExtendedInstallationMetadata metadata;
    metadata.version = manifest.version;
    metadata.folderCount = static_cast<uint32_t>(manifest.payload.folders.size());
    metadata.totalPayloadCompressedSize = manifest.payload.totalCompressedSize;

    metadata.appProductName = manifest.identity.productName;
    // appName 为空回退产品名（防御：正常打包链路下必填）。
    metadata.appName = manifest.identity.appName.empty() ? manifest.identity.productName
                                                         : manifest.identity.appName;
    metadata.appId = manifest.identity.appId;
    metadata.appPublisher = manifest.identity.publisher;
    metadata.appVersion = manifest.identity.version;
    metadata.appDefaultDir = manifest.identity.defaultDir;

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
        ext.framed = folder.framed;
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

    metadata.preInstall.reserve(manifest.hooks.preInstall.size());
    for (const auto& hook : manifest.hooks.preInstall) {
        metadata.preInstall.push_back(ScriptFromHook(hook));
    }
    metadata.postInstall.reserve(manifest.hooks.postInstall.size());
    for (const auto& hook : manifest.hooks.postInstall) {
        metadata.postInstall.push_back(ScriptFromHook(hook));
    }
    return metadata;
}

} // namespace MultiThreadedInstaller
