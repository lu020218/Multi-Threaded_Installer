#include "installer/installed_instance_resolver.h"

#include "installer/install_manifest_store.h"
#include "installer/path_resolver.h"
#include "installer/registry_utils.h"
#include "common/utf8_utils.h"

#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <algorithm>
#include <cctype>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

std::string TrimAsciiCopy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

bool ExistingInstallDirectoryLooksValid(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(PathFromUtf8(path), ec) &&
           std::filesystem::is_directory(PathFromUtf8(path), ec);
}

// 只为取顶层 appVersion 而读上次安装清单。用 SAX 在读到 appVersion 时立即中止，
// 避免对后面 37641 条 files[]/fileFingerprints[] 做全量 DOM 解析（原本约 120ms）。
std::string ReadManifestAppVersionFast(const std::string& manifestPath) {
    if (manifestPath.empty()) {
        return {};
    }
    std::ifstream file(PathFromUtf8(manifestPath), std::ios::binary);
    if (!file) {
        return {};
    }

    struct AppVersionSax : public nlohmann::json_sax<nlohmann::json> {
        int depth = 0;
        bool expectValue = false;
        std::string version;

        bool start_object(std::size_t) override { ++depth; expectValue = false; return true; }
        bool end_object() override { --depth; return true; }
        bool start_array(std::size_t) override { ++depth; expectValue = false; return true; }
        bool end_array() override { --depth; return true; }
        bool key(string_t& val) override {
            expectValue = (depth == 1 && val == "appVersion");
            return true;
        }
        bool string(string_t& val) override {
            if (expectValue) {
                version = val;
                return false;  // 命中即中止解析
            }
            return true;
        }
        bool null() override { expectValue = false; return true; }
        bool boolean(bool) override { expectValue = false; return true; }
        bool number_integer(number_integer_t) override { expectValue = false; return true; }
        bool number_unsigned(number_unsigned_t) override { expectValue = false; return true; }
        bool number_float(number_float_t, const string_t&) override { expectValue = false; return true; }
        bool binary(binary_t&) override { expectValue = false; return true; }
        bool parse_error(std::size_t, const std::string&,
                         const nlohmann::detail::exception&) override {
            return false;
        }
    };

    AppVersionSax sax;
    nlohmann::json::sax_parse(file, &sax, nlohmann::json::input_format_t::json, false);
    return sax.version;
}

struct DetectCandidate {
    std::string source;
    std::string registryPath;
    std::string registryKey;
};

std::vector<DetectCandidate> BuildDetectCandidates(const ExtendedInstallationMetadata& metadata) {
    std::vector<DetectCandidate> candidates;

    const auto& detect = metadata.installState.detect;
    if (!detect.primary.registry.empty() && !detect.primary.value.empty()) {
        auto storeIt = std::find_if(metadata.installState.registries.begin(),
                                    metadata.installState.registries.end(),
                                    [&](const InstallStateRegistryStoreConfig& store) {
                                        return store.id == detect.primary.registry;
                                    });
        if (storeIt != metadata.installState.registries.end()) {
            auto valueIt = storeIt->values.find(detect.primary.value);
            if (valueIt != storeIt->values.end()) {
                const std::string registryKey = valueIt->second.key.empty() ? valueIt->first : valueIt->second.key;
                if (!storeIt->path.empty() && !registryKey.empty()) {
                    candidates.push_back({"installState", storeIt->path, registryKey});
                }
            }
        }
    }

    for (size_t i = 0; i < detect.legacy.size(); ++i) {
        const auto& legacy = detect.legacy[i];
        if (legacy.path.empty() || legacy.installDirValue.empty()) {
            continue;
        }
        const std::string source = legacy.id.empty()
                                       ? "legacy:" + std::to_string(i)
                                       : "legacy:" + legacy.id;
        candidates.push_back({source, legacy.path, legacy.installDirValue});
    }

    for (const auto& store : metadata.installState.registries) {
        auto logical = store.values.find("installDir");
        if (logical == store.values.end()) {
            continue;
        }
        const std::string registryKey = logical->second.key.empty() ? logical->first : logical->second.key;
        if (!store.path.empty() && !registryKey.empty()) {
            candidates.push_back({"installer.installState:" + store.id, store.path, registryKey});
        }
    }

    return candidates;
}

}  // namespace

bool resolveInstalledInstanceFromInstallRoots(const ExtendedInstallationMetadata& metadata,
                                              InstalledInstanceInfo& instanceInfo) {
    instanceInfo = InstalledInstanceInfo{};
    (void)metadata;
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

bool resolveInstallDirFromInstallStateStore(const ExtendedInstallationMetadata& metadata,
                                            InstallerPathResolver& resolver,
                                            std::string& installDir,
                                            std::string& manifestPath,
                                            std::string& detectSource,
                                            std::string& error) {
    installDir.clear();
    manifestPath.clear();
    detectSource.clear();
    error.clear();

    std::vector<DetectCandidate> candidates = BuildDetectCandidates(metadata);
    if (candidates.empty()) {
        error = "Install state detection requires installer.installState.detect.primary or legacy";
        return false;
    }

    std::string lastError;
    for (const auto& candidate : candidates) {
        std::string registryPath = resolver.expandEnvironmentVariables(candidate.registryPath);
        std::string registryKey = TrimAsciiCopy(candidate.registryKey);
        if (registryPath.empty() || registryKey.empty()) {
            lastError = "Install state detection registry path or key is empty";
            continue;
        }

        std::string candidateInstallDir;
        if (!readRegistryStringValue(registryPath, registryKey, candidateInstallDir)) {
            lastError = "Failed to read previous installDir from installState registry";
            continue;
        }

        candidateInstallDir = TrimAsciiCopy(resolver.expandEnvironmentVariables(candidateInstallDir));
        if (candidateInstallDir.empty()) {
            lastError = "Previous installDir in installState registry is empty";
            continue;
        }

        std::filesystem::path installPath = PathFromUtf8(candidateInstallDir).lexically_normal();
        std::error_code ec;
        if (!std::filesystem::exists(installPath, ec) || !std::filesystem::is_directory(installPath, ec)) {
            lastError = "Previous installDir from installState does not exist or is not a directory";
            continue;
        }

        std::filesystem::path manifest = installPath / "install.manifest.json";
        if (std::filesystem::exists(manifest, ec) && std::filesystem::is_regular_file(manifest, ec)) {
            manifestPath = Utf8FromPath(manifest);
        }
        installDir = Utf8FromPath(installPath);
        detectSource = candidate.source;
        return true;
    }

    error = lastError.empty() ? "Failed to resolve installDir from installState registry" : lastError;
    return false;
}

bool resolveInstalledInstanceFromInstallState(const ExtendedInstallationMetadata& metadata,
                                              InstallerPathResolver& resolver,
                                              InstalledInstanceInfo& instanceInfo,
                                              std::string* error) {
    instanceInfo = InstalledInstanceInfo{};
    std::string localError;
    std::string installDir;
    std::string manifestPath;
    std::string detectSource;
    if (!resolveInstallDirFromInstallStateStore(metadata, resolver, installDir, manifestPath, detectSource, localError)) {
        if (error) {
            *error = localError;
        }
        return false;
    }

    instanceInfo.found = true;
    instanceInfo.installDir = installDir;
    instanceInfo.manifestPath = manifestPath;
    instanceInfo.detectSource = detectSource;
    if (!manifestPath.empty()) {
        instanceInfo.installedVersion = ReadManifestAppVersionFast(manifestPath);
    }
    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace MultiThreadedInstaller
