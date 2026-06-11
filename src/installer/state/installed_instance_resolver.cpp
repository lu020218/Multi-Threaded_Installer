#include "installer/state/installed_instance_resolver.h"

#include "installer/state/install_manifest_store.h"
#include "installer/platform/path_resolver.h"
#include "installer/state/registry_utils.h"
#include "common/engine_defaults.h"
#include "common/utf8_utils.h"

#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <algorithm>
#include <cctype>
#include <vector>

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
    // [旧版本-发现安装路径] 构造探测候选：读引擎写死的 HKLM\Software\<product>\InstallDir
    // 作为唯一来源，用来发现机器上已存在的旧版本安装目录（需求 §5）。
    std::vector<DetectCandidate> candidates;
    if (!metadata.appProductName.empty()) {
        candidates.push_back({"installState",
                              EngineDefaults::RegistryPath(metadata.appProductName),
                              "InstallDir"});
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

// [旧版本-发现安装路径] 核心实现：从 installState 注册表读出旧 InstallDir，校验目录存在，
// 再在该目录下定位 install.manifest.json（卸载/升级清理要消费的旧版本快照）。
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

// [旧版本-发现安装路径] 对外封装：发现旧安装目录 + 旧 manifest 路径 + 旧版本号，
// 打包成 InstalledInstanceInfo 供安装计划/卸载判定覆盖安装与升级目标。
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
