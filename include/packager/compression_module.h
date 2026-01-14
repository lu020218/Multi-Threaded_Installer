#pragma once

#include "common/types.h"
#include "common/lzma_loader.h"
#include <memory>

// Conditional includes based on availability
#ifdef ZSTD_FOUND
#include <zstd.h>
#endif

#ifdef LibLZMA_FOUND
#include <lzma.h>
#endif

namespace MultiThreadedInstaller {

class CompressionModule {
public:
    CompressionModule();
    ~CompressionModule();
    
    // 压缩文件夹
    CompressionResult compressFolder(const FolderInfo& folder);
    
    // 设置压缩算法
    bool setCompressionAlgorithm(CompressionAlgorithm algorithm);
    
    // 设置压缩级别
    bool setCompressionLevel(int level);
    
    // 设置块大小（仅适用于zstd）
    bool setBlockSize(size_t blockSize = Constants::DEFAULT_BLOCK_SIZE);
    
private:
    CompressionAlgorithm currentAlgorithm;
    int compressionLevel;
    size_t blockSize;
    
    // Zstandard相关
#ifdef ZSTD_FOUND
    ZSTD_CCtx* zstdContext;
#else
    void* zstdContext; // Stub
#endif
    
    // LZMA相关
#ifdef LibLZMA_FOUND
    lzma_stream lzmaStream;
    bool lzmaInitialized;
    std::unique_ptr<LzmaLoader> lzmaLoader;
#else
    void* lzmaStream; // Stub
    bool lzmaInitialized;
#endif
    
    // Zstandard压缩实现
    CompressionResult compressWithZstd(const FolderInfo& folder);
    
    // LZMA压缩实现
    CompressionResult compressWithLzma(const FolderInfo& folder);
    
    // 计算校验和
    uint32_t calculateChecksum(const std::vector<uint8_t>& data);
    
    // 读取文件内容
    std::vector<uint8_t> readFileContent(const std::string& filePath);
    
    // 创建文件夹的tar格式数据
    std::vector<uint8_t> createTarData(const FolderInfo& folder);
    
    // 块级压缩实现 (ZSTD)
    std::vector<uint8_t> compressWithBlocks(const std::vector<uint8_t>& data);
    
    // 块级压缩实现 (LZMA)
    std::vector<uint8_t> compressWithBlocksLzma(const std::vector<uint8_t>& data);
    
    // SHA-256校验和计算
    std::vector<uint8_t> calculateSHA256(const std::vector<uint8_t>& data);
};

} // namespace MultiThreadedInstaller