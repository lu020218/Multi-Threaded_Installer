#include "installer/payload/package_manifest_validator.h"

#include <algorithm>
#include <unordered_set>

namespace MultiThreadedInstaller {
namespace {

bool IsValidCompressionAlgorithm(CompressionAlgorithm algorithm) {
    return algorithm == CompressionAlgorithm::LZMA2_XZ ||
           algorithm == CompressionAlgorithm::ZSTD ||
           algorithm == CompressionAlgorithm::NONE;
}

bool IsValidHookOnFailure(HookOnFailure onFailure) {
    return onFailure == HookOnFailure::ABORT ||
           onFailure == HookOnFailure::CONTINUE;
}

bool ValidateHook(const HookScript& hook, std::string& error) {
    if (!hook.present) {
        return true;
    }
    if (hook.content.empty()) {
        error = "Package manifest hook is marked present but carries no script content.";
        return false;
    }
    if (!IsValidHookOnFailure(hook.onFailure)) {
        error = "Package manifest hook has an invalid onFailure policy.";
        return false;
    }
    if (hook.timeoutSec == 0) {
        error = "Package manifest hook timeout must be greater than zero.";
        return false;
    }
    return true;
}

} // namespace

// 重构后的 manifest 只有三块：identity + payload + hooks。校验随之收窄。
bool ValidatePackageManifest(const PackageManifest& manifest, std::string& error) {
    error.clear();
    if (manifest.version != Constants::VERSION) {
        error = "Unsupported package manifest version.";
        return false;
    }
    if (manifest.identity.productName.empty() || manifest.identity.version.empty() ||
        manifest.identity.appName.empty() || manifest.identity.appId.empty()) {
        error = "Package manifest identity is incomplete.";
        return false;
    }
    if (manifest.identity.defaultDir.empty()) {
        error = "Package manifest default directory is empty.";
        return false;
    }

    std::unordered_set<std::string> folderIds;
    uint64_t maxPayloadEnd = 0;
    for (const auto& folder : manifest.payload.folders) {
        if (folder.folderId.empty() || folder.folderName.empty()) {
            error = "Package manifest payload folder identity is incomplete.";
            return false;
        }
        if (!folderIds.insert(folder.folderId).second) {
            error = "Package manifest contains duplicate payload folder id: " + folder.folderId;
            return false;
        }
        if (folder.target.empty()) {
            error = "Package manifest payload folder target is empty: " + folder.folderId;
            return false;
        }
        if (!IsValidCompressionAlgorithm(folder.algorithm)) {
            error = "Package manifest contains invalid compression algorithm.";
            return false;
        }
        if (folder.compressedSize == 0 && folder.originalSize != 0) {
            error = "Package manifest contains empty compressed payload for non-empty folder.";
            return false;
        }
        if (folder.offset + folder.compressedSize < folder.offset) {
            error = "Package manifest payload range overflows.";
            return false;
        }
        maxPayloadEnd = std::max(maxPayloadEnd, folder.offset + folder.compressedSize);
    }
    if (maxPayloadEnd > manifest.payload.totalCompressedSize) {
        error = "Package manifest payload range exceeds total compressed size.";
        return false;
    }

    for (const auto& hook : manifest.hooks.preInstall) {
        if (!ValidateHook(hook, error)) {
            return false;
        }
    }
    for (const auto& hook : manifest.hooks.postInstall) {
        if (!ValidateHook(hook, error)) {
            return false;
        }
    }

    return true;
}

} // namespace MultiThreadedInstaller
