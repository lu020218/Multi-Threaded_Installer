#pragma once

#include "common/archive_types.h"

namespace MultiThreadedInstaller {

// 本次构建的身份。其余版本资源字段（FileVersion/ProductVersion/CompanyName...）
// 由引擎从 version/publisher 在打包期派生并写入 exe PE 资源，不进 manifest。
struct PackageIdentity {
    std::string productName;
    std::string publisher;
    std::string version;
    std::string defaultDir;
    std::string copyright;   // 可空；缺省时由 publisher + 构建年份派生
};

struct PackagePayloadFolder {
    std::string folderId;
    std::string folderName;
    std::string source;
    std::string target;
    bool required = false;
    uint64_t offset = 0;
    uint64_t compressedSize = 0;
    uint64_t originalSize = 0;
    uint32_t checksum = 0;
    CompressionAlgorithm algorithm = CompressionAlgorithm::LZMA2_XZ;
    bool framed = false;
    std::vector<FileIndexEntry> fileIndex;
};

struct PackagePayloadManifest {
    uint64_t totalCompressedSize = 0;
    std::vector<PackagePayloadFolder> folders;
};

// 一个 pre/post 钩子，脚本内容在打包期内嵌。
struct PackageHook {
    bool present = false;
    std::string scriptName;
    std::vector<uint8_t> content;
    std::string args;
    HookOnFailure onFailure = HookOnFailure::ABORT;
    uint32_t timeoutSec = 300;
};

struct PackageHooks {
    PackageHook preInstall;
    PackageHook postInstall;
};

struct PackageManifest {
    uint32_t version = Constants::VERSION;
    PackageIdentity identity;
    PackagePayloadManifest payload;
    PackageHooks hooks;
};

PackageManifest PackageManifestFromExtendedMetadata(const ExtendedInstallationMetadata& metadata);
ExtendedInstallationMetadata PackageManifestToExtendedMetadata(const PackageManifest& manifest);

} // namespace MultiThreadedInstaller
