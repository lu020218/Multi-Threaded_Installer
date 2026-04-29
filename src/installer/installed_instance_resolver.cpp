#include "installer/installed_instance_resolver.h"

#include "installer/install_manifest_store.h"
#include "installer/registry_utils.h"
#include "common/utf8_utils.h"

#include <filesystem>
#include <json.hpp>

namespace MultiThreadedInstaller {

namespace {

bool ExistingInstallDirectoryLooksValid(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(PathFromUtf8(path), ec) &&
           std::filesystem::is_directory(PathFromUtf8(path), ec);
}

nlohmann::json ReadManifestJsonLocal(const std::string& manifestPath) {
    nlohmann::json manifest;
    if (!manifestPath.empty()) {
        readManifest(manifestPath, manifest);
    }
    return manifest;
}

}  // namespace

bool resolveInstalledInstanceFromInstallRoots(const ExtendedInstallationMetadata& metadata,
                                              InstalledInstanceInfo& instanceInfo) {
    instanceInfo = InstalledInstanceInfo{};

    for (const auto& entry : metadata.lifecycleUpgradeCleanup.installRoots) {
        if (entry.path.empty() || entry.key.empty()) {
            continue;
        }

        std::string installDir;
        std::string manifestPath;
        if (!resolveInstallInfoFromRegistry(entry.path, entry.key, manifestPath, installDir)) {
            continue;
        }

        instanceInfo.found = true;
        instanceInfo.installDir = installDir;
        instanceInfo.manifestPath = manifestPath;
        if (!manifestPath.empty()) {
            nlohmann::json manifest = ReadManifestJsonLocal(manifestPath);
            instanceInfo.installedVersion = manifest.value("appVersion", "");
        }
        return true;
    }

    return false;
}

bool resolveInstallInfoFromRegistry(const std::string& registryPath,
                                    const std::string& registryKey,
                                    std::string& manifestPath,
                                    std::string& installDir) {
    manifestPath.clear();
    installDir.clear();
    if (registryPath.empty() || registryKey.empty()) {
        return false;
    }

    std::string candidateInstallDir;
    if (!readRegistryStringValue(registryPath, registryKey, candidateInstallDir) ||
        !ExistingInstallDirectoryLooksValid(candidateInstallDir)) {
        return false;
    }

    installDir = candidateInstallDir;
    std::filesystem::path localManifest = PathFromUtf8(candidateInstallDir) / "install.manifest.json";
    if (std::filesystem::exists(localManifest)) {
        manifestPath = Utf8FromPath(localManifest);
    }

    return true;
}

}  // namespace MultiThreadedInstaller
