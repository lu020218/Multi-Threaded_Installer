#pragma once

#include "common/types.h"
#include "installer/thread_pool_manager.h"
#include "installer/stream_sink.h"
#include "installer/crc32_stream.h"

#include <memory>

namespace MultiThreadedInstaller {

class DecompressionEngine {
public:
    DecompressionEngine();
    ~DecompressionEngine();
    
    // 解压文件夹
    struct LegacyStageTiming {
        long long decompressNs = 0;
        long long writeNs = 0;
    };
    
    bool decompressFolder(const DecompressionTask& task, LegacyStageTiming* timing = nullptr);
    
    // 流式解压到输出接口
    bool decompressToStream(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum,
                            LegacyStageTiming* timing);
    
    // 解压单个 LZMA 块
    bool decompressLzmaBlockData(const std::vector<uint8_t>& compressedData,
                                 size_t originalSize,
                                 std::vector<uint8_t>& output);
    
    // 设置线程池
    void setThreadPool(std::shared_ptr<ThreadPoolManager> threadPool);
    
    // 注册进度回调
    void registerProgressCallback(ProgressCallback callback);
    
private:
    std::shared_ptr<ThreadPoolManager> threadPool;
    ProgressCallback progressCallback;
    
    // LZMA解压实现（流式）
    bool decompressLzma(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum,
                        LegacyStageTiming* timing);
    
    // LZMA块级解压实现（流式）
    bool decompressLzmaBlocks(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum,
                              LegacyStageTiming* timing);
    
    
    // 报告进度
    void reportProgress(const std::string& folderName, const std::string& currentFile, float progress);
};

} // namespace MultiThreadedInstaller
