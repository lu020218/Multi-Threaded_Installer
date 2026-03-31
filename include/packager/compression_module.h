#pragma once

#include "common/archive_types.h"

#include <memory>

namespace MultiThreadedInstaller {

class FolderPayloadCompressor;

class CompressionModule {
public:
    CompressionModule();
    ~CompressionModule();

    CompressionResult compressFolder(const FolderInfo& folder);

    bool setCompressionAlgorithm(CompressionAlgorithm algorithm);
    bool setCompressionLevel(int level);
    bool setThreadCount(int threadCount);

private:
    CompressionAlgorithm currentAlgorithm;
    int compressionLevel;
    bool compressionLevelExplicitlySet;
    int threadCount;
    std::unique_ptr<FolderPayloadCompressor> payloadCompressor;
};

} // namespace MultiThreadedInstaller
