#pragma once

#include "common/types.h"
#include "installer/thread_pool_manager.h"

// Conditional includes based on availability
#ifdef ZSTD_FOUND
#include <zstd.h>
#endif

#ifdef LibLZMA_FOUND
#include <lzma.h>
#endif

#include <memory>

namespace MultiThreadedInstaller {

class DecompressionEngine {
public:
    DecompressionEngine();
    ~DecompressionEngine();
    
    // 解压文件夹
    bool decompressFolder(const DecompressionTask& task);
    
    // 设置线程池
    void setThreadPool(std::shared_ptr<ThreadPoolManager> threadPool);
    
    // 注册进度回调
    void registerProgressCallback(ProgressCallback callback);
    
private:
    // Zstandard解压上下文
#ifdef ZSTD_FOUND
    ZSTD_DCtx* zstdContext;
#else
    void* zstdContext; // Stub
#endif
    
    // LZMA解压器
#ifdef LibLZMA_FOUND
    lzma_stream lzmaStream;
    bool lzmaInitialized;
#else
    void* lzmaDecoder; // Stub
#endif
    
    std::shared_ptr<ThreadPoolManager> threadPool;
    ProgressCallback progressCallback;
    
    // Zstandard解压实现
    bool decompressZstd(const DecompressionTask& task);
    
    // Zstandard块级解压实现
    bool decompressZstdBlocks(const DecompressionTask& task);
    
    // LZMA解压实现
    bool decompressLzma(const DecompressionTask& task);
    
    // LZMA块级解压实现
    bool decompressLzmaBlocks(const DecompressionTask& task);
    
    // 验证校验和
    bool verifyChecksum(const std::vector<uint8_t>& data, uint32_t expectedChecksum);
    
    // 计算校验和（并行）
    uint32_t calculateChecksum(const std::vector<uint8_t>& data);
    
    // 计算校验和（单线程）
    uint32_t calculateChecksumSingle(const std::vector<uint8_t>& data);
    
    // 流式ZSTD解压（支持多线程）
    size_t decompressZstdStreaming(const std::vector<uint8_t>& compressedData, 
                                   std::vector<uint8_t>& decompressedData);
    
    // 从tar格式数据提取文件
    bool extractTarData(const std::vector<uint8_t>& tarData, const std::string& targetPath);
    
    // 报告进度
    void reportProgress(const std::string& folderName, float progress);
};

} // namespace MultiThreadedInstaller