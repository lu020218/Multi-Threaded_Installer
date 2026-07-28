#pragma once

#include "common/config_types.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace MultiThreadedInstaller {

// Fingerprint of a previously installed file, recorded in install.manifest.json.
// Used by the incremental "zero-read" skip path (Scheme A): the installer can
// compare the new package's per-file hash against the previously installed
// hash without reading file content from disk.
struct InstalledFileFingerprint {
    uint64_t size = 0;
    uint64_t contentHash = 0;
};

// Keyed by normalizePathForCompare(absolute path).
using InstalledFileFingerprintMap = std::unordered_map<std::string, InstalledFileFingerprint>;

struct FileIndexEntry {
    std::string relativePath;
    uint64_t offset;
    uint64_t size;
    // Per-file content fingerprint (FNV-1a 64) used by incremental install to
    // skip rewriting unchanged files. 0 means "no fingerprint available"
    // (older packages), which forces a fail-safe rewrite.
    uint64_t contentHash = 0;
    // Per-file compressed frame location within the folder payload. Only
    // meaningful when the folder is framed (per-file compression); lets the
    // installer seek to and decompress just the changed files (P2).
    uint64_t frameOffset = 0;
    uint64_t frameCompressedSize = 0;
};

struct CompressionResult {
    // A single standard XZ/LZMA2 payload for one folder.
    std::vector<uint8_t> compressedData;
    uint32_t checksum;
    size_t originalSize;
    size_t compressedSize;
    CompressionAlgorithm algorithm;
    // When true, compressedData is a concatenation of independently compressed
    // per-file frames (see FileIndexEntry::frameOffset/frameCompressedSize)
    // rather than a single stream over the whole folder tar.
    bool framed = false;
    // File manifest for logging, validation, and post-install bookkeeping only.
    std::vector<FileIndexEntry> fileIndex;

    CompressionResult()
        : checksum(0),
          originalSize(0),
          compressedSize(0),
          algorithm(CompressionAlgorithm::LZMA2_XZ) {}
};

// ── 载荷（打包产出 / 包内序列化 / 运行期解压 三层共用同一结构体）────────────────

/// 一个待安装目录（folder）的载荷描述：身份 + 在数据包中的位置 + 落点。
struct PackagePayloadFolder {
    std::string folderId;     ///< 唯一标识（取自 --input 顶层子目录名）。
    std::string folderName;   ///< 显示名。
    std::string source;       ///< 打包期源目录（运行期仅日志）。
    std::string target;       ///< 安装目标（支持 %InstallDir% 与环境变量）。
    bool required = false;    ///< 是否必装（单产品单载荷下恒为全装）。
    uint64_t offset = 0;          ///< 在数据区中的字节偏移。
    uint64_t compressedSize = 0;  ///< 压缩后字节数。
    uint64_t originalSize = 0;    ///< 原始字节数。
    uint32_t checksum = 0;        ///< 校验和（落盘后校验；分帧模式恒 0，逐文件 contentHash 兜底）。
    CompressionAlgorithm algorithm = CompressionAlgorithm::LZMA2_XZ;  ///< 压缩算法。
    bool framed = false;          ///< 是否按文件分帧（支持逐文件跳过解压）。
    std::vector<FileIndexEntry> fileIndex;  ///< 文件清单（指纹/分帧定位/账本）。
};

/// 全部载荷的汇总。
struct PackagePayloadManifest {
    uint64_t totalCompressedSize = 0;            ///< 数据区总压缩字节数。
    std::vector<PackagePayloadFolder> folders;   ///< 各 folder 载荷。
};

// HookAuxFile / HookScript / PackageHooks 已收敛到 config_types.h（三层共用）。
// 旧的 FolderMapping/PackagePayloadFolder/InstallationMetadata/PackageManifest
// 已删除：全工程统一消费 package_manifest.h 的 PackageManifest（唯一元数据根）。

struct DecompressionTask {
    std::vector<uint8_t> compressedData;
    std::string folderName;
    std::string targetPath;
    unsigned int schedulerConcurrencyHint;
    uint32_t expectedChecksum;
    size_t originalSize;
    CompressionAlgorithm algorithm;
    // Per-file fingerprints for this folder. When populated, the extractor may
    // skip rewriting files whose on-disk content already matches.
    std::vector<FileIndexEntry> fileIndex;
    // Previously installed file fingerprints (from the old install manifest),
    // enabling the zero-read skip decision (Scheme A). May be null.
    std::shared_ptr<const InstalledFileFingerprintMap> oldInstalledFingerprints;

    DecompressionTask()
        : schedulerConcurrencyHint(1),
          expectedChecksum(0),
          originalSize(0),
          algorithm(CompressionAlgorithm::LZMA2_XZ) {}
};

using ProgressCallback = std::function<void(const std::string&, const std::string&, float)>;

namespace Constants {
    constexpr uint32_t MAGIC_NUMBER = 0x4D544950;
    constexpr uint32_t DATA_MAGIC_NUMBER = 0x4D544450;
    // 27 = 按文件分帧默认开启 + 小文件聚合帧(entry.offset=帧内偏移,多成员共享 frameOffset)。
    // 26 = 身份新增 appName(主 exe 程序名)/appId(产品唯一 id)。安装器只读等于该值的包。
    constexpr uint32_t VERSION = 27;

    constexpr int DEFAULT_LZMA_LEVEL = 9;
    constexpr int DEFAULT_ZSTD_LEVEL = 3;
}

/// 全工程唯一的元数据根：打包器构建它、codec 序列化/反序列化它、
/// 安装器与卸载器直接消费它（不再有平行的运行期元数据结构）。
/// 组成部件三层共用：PackageIdentity/HookScript/PackageHooks 见 config_types.h，
/// PackagePayloadFolder/PackagePayloadManifest/FileIndexEntry 见本文件上方。
struct PackageManifest {
    uint32_t version = Constants::VERSION;  ///< manifest 二进制版本（codec 用，拒读旧包）。
    PackageIdentity identity;               ///< 产品身份。
    PackagePayloadManifest payload;         ///< 载荷。
    PackageHooks hooks;                     ///< 钩子。
};

struct DataPackageHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t metadataOffset;
    uint64_t metadataSize;
    uint64_t dataOffset;
    uint64_t dataSize;

    DataPackageHeader()
        : magic(Constants::DATA_MAGIC_NUMBER),
          version(1),
          metadataOffset(0),
          metadataSize(0),
          dataOffset(0),
          dataSize(0) {}
};

} // namespace MultiThreadedInstaller
