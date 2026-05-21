#include "installer/package_manifest_validator.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace MultiThreadedInstaller {
namespace {

bool IsValidCompressionAlgorithm(CompressionAlgorithm algorithm) {
    return algorithm == CompressionAlgorithm::LZMA2_XZ ||
           algorithm == CompressionAlgorithm::ZSTD;
}

bool IsValidRegistryValueType(RegistryValueType type) {
    return type == RegistryValueType::STRING ||
           type == RegistryValueType::DWORD ||
           type == RegistryValueType::EXPAND_STRING;
}

bool IsValidComponentSourceType(ComponentSourceType type) {
    return type == ComponentSourceType::EMBEDDED ||
           type == ComponentSourceType::LOCAL ||
           type == ComponentSourceType::DOWNLOAD;
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool StartsWithInstallDirToken(const std::string& path) {
    const std::string lowered = ToLowerCopy(path);
    return lowered.rfind("%installdir%", 0) == 0 ||
           lowered.rfind("installdirectory", 0) == 0;
}

bool IsHttpsUrl(const std::string& value) {
    return ToLowerCopy(value).rfind("https://", 0) == 0;
}

bool IsSha256Hex(const std::string& value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isxdigit(ch) != 0;
           });
}

bool ContainsParentTraversal(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    return normalized == ".." ||
           normalized.rfind("..\\", 0) == 0 ||
           normalized.find("\\..\\") != std::string::npos ||
           (normalized.size() >= 3 && normalized.compare(normalized.size() - 3, 3, "\\..") == 0);
}

bool IsValidUninstallEntryScope(UninstallEntryScope scope) {
    return scope == UninstallEntryScope::CURRENT_USER ||
           scope == UninstallEntryScope::LOCAL_MACHINE ||
           scope == UninstallEntryScope::WOW6432 ||
           scope == UninstallEntryScope::ANY ||
           scope == UninstallEntryScope::BOTH;
}

bool ValidateRegistryList(const std::vector<RegistryEntry>& entries, std::string& error) {
    for (const auto& entry : entries) {
        if (!IsValidRegistryValueType(entry.type)) {
            error = "Invalid registry value type in package manifest.";
            return false;
        }
    }
    return true;
}

bool ValidateUninstallEntries(const std::vector<UninstallEntryCleanup>& entries,
                              std::string& error) {
    for (const auto& entry : entries) {
        if (!IsValidUninstallEntryScope(entry.scope)) {
            error = "Invalid uninstall entry scope in package manifest.";
            return false;
        }
    }
    return true;
}

bool ValidateCleanup(const UninstallCleanupConfig& cleanup, std::string& error) {
    return ValidateRegistryList(cleanup.registry.legacyKeys, error) &&
           ValidateUninstallEntries(cleanup.uninstallEntries, error);
}

bool ValidateCleanup(const UpgradeCleanupConfig& cleanup, std::string& error) {
    return ValidateRegistryList(cleanup.registry.legacyKeys, error) &&
           ValidateUninstallEntries(cleanup.uninstallEntries, error);
}

} // namespace

bool ValidatePackageManifest(const PackageManifest& manifest, std::string& error) {
    error.clear();
    if (manifest.version != Constants::VERSION) {
        error = "Unsupported package manifest version.";
        return false;
    }
    if (manifest.identity.appName.empty() || manifest.identity.appVersion.empty()) {
        error = "Package manifest identity is incomplete.";
        return false;
    }
    if (manifest.install.defaultDir.empty()) {
        error = "Package manifest install default directory is empty.";
        return false;
    }
    for (const auto& group : manifest.install.registryWrite) {
        if (group.path.empty()) {
            error = "Package manifest installer registry write path is empty.";
            return false;
        }
        for (const auto& pair : group.values) {
            if (pair.first.empty() && pair.second.key.empty()) {
                error = "Package manifest installer registry write value key is empty.";
                return false;
            }
        }
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

    std::unordered_map<std::string, const ComponentConfig*> components;
    for (const auto& component : manifest.components.components) {
        if (component.id.empty()) {
            error = "Package manifest component id is empty.";
            return false;
        }
        if (!components.emplace(component.id, &component).second) {
            error = "Package manifest contains duplicate component id: " + component.id;
            return false;
        }
        if (!IsValidComponentSourceType(component.source.type)) {
            error = "Package manifest contains invalid component source type.";
            return false;
        }
        if (component.source.type == ComponentSourceType::LOCAL &&
            component.source.local.installer.empty()) {
            error = "Package manifest local component installer command is empty.";
            return false;
        }
        if (component.source.type == ComponentSourceType::DOWNLOAD) {
            if (!IsHttpsUrl(component.source.download.url)) {
                error = "Package manifest download component URL must use HTTPS.";
                return false;
            }
            if (!component.source.download.sha256.empty() &&
                !IsSha256Hex(component.source.download.sha256)) {
                error = "Package manifest download component SHA256 is invalid.";
                return false;
            }
            if (!StartsWithInstallDirToken(component.source.download.saveAs) ||
                ContainsParentTraversal(component.source.download.saveAs)) {
                error = "Package manifest download component save path must be under install dir.";
                return false;
            }
        }
        for (const auto& folderId : component.folders) {
            if (folderIds.find(folderId) == folderIds.end()) {
                error = "Package manifest component references unknown folder: " + folderId;
                return false;
            }
        }
    }

    enum class VisitState { Visiting, Visited };
    std::unordered_map<std::string, VisitState> visit;
    std::function<bool(const std::string&)> dfs = [&](const std::string& id) {
        auto state = visit.find(id);
        if (state != visit.end()) {
            if (state->second == VisitState::Visiting) {
                error = "Package manifest component dependency cycle detected: " + id;
                return false;
            }
            return true;
        }
        auto componentIt = components.find(id);
        if (componentIt == components.end()) {
            error = "Package manifest component dependency not found: " + id;
            return false;
        }
        visit[id] = VisitState::Visiting;
        for (const auto& dep : componentIt->second->dependsOn) {
            if (!dfs(dep)) {
                return false;
            }
        }
        visit[id] = VisitState::Visited;
        return true;
    };
    for (const auto& item : components) {
        if (!dfs(item.first)) {
            return false;
        }
    }

    return true;
}

bool ValidateExtendedInstallationMetadata(const ExtendedInstallationMetadata& metadata,
                                          std::string& error) {
    PackageManifest manifest = PackageManifestFromExtendedMetadata(metadata);
    if (!ValidatePackageManifest(manifest, error)) {
        return false;
    }
    if (metadata.extendedPayloadMappings.size() != metadata.folderCount ||
        metadata.payloadMappings.size() != metadata.folderCount) {
        error = "Package manifest folder count mismatch.";
        return false;
    }
    return true;
}

} // namespace MultiThreadedInstaller
