#include "packager/package_manifest_builder.h"

#include "common/utf8_utils.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

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

// 读取单个文件的全部字节。
bool ReadFileBytes(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out.assign(content.begin(), content.end());
    return true;
}

// 内嵌主脚本所在目录里除主脚本外的全部文件（递归，保留相对结构），作为兄弟文件。
// 运行期会与主脚本释放到同一临时目录，使主脚本可直接 call/调用它们。
void EmbedSiblingFiles(const fs::path& scriptPath, HookScript& hook) {
    const fs::path scriptDir = scriptPath.parent_path();
    if (scriptDir.empty()) {
        return;
    }
    std::error_code ec;
    fs::recursive_directory_iterator it(scriptDir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        std::cerr << "WARNING: failed to scan hook sibling directory: "
                  << Utf8FromPath(scriptDir) << " (" << ec.message() << ")" << std::endl;
        return;
    }
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        const fs::path& entryPath = it->path();
        std::error_code statEc;
        if (!fs::is_regular_file(entryPath, statEc)) {
            continue;
        }
        // 跳过主脚本本身（已作为 content 内嵌）。
        std::error_code eqEc;
        if (fs::equivalent(entryPath, scriptPath, eqEc) && !eqEc) {
            continue;
        }
        HookAuxFile aux;
        aux.relativePath = fs::relative(entryPath, scriptDir, statEc).generic_string();
        if (statEc || aux.relativePath.empty()) {
            continue;
        }
        if (!ReadFileBytes(entryPath, aux.content)) {
            std::cerr << "WARNING: failed to read hook sibling file, skipped: "
                      << Utf8FromPath(entryPath) << std::endl;
            continue;
        }
        hook.auxFiles.push_back(std::move(aux));
    }
}

// 读取 hook 脚本字节并内嵌进 manifest（同一 HookScript 对象由声明形态就地长成内嵌形态）。
HookScript BuildHook(const HookScript& cfg, const std::string& configDirectory) {
    HookScript hook;
    if (!cfg.present) {
        return hook;
    }
    fs::path scriptPath = PathFromUtf8(cfg.sourcePath);
    if (!scriptPath.is_absolute()) {
        scriptPath = PathFromUtf8(configDirectory) / scriptPath;
    }
    std::vector<uint8_t> content;
    if (!ReadFileBytes(scriptPath, content)) {
        // 校验阶段已确认脚本存在；此处保持 present=false 视作未配置。
        return hook;
    }
    if (content.empty()) {
        // 空脚本（0 字节占位）视作未配置该 hook：present=false。
        // 否则会得到 present=true + content 为空的非法 manifest，导致安装器
        // 运行期校验失败（"hook is marked present but carries no script content"），
        // 表现为安装器双击/静默都启动失败、弹空白错误框。
        std::cerr << "WARNING: hook script is empty, treated as no hook: "
                  << Utf8FromPath(scriptPath) << std::endl;
        return hook;
    }
    hook = cfg;  // 声明字段(sourcePath/args/onFailure/timeoutSec/keep/keepDir)整体沿用
    hook.present = true;
    hook.scriptName = Utf8FromPath(scriptPath.filename());
    hook.content = std::move(content);
    // 内嵌主脚本同目录的兄弟文件，使运行期可调用同目录脚本。
    EmbedSiblingFiles(scriptPath, hook);
    return hook;
}

} // namespace

PackageManifest PackageManifestBuilder::build(const std::vector<CompressionResult>& results,
                                              const std::vector<FolderInfo>& folderInfos,
                                              const PackagerConfiguration& config,
                                              const std::string& configDirectory) const {
    PackageManifest manifest;

    manifest.identity.productName = config.app.productName;
    manifest.identity.appName = config.app.appName;
    manifest.identity.appId = config.app.appId;
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
    auto buildHookList = [&](const std::vector<HookScript>& configs,
                             std::vector<HookScript>& out) {
        out.reserve(configs.size());
        for (const auto& cfg : configs) {
            HookScript hook = BuildHook(cfg, configDirectory);
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
