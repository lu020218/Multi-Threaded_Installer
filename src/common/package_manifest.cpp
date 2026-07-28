#include "common/package_manifest.h"

namespace MultiThreadedInstaller {

// 钩子三层共用 HookScript 后，桥接不再需要逐字段转换，直接整 vector 拷贝。

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

    manifest.hooks.preInstall = metadata.preInstall;
    manifest.hooks.postInstall = metadata.postInstall;
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

    metadata.preInstall = manifest.hooks.preInstall;
    metadata.postInstall = manifest.hooks.postInstall;
    return metadata;
}

} // namespace MultiThreadedInstaller
