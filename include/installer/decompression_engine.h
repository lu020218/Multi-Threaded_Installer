#pragma once

#include "common/archive_types.h"
#include "installer/thread_pool_manager.h"
#include "installer/stream_sink.h"
#include "installer/crc32_stream.h"

#include <memory>

namespace MultiThreadedInstaller {

class DecompressionEngine {
public:
    DecompressionEngine();
    ~DecompressionEngine();
    

    struct LegacyStageTiming {
        long long decompressNs = 0;
        long long writeNs = 0;
    };
    
    bool decompressFolder(const DecompressionTask& task, LegacyStageTiming* timing = nullptr);
    

    bool decompressToStream(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum,
                            LegacyStageTiming* timing);
    

    bool decompressLzmaBlockData(const std::vector<uint8_t>& compressedData,
                                 size_t originalSize,
                                 std::vector<uint8_t>& output);
    

    void setThreadPool(std::shared_ptr<ThreadPoolManager> threadPool);
    

    void registerProgressCallback(ProgressCallback callback);
    
private:
    std::shared_ptr<ThreadPoolManager> threadPool;
    ProgressCallback progressCallback;
    

    bool decompressLzma(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum,
                        LegacyStageTiming* timing);

    bool decompressZstd(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum,
                        LegacyStageTiming* timing);
    

    bool decompressLzmaBlocks(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum,
                              LegacyStageTiming* timing);
    
    

    void reportProgress(const std::string& folderName, const std::string& currentFile, float progress);
};

} // namespace MultiThreadedInstaller
