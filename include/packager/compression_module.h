#pragma once

#include "common/types.h"

#ifdef LibLZMA_FOUND
#include <lzma.h>
#endif
#ifdef ZSTD_FOUND
#include <zstd.h>
#endif

namespace MultiThreadedInstaller {

class CompressionModule {
public:
    CompressionModule();
    ~CompressionModule();
    

    CompressionResult compressFolder(const FolderInfo& folder);
    

    bool setCompressionAlgorithm(CompressionAlgorithm algorithm);
    

    bool setCompressionLevel(int level);
    
    bool setThreadCount(int threadCount);
    

    bool setBlockSize(size_t blockSize = Constants::DEFAULT_BLOCK_SIZE);
    
private:
    CompressionAlgorithm currentAlgorithm;
    int compressionLevel;
    bool compressionLevelExplicitlySet;
    int threadCount;
    size_t blockSize;
    

#ifdef LibLZMA_FOUND
    lzma_stream lzmaStream;
    bool lzmaInitialized;
    bool lzmaSupportsMt;
#else
    void* lzmaStream; // Stub
    bool lzmaInitialized;
#endif
    

    CompressionResult compressWithLzma(const FolderInfo& folder);

    CompressionResult compressWithZstd(const FolderInfo& folder);
    

    uint32_t calculateChecksum(const std::vector<uint8_t>& data);
    

    std::vector<uint8_t> createTarData(const FolderInfo& folder,
                                       std::vector<FileIndexEntry>& fileIndex);
    

    std::vector<uint8_t> compressWithBlocksLzma(const std::vector<uint8_t>& data);
    
};

} // namespace MultiThreadedInstaller
