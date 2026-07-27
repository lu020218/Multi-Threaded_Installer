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

// 与主钩子脚本同目录的「兄弟文件」（脚本/数据等），随包内嵌，运行期与主脚本释放到
// 同一临时目录，使主脚本可 `call common.bat`、`.\sub\helper.ps1` 或读取同目录数据文件。
struct HookAuxFile {
    std::string relativePath;        // 相对主脚本所在目录（generic '/'），如 "common.bat"、"sub/helper.ps1"
    std::vector<uint8_t> content;    // 打包期读入内嵌的文件字节
};

// 一个内嵌的 pre/post 钩子脚本，随安装器一同打包，运行期临时释放后执行。
struct HookScript {
    bool present = false;
    std::string scriptName;          // 仅日志用，如 pre_install.bat
    std::vector<uint8_t> content;    // 打包期读入内嵌的脚本字节
    std::string args;                // 本次构建特有参数
    uint8_t onFailure = 0;           // 0 = abort（中止回滚），1 = continue（记日志继续）
    uint32_t timeoutSec = 300;       // 超时上限（秒）
    // 主脚本所在目录内的其余文件（递归内嵌），运行期与主脚本释放到同一临时目录。
    std::vector<HookAuxFile> auxFiles;
    // 执行后保留：true 时把主脚本+兄弟文件拷贝到 keepDir（无论脚本成功失败都拷贝）。
    bool keep = false;
    // 保留目标目录，支持 %INSTALL_DIR% / %VERSION% 与系统环境变量（如 %INSTALL_DIR%\scripts）。
    std::string keepDir;
};

// 构建期 → 运行期唯一的桥，收窄为：身份 + 载荷 + 钩子。
// 其余安装行为默认值、清理规则、跨版本兼容等全部写死/实现在引擎内。
struct ExtendedInstallationMetadata : public InstallationMetadata {
    // 身份（其余版本资源字段在打包期已写入 exe PE 资源，运行期无需携带）。
    std::string appProductName;
    std::string appName;    ///< 主 exe 程序名（不含 .exe）；为空时回退 appProductName。
    std::string appId;      ///< 产品唯一 id（如 com.comp.myapp）；落 install.manifest.json。
    std::string appPublisher;
    std::string appVersion;
    std::string appDefaultDir;

    // 载荷。
    std::vector<ExtendedFolderMapping> extendedPayloadMappings;

    // 钩子：每个钩子点可挂多个脚本，按顺序依次执行（支持 bat/cmd/ps1）。
    std::vector<HookScript> preInstall;
    std::vector<HookScript> postInstall;

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
    // 27 = 按文件分帧默认开启 + 小文件聚合帧(entry.offset=帧内偏移,多成员共享 frameOffset)。
    // 26 = 身份新增 appName(主 exe 程序名)/appId(产品唯一 id)。安装器只读等于该值的包。
    constexpr uint32_t VERSION = 27;

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
