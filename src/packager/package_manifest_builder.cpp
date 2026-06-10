#include "packager/package_manifest_builder.h"

#include "common/utf8_utils.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace MultiThreadedInstaller {
namespace {

std::string FolderNameFromPath(const std::string& path) {
    return Utf8FromPath(PathFromUtf8(path).filename());
}

std::string CurrentYear() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    return std::to_string(1900 + tm.tm_year);
}

std::string ResolveCopyright(const AppConfig& app) {
    if (!app.copyright.empty()) {
        return app.copyright;
    }
    return "Copyright (c) " + CurrentYear() + " " + app.publisher;
}

// 读取 hook 脚本字节并内嵌进 manifest。
PackageHook BuildHook(const HookConfig& cfg, const std::string& configDirectory) {
    PackageHook hook;
    if (!cfg.present) {
        return hook;
    }
    fs::path scriptPath = PathFromUtf8(cfg.path);
    if (!scriptPath.is_absolute()) {
        scriptPath = PathFromUtf8(configDirectory) / scriptPath;
    }
    std::ifstream in(scriptPath, std::ios::binary);
    if (!in) {
        // 校验阶段已确认脚本存在；此处保持 present=false 视作未配置。
        return hook;
    }
    hook.present = true;
    hook.scriptName = Utf8FromPath(scriptPath.filename());
    hook.content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    hook.args = cfg.args;
    hook.onFailure = cfg.onFailure;
    hook.timeoutSec = cfg.timeoutSec;
    return hook;
}

} // namespace

PackageManifest PackageManifestBuilder::build(const std::vector<CompressionResult>& results,
                                              const std::vector<FolderInfo>& folderInfos,
                                              const PackagerConfiguration& config,
                                              const std::string& configDirectory) const {
    PackageManifest manifest;

    manifest.identity.productName = config.app.productName;
    manifest.identity.publisher = config.app.publisher;
    manifest.identity.version = config.app.version;
    manifest.identity.defaultDir = config.app.defaultDir;
    manifest.identity.copyright = ResolveCopyright(config.app);

    uint64_t offset = 0;
    const size_t count = std::min(results.size(), folderInfos.size());
    manifest.payload.folders.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& info = folderInfos[i];
        const auto& result = results[i];

        PackagePayloadFolder folder;
        folder.folderId = info.id;
        folder.folderName = FolderNameFromPath(info.sourcePath);
        folder.source = info.sourcePath;
        folder.target = info.targetPath;
        folder.required = true;
        folder.offset = offset;
        folder.compressedSize = static_cast<uint64_t>(result.compressedSize);
        folder.originalSize = static_cast<uint64_t>(result.originalSize);
        folder.checksum = result.checksum;
        folder.algorithm = result.algorithm;
        folder.framed = result.framed;
        folder.fileIndex = result.fileIndex;
        manifest.payload.totalCompressedSize += folder.compressedSize;
        offset += folder.compressedSize;
        manifest.payload.folders.push_back(std::move(folder));
    }

    // preInstall / postInstall 各支持多个脚本，按声明顺序内嵌（脚本字节随包携带）。
    auto buildHookList = [&](const std::vector<HookConfig>& configs,
                             std::vector<PackageHook>& out) {
        out.reserve(configs.size());
        for (const auto& cfg : configs) {
            PackageHook hook = BuildHook(cfg, configDirectory);
            if (hook.present) {
                out.push_back(std::move(hook));
            }
        }
    };
    buildHookList(config.hooks.preInstall, manifest.hooks.preInstall);
    buildHookList(config.hooks.postInstall, manifest.hooks.postInstall);
    return manifest;
}

} // namespace MultiThreadedInstaller
