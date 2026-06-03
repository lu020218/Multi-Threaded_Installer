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
    void setPerFileFrames(bool enabled) { perFileFrames_ = enabled; }

    CompressionResult compressFolder(const FolderInfo& folder) const;

private:
    CompressionAlgorithm currentAlgorithm;
    int compressionLevel;
    int threadCount;
    bool perFileFrames_ = false;

    CompressionResult compressWithXzLzma2(const FolderInfo& folder) const;
    CompressionResult compressWithZstd(const FolderInfo& folder) const;
    // Per-file framed payload: each file compressed into its own independent
    // frame so the installer can skip decompressing unchanged files (P2).
    CompressionResult compressFolderFramed(const FolderInfo& folder) const;
    uint32_t calculateChecksum(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> createTarData(const FolderInfo& folder,
                                       std::vector<FileIndexEntry>& fileIndex) const;
};

} // namespace MultiThreadedInstaller
