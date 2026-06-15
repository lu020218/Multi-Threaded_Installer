#pragma once

#include "common/archive_types.h"

namespace MultiThreadedInstaller {

/// 目录载荷压缩器：把目录打成 tar 后用 XZ/ZSTD 压成单个载荷流（或按文件分帧）。
class FolderPayloadCompressor {
public:
    FolderPayloadCompressor();
    ~FolderPayloadCompressor() = default;

    bool setCompressionAlgorithm(CompressionAlgorithm algorithm);  ///< 设置算法。
    bool setCompressionLevel(int level);                           ///< 设置级别。
    bool setThreadCount(int threadCount);                          ///< 设置线程数。
    void setPerFileFrames(bool enabled) { perFileFrames_ = enabled; }  ///< 是否按文件分帧。
    /// 设置 XZ 多线程分块大小（字节）；0=自动(按 tar 大小对齐解码并行度,块更大、压缩比更高)。
    void setBlockSizeBytes(uint64_t bytes) { blockSizeBytes_ = bytes; }

    /// 压缩一个目录，返回压缩结果（含 fileIndex）。
    CompressionResult compressFolder(const FolderInfo& folder) const;

private:
    CompressionAlgorithm currentAlgorithm;  ///< 算法。
    int compressionLevel;                   ///< 级别。
    int threadCount;                        ///< 线程数。
    bool perFileFrames_ = false;            ///< 是否分帧。
    uint64_t blockSizeBytes_ = 0;           ///< XZ 分块大小(字节)；0=自动。

    CompressionResult compressWithXzLzma2(const FolderInfo& folder) const;  ///< 整流 XZ/LZMA2 压缩。
    CompressionResult compressWithZstd(const FolderInfo& folder) const;     ///< 整流 ZSTD 压缩。
    /// 按文件分帧压缩：每个文件独立成帧，使安装器可跳过未变文件的解压（P2 增量优化）。
    CompressionResult compressFolderFramed(const FolderInfo& folder) const;
    uint32_t calculateChecksum(const std::vector<uint8_t>& data) const;  ///< 计算校验和。
    /// 把目录内文件打成 tar 字节流，同时产出 fileIndex（各文件偏移/大小/哈希）。
    std::vector<uint8_t> createTarData(const FolderInfo& folder,
                                       std::vector<FileIndexEntry>& fileIndex) const;
};

} // namespace MultiThreadedInstaller
