#include "installer/state/install_manifest_store.h"

#include "installer/platform/file_system_operator.h"
#include "installer/platform/installer_helpers.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"

#include <filesystem>
#include <fstream>

namespace MultiThreadedInstaller {

using json = nlohmann::json;

#ifdef _WIN32
namespace {

bool IsValidUtf8(const std::string& text) {
    size_t i = 0;
    const size_t len = text.size();
    while (i < len) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c <= 0x7F) {
            ++i;
        } else if ((c >> 5) == 0x6) {
            if (i + 1 >= len) return false;
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            if ((c1 >> 6) != 0x2) return false;
            i += 2;
        } else if ((c >> 4) == 0xE) {
            if (i + 2 >= len) return false;
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2) return false;
            i += 3;
        } else if ((c >> 3) == 0x1E) {
            if (i + 3 >= len) return false;
            unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            unsigned char c3 = static_cast<unsigned char>(text[i + 3]);
            if ((c1 >> 6) != 0x2 || (c2 >> 6) != 0x2 || (c3 >> 6) != 0x2) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

std::string EnsureUtf8(const std::string& text) {
    if (text.empty()) {
        return text;
    }
    if (IsValidUtf8(text)) {
        return text;
    }
    std::string utf8 = AcpToUtf8(text);
    return utf8.empty() ? text : utf8;
}

}  // namespace
#else
namespace {
std::string EnsureUtf8(const std::string& text) { return text; }
}  // namespace
#endif

namespace {

std::vector<std::string> EnsureUtf8List(const std::vector<std::string>& values) {
    std::vector<std::string> safeValues;
    safeValues.reserve(values.size());
    for (const auto& value : values) {
        safeValues.push_back(EnsureUtf8(value));
    }
    return safeValues;
}

json NamedEntriesToManifestJson(const std::vector<NamedCleanupEntry>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        if (!entry.name.empty()) {
            out.push_back({{"name", EnsureUtf8(entry.name)}});
        }
    }
    return out;
}

// 卸载入口账本只记名字；删除时按名字推导键精确删除（deleteSystemUninstallEntry）。
json UninstallEntriesToManifestJson(const std::vector<UninstallEntryCleanup>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        if (!entry.name.empty()) {
            out.push_back({{"name", EnsureUtf8(entry.name)}});
        }
    }
    return out;
}

// 自定义注册表项卸载账本：只记位置(path/key)，删除不需要值/类型。
json RegistryEntriesToManifestJson(const std::vector<RegistryEntry>& entries) {
    json out = json::array();
    for (const auto& entry : entries) {
        if (!entry.path.empty() && !entry.key.empty()) {
            out.push_back({
                {"path", EnsureUtf8(entry.path)},
                {"key", EnsureUtf8(entry.key)}
            });
        }
    }
    return out;
}

}  // namespace

bool writeManifest(const std::string& manifestPath,
                   const std::string& appId,
                   const std::string& displayName,
                   const std::string& appVersion,
                   const std::string& installDir,
                   const std::vector<std::string>& cleanupRoots,
                   const UninstallCleanupConfig& actualCleanupSnapshot,
                   const std::vector<std::string>& filePaths,
                   const std::vector<std::string>& killProcesses,
                   bool autoStartup,
                   bool desktopIcons,
                   const std::string& desktopShortcutDisplayName,
                   const std::string& uninstallPath,
                   const std::string& languageCode,
                   const std::string& appPublisher,
                   const InstalledFileFingerprintMap& fileFingerprints,
                   const std::vector<std::string>& installedComponentIds) {
    if (manifestPath.empty()) {
        return false;
    }

    try {
        json root;
        root["version"] = "1.0";
        root["manifestVersion"] = 3;
        root["appId"] = EnsureUtf8(appId);
        root["displayName"] = EnsureUtf8(displayName);
        root["appName"] = EnsureUtf8(displayName);
        root["appVersion"] = EnsureUtf8(appVersion);
        root["installDir"] = EnsureUtf8(installDir);
        root["uninstallPath"] = EnsureUtf8(uninstallPath);
        root["cleanupRoots"] = EnsureUtf8List(cleanupRoots);
        root["publisher"] = EnsureUtf8(appPublisher);

        std::vector<std::string> safeFiles;
        safeFiles.reserve(filePaths.size());
        for (const auto& path : filePaths) {
            safeFiles.push_back(EnsureUtf8(path));
        }
        root["files"] = safeFiles;

        // Per-file fingerprints enable the next upgrade's zero-read skip path.
        if (!fileFingerprints.empty()) {
            json fingerprintArray = json::array();
            for (const auto& path : safeFiles) {
                const auto it = fileFingerprints.find(normalizePathForCompare(path));
                if (it == fileFingerprints.end()) {
                    continue;
                }
                fingerprintArray.push_back({
                    {"path", path},
                    {"size", it->second.size},
                    {"contentHash", it->second.contentHash},
                });
            }
            if (!fingerprintArray.empty()) {
                root["fileFingerprints"] = std::move(fingerprintArray);
            }
        }

        root["autoStartup"] = autoStartup;
        root["desktopIcon"] = desktopIcons;
        root["desktopShortcutDisplayName"] = EnsureUtf8(desktopShortcutDisplayName);
        root["language"] = EnsureUtf8(languageCode);
        root["killProcesses"] = EnsureUtf8List(killProcesses);
        // 已安装组件 id：卸载时据此反向执行各组件的卸载程序（清理其注册表等）。
        root["installedComponents"] = EnsureUtf8List(installedComponentIds);

        // 运行时卸载账本：卸载主流程按此撤销已写入的快捷方式/启动项/系统卸载入口。
        json cleanup;
        cleanup["shortcuts"] = NamedEntriesToManifestJson(actualCleanupSnapshot.shortcuts);
        cleanup["startup"] = NamedEntriesToManifestJson(actualCleanupSnapshot.startup);
        cleanup["processes"] = NamedEntriesToManifestJson(actualCleanupSnapshot.processes);
        cleanup["uninstallEntries"] = UninstallEntriesToManifestJson(actualCleanupSnapshot.uninstallEntries);
        cleanup["registry"] = RegistryEntriesToManifestJson(actualCleanupSnapshot.registryEntries);
        root["cleanup"] = std::move(cleanup);

        std::filesystem::path path = PathFromUtf8(manifestPath);
        std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            FileSystemOperator fs;
            if (!fs.createDirectoryRecursive(Utf8FromPath(parent))) {
                return false;
            }
        }

        std::ofstream out(toLongPath(path), std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        std::string payload = root.dump(2, ' ', false, json::error_handler_t::replace);
        out.write(payload.c_str(), static_cast<std::streamsize>(payload.size()));
        return static_cast<bool>(out);
    } catch (const std::exception& e) {
        logInstallerError(std::string("[UNINSTALL] Failed to write manifest: ") + e.what());
        return false;
    }
}

bool readManifest(const std::string& manifestPath, json& outManifest) {
    if (manifestPath.empty()) {
        return false;
    }
    std::ifstream in(toLongPath(PathFromUtf8(manifestPath)), std::ios::binary);
    if (!in) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty()) {
        return false;
    }
    outManifest = json::parse(content, nullptr, false);
    return !outManifest.is_discarded();
}

bool loadPreviousInstallOptions(const std::string& manifestPath,
                                PreviousInstallOptions& options,
                                std::string& error) {
    options = PreviousInstallOptions{};
    error.clear();

    json manifest;
    if (!readManifest(manifestPath, manifest)) {
        error = "Failed to read previous install manifest";
        return false;
    }
    if (!manifest.contains("autoStartup") || !manifest["autoStartup"].is_boolean() ||
        !manifest.contains("desktopIcon") || !manifest["desktopIcon"].is_boolean() ||
        !manifest.contains("language") || !manifest["language"].is_string()) {
        error = "Previous install manifest does not contain complete install options";
        return false;
    }

    options.autoStartup = manifest.value("autoStartup", false);
    options.desktopIcon = manifest.value("desktopIcon", false);
    options.languageCode = manifest.value("language", "");
    return true;
}

bool loadPreviousInstallFileFingerprints(const std::string& manifestPath,
                                         InstalledFileFingerprintMap& out) {
    out.clear();

    json manifest;
    if (!readManifest(manifestPath, manifest)) {
        return false;
    }
    return loadPreviousInstallFileFingerprints(manifest, out);
}

bool loadPreviousInstallFileFingerprints(const json& manifest, InstalledFileFingerprintMap& out) {
    out.clear();
    if (!manifest.contains("fileFingerprints") || !manifest["fileFingerprints"].is_array()) {
        return false;
    }
    for (const auto& item : manifest["fileFingerprints"]) {
        if (!item.is_object()) {
            continue;
        }
        const std::string path = item.value("path", std::string{});
        if (path.empty()) {
            continue;
        }
        const std::string key = normalizePathForCompare(path);
        if (key.empty()) {
            continue;
        }
        InstalledFileFingerprint fingerprint;
        fingerprint.size = item.value("size", 0ULL);
        fingerprint.contentHash = item.value("contentHash", 0ULL);
        if (fingerprint.contentHash == 0) {
            continue;
        }
        out[key] = fingerprint;
    }
    return !out.empty();
}

}  // namespace MultiThreadedInstaller
