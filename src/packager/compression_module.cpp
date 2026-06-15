#include "packager/compression_module.h"

#include "packager/folder_payload_compressor.h"

#include <chrono>
#include <iostream>

namespace MultiThreadedInstaller {

namespace {

const char* CompressionAlgorithmName(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::LZMA2_XZ:
            return "XZ/LZMA2";
        case CompressionAlgorithm::ZSTD:
            return "ZSTD";
        default:
            return "Unknown";
    }
}

int GetDefaultCompressionLevel(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::ZSTD:
            return Constants::DEFAULT_ZSTD_LEVEL;
        case CompressionAlgorithm::LZMA2_XZ:
        default:
            return Constants::DEFAULT_LZMA_LEVEL;
    }
}

} // namespace

CompressionModule::CompressionModule()
    : currentAlgorithm(CompressionAlgorithm::LZMA2_XZ),
      compressionLevel(Constants::DEFAULT_LZMA_LEVEL),
      compressionLevelExplicitlySet(false),
      threadCount(0),
      payloadCompressor(std::make_unique<FolderPayloadCompressor>()) {}

CompressionModule::~CompressionModule() = default;

CompressionResult CompressionModule::compressFolder(const FolderInfo& folder) {
    const char* algorithmName = CompressionAlgorithmName(currentAlgorithm);
    const auto startTime = std::chrono::steady_clock::now();

    std::cout << "[Packager][Compress] folder=" << folder.sourcePath
              << " algorithm=" << algorithmName
              << " level=" << compressionLevel
              << " requestedThreads=" << threadCount
              << std::endl;

    CompressionResult result = payloadCompressor->compressFolder(folder);

    const auto endTime = std::chrono::steady_clock::now();
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    if (result.compressedSize > 0) {
        const double compressionRatio =
            static_cast<double>(result.compressedSize) / static_cast<double>(result.originalSize);
        const double savedSpace = (1.0 - compressionRatio) * 100.0;
        std::cout << "[Packager][Compress] folder=" << folder.sourcePath
                  << " completed"
                  << " algorithm=" << algorithmName
                  << " level=" << compressionLevel
                  << " requestedThreads=" << threadCount
                  << " originalSize=" << result.originalSize
                  << " compressedSize=" << result.compressedSize
                  << " ratio=" << compressionRatio
                  << " savedPercent=" << savedSpace
                  << " durationMs=" << duration.count()
                  << std::endl;
    } else {
        std::cerr << "[Packager][Compress] folder=" << folder.sourcePath
                  << " failed"
                  << " algorithm=" << algorithmName
                  << " level=" << compressionLevel
                  << " requestedThreads=" << threadCount
                  << " durationMs=" << duration.count()
                  << std::endl;
    }

    return result;
}

bool CompressionModule::setCompressionAlgorithm(CompressionAlgorithm algorithm) {
    const char* oldAlgorithm = CompressionAlgorithmName(currentAlgorithm);
    const char* newAlgorithm = CompressionAlgorithmName(algorithm);
    (void)oldAlgorithm;
    (void)newAlgorithm;

#ifndef ZSTD_FOUND
    if (algorithm == CompressionAlgorithm::ZSTD) {
        std::cerr << "ZSTD support not compiled in; requested algorithm ignored" << std::endl;
        return false;
    }
#endif

    if (algorithm != CompressionAlgorithm::LZMA2_XZ &&
        algorithm != CompressionAlgorithm::ZSTD) {
        return false;
    }

    if (!payloadCompressor->setCompressionAlgorithm(algorithm)) {
        return false;
    }

    currentAlgorithm = algorithm;
    if (!compressionLevelExplicitlySet) {
        compressionLevel = GetDefaultCompressionLevel(currentAlgorithm);
        if (!payloadCompressor->setCompressionLevel(compressionLevel)) {
            return false;
        }
    }
    return true;
}

bool CompressionModule::setCompressionLevel(int level) {
    if (!payloadCompressor->setCompressionLevel(level)) {
        return false;
    }

    compressionLevel = level;
    compressionLevelExplicitlySet = true;
    return true;
}

bool CompressionModule::setThreadCount(int requestedThreadCount) {
    if (!payloadCompressor->setThreadCount(requestedThreadCount)) {
        return false;
    }
    threadCount = requestedThreadCount;
    return true;
}

void CompressionModule::setPerFileFrames(bool enabled) {
    payloadCompressor->setPerFileFrames(enabled);
}

void CompressionModule::setBlockSizeBytes(uint64_t bytes) {
    payloadCompressor->setBlockSizeBytes(bytes);
}

} // namespace MultiThreadedInstaller
