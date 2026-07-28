#include "installer/state/installed_instance_resolver.h"

#include "installer/state/install_manifest_store.h"
#include "installer/platform/path_resolver.h"
#include "installer/state/registry_utils.h"
#include "common/engine_defaults.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"

#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

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

    // 新 schema(manifestVersion 4+)读顶层 "app" 对象的 "version"；旧 schema 读顶层
    // "appVersion"。两种命中任一即中止解析（同一 manifest 只会存在其中一种）。
    struct AppVersionSax : public nlohmann::json_sax<nlohmann::json> {
        int depth = 0;
        int appObjectDepth = 0;   // 顶层 "app" 对象的深度（0=未进入）
        bool pendingAppObject = false;
        bool expectValue = false;
        std::string version;

        bool start_object(std::size_t) override {
            ++depth;
            if (pendingAppObject) {
                appObjectDepth = depth;
                pendingAppObject = false;
            }
            expectValue = false;
            return true;
        }
        bool end_object() override {
            if (appObjectDepth == depth) {
                appObjectDepth = 0;
            }
            --depth;
            return true;
        }
        bool start_array(std::size_t) override { ++depth; pendingAppObject = false; expectValue = false; return true; }
        bool end_array() override { --depth; return true; }
        bool key(string_t& val) override {
            pendingAppObject = (depth == 1 && val == "app");
            expectValue = (depth == 1 && val == "appVersion") ||
                          (appObjectDepth != 0 && depth == appObjectDepth && val == "version");
            return true;
        }
        bool string(string_t& val) override {
            if (expectValue) {
                version = val;
                return false;  // 命中即中止解析
            }
            pendingAppObject = false;
            return true;
        }
        bool null() override { expectValue = false; pendingAppObject = false; return true; }
        bool boolean(bool) override { expectValue = false; pendingAppObject = false; return true; }
        bool number_integer(number_integer_t) override { expectValue = false; pendingAppObject = false; return true; }
        bool number_unsigned(number_unsigned_t) override { expectValue = false; pendingAppObject = false; return true; }
        bool number_float(number_float_t, const string_t&) override { expectValue = false; pendingAppObject = false; return true; }
        bool binary(binary_t&) override { expectValue = false; pendingAppObject = false; return true; }
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

std::vector<DetectCandidate> BuildDetectCandidates(const PackageManifest& metadata) {
    // [旧版本-发现安装路径] 构造探测候选：读引擎写死的 HKLM\Software\<product>\InstallDir
    // 作为唯一来源，用来发现机器上已存在的旧版本安装目录（需求 §5）。
    std::vector<DetectCandidate> candidates;
    if (!metadata.identity.productName.empty()) {
        candidates.push_back({"installState",
                              EngineDefaults::RegistryPath(metadata.identity.productName),
                              "InstallDir"});
    }
    return candidates;
}

}  // namespace

// [旧版本-发现安装路径] 核心实现：从 installState 注册表读出旧 InstallDir，校验目录存在，
// 再在该目录下定位 install.manifest.json（卸载/升级清理要消费的旧版本快照）。
bool resolveInstallDirFromInstallStateStore(const PackageManifest& metadata,
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
// 版本号优先级：产品注册表 Version → 旧 manifest appVersion（注册表由安装收尾统一写入，
// 是权威来源；manifest 仅作注册表值缺失/损坏时的兜底）。
bool resolveInstalledInstanceFromInstallState(const PackageManifest& metadata,
                                              InstallerPathResolver& resolver,
                                              InstalledInstanceInfo& instanceInfo,
                                              std::string* error) {
    instanceInfo = InstalledInstanceInfo{};
    std::string localError;
    std::string installDir;
    std::string manifestPath;
    std::string detectSource;
    if (!resolveInstallDirFromInstallStateStore(metadata, resolver, installDir, manifestPath, detectSource, localError)) {
        instanceInfo.detectError = localError;
        if (error) {
            *error = localError;
        }
        return false;
    }

    instanceInfo.found = true;
    instanceInfo.installDir = installDir;
    instanceInfo.manifestPath = manifestPath;
    instanceInfo.detectSource = detectSource;

    std::string registryVersion;
    if (readRegistryStringValue(EngineDefaults::RegistryPath(metadata.identity.productName),
                                "Version", registryVersion) &&
        !TrimAsciiCopy(registryVersion).empty()) {
        instanceInfo.installedVersion = TrimAsciiCopy(registryVersion);
    } else if (!manifestPath.empty()) {
        instanceInfo.installedVersion = ReadManifestAppVersionFast(manifestPath);
    }
    if (error) {
        error->clear();
    }
    return true;
}

// [旧版本-发现安装路径] 进程级快照：首次调用做一次真实探测并缓存（含"未检出"结果），
// 后续直接复用。安装/卸载单次运行内旧安装状态不应在探测点之间变化，快照消除了
// 重复注册表/磁盘/manifest 读取与多次探测间的不一致。
InstalledInstanceInfo GetInstalledInstanceSnapshot(const PackageManifest& metadata,
                                                   InstallerPathResolver& resolver) {
    static std::mutex snapshotMutex;
    static bool snapshotTaken = false;
    static std::string snapshotProduct;
    static InstalledInstanceInfo snapshot;

    std::lock_guard<std::mutex> lock(snapshotMutex);
    if (!snapshotTaken || snapshotProduct != metadata.identity.productName) {
        InstalledInstanceInfo fresh;
        resolveInstalledInstanceFromInstallState(metadata, resolver, fresh, nullptr);
        snapshot = std::move(fresh);
        snapshotProduct = metadata.identity.productName;
        snapshotTaken = true;
        logInstallerInfo(std::string("[InstalledInstance] snapshot taken found=") +
                         (snapshot.found ? "true" : "false") +
                         " installDir=" + snapshot.installDir +
                         " version=" + snapshot.installedVersion +
                         " manifest=" + (snapshot.manifestPath.empty() ? "<none>" : snapshot.manifestPath));
    }
    return snapshot;
}

}  // namespace MultiThreadedInstaller
