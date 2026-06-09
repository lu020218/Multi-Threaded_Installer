#pragma once

#include "common/archive_types.h"
#include "common/lzma_loader.h"
#include "installer/payload/crc32_stream.h"
#include "installer/payload/stream_sink.h"

namespace MultiThreadedInstaller {

/// 标准 folder 载荷解压器：只处理整 folder 的 XZ/LZMA2 或 ZSTD 流并写入 StreamSink；
/// 已不再支持旧的块级安装格式。算法按载荷的 algorithm 字段分派到 LZMA/ZSTD。
class DecompressionEngine {
public:
    /// 解压/写盘耗时（纳秒，计时诊断用）。
    struct DecompressionTiming {
        long long decompressNs = 0;  ///< 解压耗时。
        long long writeNs = 0;       ///< 落盘耗时。
    };

    /// 解压产出的附加信息。
    struct DecompressionOutcome {
        bool rebootRequired = false;                   ///< 是否有锁定文件待重启替换。
        std::vector<std::string> pendingReplaceFiles;  ///< 待重启替换的文件。
        std::vector<std::string> installedFiles;       ///< 实际写入的文件。
        std::vector<std::string> skippedFiles;         ///< 命中指纹被跳过（零读）的文件。
    };

    DecompressionEngine();
    ~DecompressionEngine();

    /// 解压一个 folder 并落地到其目标路径（内部用文件 sink + tar 解析）。
    bool decompressFolder(const DecompressionTask& task,
                          DecompressionTiming* timing = nullptr,
                          DecompressionOutcome* outcome = nullptr);

    /// 解压一个 folder 并把还原字节写入指定 sink（可选边解压边算校验和）。
    bool decompressToStream(const DecompressionTask& task,
                            StreamSink& sink,
                            Crc32Stream* checksum = nullptr,
                            DecompressionTiming* timing = nullptr,
                            DecompressionOutcome* outcome = nullptr);

    /// 注册进度回调（解压过程中按 folder/文件上报）。
    void registerProgressCallback(ProgressCallback callback);

private:
    ProgressCallback progressCallback_;  ///< 进度回调。
    LzmaLoader lzmaLoader_;               ///< liblzma 动态加载器。

    /// XZ/LZMA2 解压实现。
    bool decompressLzma(const DecompressionTask& task,
                        StreamSink& sink,
                        Crc32Stream* checksum,
                        DecompressionTiming* timing);
    /// ZSTD 解压实现。
    bool decompressZstd(const DecompressionTask& task,
                        StreamSink& sink,
                        Crc32Stream* checksum,
                        DecompressionTiming* timing);
    /// 上报一次进度。
    void reportProgress(const std::string& folderName,
                        const std::string& currentFile,
                        float progress);
};

} // namespace MultiThreadedInstaller
