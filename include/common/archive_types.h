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

struct FolderMapping {
    std::string folderId;
    std::string folderName;
    std::string targetPath;
    // Payload location and size in the package data area.
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint32_t checksum;
    CompressionAlgorithm algorithm;

    FolderMapping()
        : offset(0),
          compressedSize(0),
          originalSize(0),
          checksum(0),
          algorithm(CompressionAlgorithm::LZMA2_XZ) {}
};

struct ExtendedFolderMapping : public FolderMapping {
    std::string target;
    // When true the payload is per-file framed (see CompressionResult::framed).
    bool framed = false;
    // File manifest for logging, validation, and post-install bookkeeping only.
    std::vector<FileIndexEntry> fileIndex;

    ExtendedFolderMapping()
        : FolderMapping() {}
};

struct InstallationMetadata {
    uint32_t version;
    uint32_t folderCount;
    std::vector<FolderMapping> payloadMappings;
    uint64_t totalPayloadCompressedSize;

    InstallationMetadata() : version(1), folderCount(0), totalPayloadCompressedSize(0) {}
};

// 一个内嵌的 pre/post 钩子脚本，随安装器一同打包，运行期临时释放后执行。
struct HookScript {
    bool present = false;
    std::string scriptName;          // 仅日志用，如 pre_install.bat
    std::vector<uint8_t> content;    // 打包期读入内嵌的脚本字节
    std::string args;                // 本次构建特有参数
    uint8_t onFailure = 0;           // 0 = abort（中止回滚），1 = continue（记日志继续）
    uint32_t timeoutSec = 300;       // 超时上限（秒）
};

// 构建期 → 运行期唯一的桥，收窄为：身份 + 载荷 + 钩子。
// 其余安装行为默认值、清理规则、跨版本兼容等全部写死/实现在引擎内。
struct ExtendedInstallationMetadata : public InstallationMetadata {
    // 身份（其余版本资源字段在打包期已写入 exe PE 资源，运行期无需携带）。
    std::string appProductName;
    std::string appPublisher;
    std::string appVersion;
    std::string appDefaultDir;

    // 载荷。
    std::vector<ExtendedFolderMapping> extendedPayloadMappings;

    // 钩子。
    HookScript preInstall;
    HookScript postInstall;

    ExtendedInstallationMetadata()
        : InstallationMetadata(),
          appProductName("MyApplication"),
          appPublisher(""),
          appVersion("1.0"),
          appDefaultDir("%ProgramFiles%") {}
};

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
    // 24 = 重构后的精简 manifest（identity + payload + hooks）。安装器拒绝读 < 24 的旧包。
    constexpr uint32_t VERSION = 24;

    constexpr int DEFAULT_LZMA_LEVEL = 9;
    constexpr int DEFAULT_ZSTD_LEVEL = 3;
}

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
