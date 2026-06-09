#pragma once

#include "common/archive_types.h"

#include <memory>

namespace MultiThreadedInstaller {

class FolderPayloadCompressor;

/// 打包期压缩模块：把一个 FolderInfo（目录文件集）打成 tar 并压缩成单个载荷流。
/// 算法/级别/线程数可配置，委托 FolderPayloadCompressor 实际执行。
class CompressionModule {
public:
    CompressionModule();
    ~CompressionModule();

    /// 压缩一个目录，返回压缩结果（压缩字节、校验和、原始/压缩大小、文件索引等）。
    CompressionResult compressFolder(const FolderInfo& folder);

    bool setCompressionAlgorithm(CompressionAlgorithm algorithm);  ///< 设置算法（xz/zstd/none）。
    bool setCompressionLevel(int level);   ///< 设置压缩级别；-1 用算法默认。
    bool setThreadCount(int threadCount);  ///< 设置压缩线程数；0 按 CPU 自动。
    void setPerFileFrames(bool enabled);   ///< 是否按文件分帧（支持运行期逐文件跳过解压）。

private:
    CompressionAlgorithm currentAlgorithm;     ///< 当前算法。
    int compressionLevel;                      ///< 当前级别。
    bool compressionLevelExplicitlySet;        ///< 级别是否被显式设置。
    int threadCount;                           ///< 线程数。
    std::unique_ptr<FolderPayloadCompressor> payloadCompressor;  ///< 实际压缩器。
};

} // namespace MultiThreadedInstaller
