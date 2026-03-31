#pragma once

#include "common/archive_types.h"

namespace MultiThreadedInstaller {

class FolderPayloadCompressor {
public:
    FolderPayloadCompressor();
    ~FolderPayloadCompressor() = default;

    bool setCompressionAlgorithm(CompressionAlgorithm algorithm);
    bool setCompressionLevel(int level);
    bool setThreadCount(int threadCount);

    CompressionResult compressFolder(const FolderInfo& folder) const;

private:
    CompressionAlgorithm currentAlgorithm;
    int compressionLevel;
    int threadCount;

    CompressionResult compressWithXzLzma2(const FolderInfo& folder) const;
    CompressionResult compressWithZstd(const FolderInfo& folder) const;
    uint32_t calculateChecksum(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> createTarData(const FolderInfo& folder,
                                       std::vector<FileIndexEntry>& fileIndex) const;
};

} // namespace MultiThreadedInstaller
