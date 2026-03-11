#include "packager/compression_module.h"
#include "common/utf8_utils.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <thread>
#include <algorithm>
#include <chrono>
#include <array>
#include <atomic>

// Stub macros for disabled logging
#define START_TIMER(name) 0
#define START_PROGRESS(name, total) 0
#define UPDATE_PROGRESS(id, completed, current) do {} while(0)
#define COMPLETE_PROGRESS(id) do {} while(0)
#define LOG_WARNINGF(module, format, ...) do {} while(0)

namespace MultiThreadedInstaller {

namespace {

const char* CompressionAlgorithmName(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::LZMA_HIGH:
            return "LZMA";
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
        case CompressionAlgorithm::LZMA_HIGH:
        default:
            return Constants::DEFAULT_LZMA_LEVEL;
    }
}

const std::array<uint32_t, 256>& GetCrc32Table() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < values.size(); ++i) {
            uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
            }
            values[i] = crc;
        }
        return values;
    }();
    return table;
}

unsigned int ResolveCompressionThreads(int configuredThreadCount) {
    if (configuredThreadCount > 0) {
        return static_cast<unsigned int>(configuredThreadCount);
    }
    unsigned int hwThreads = std::thread::hardware_concurrency();
    return hwThreads == 0 ? 1u : hwThreads;
}

} // namespace

CompressionModule::CompressionModule() 
    : currentAlgorithm(CompressionAlgorithm::LZMA_HIGH)
    , compressionLevel(Constants::DEFAULT_LZMA_LEVEL)
    , compressionLevelExplicitlySet(false)
    , threadCount(0)
    , blockSize(Constants::DEFAULT_BLOCK_SIZE)
    , lzmaInitialized(false)
    , lzmaSupportsMt(false) {
    
#ifdef LibLZMA_FOUND
#if LZMA_VERSION >= 500200
    lzmaSupportsMt = true;
#else
    lzmaSupportsMt = false;
#endif
    lzmaStream = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_easy_encoder(&lzmaStream, Constants::DEFAULT_LZMA_LEVEL, LZMA_CHECK_SHA256);
    if (ret == LZMA_OK) {
        lzmaInitialized = true;
        std::cout << "LZMA encoder initialized successfully" << std::endl;
    } else {
        std::cerr << "Failed to initialize LZMA encoder: " << ret << std::endl;
    }
#else
    std::cerr << "LZMA not available - using stub implementation" << std::endl;
#endif
}

CompressionModule::~CompressionModule() {
#ifdef LibLZMA_FOUND
    if (lzmaInitialized) {
        lzma_end(&lzmaStream);
    }
#endif
}

CompressionResult CompressionModule::compressFolder(const FolderInfo& folder) {
    // Performance tracking disabled
    const char* algorithmName = CompressionAlgorithmName(currentAlgorithm);
    
    auto startTime = std::chrono::steady_clock::now();
    CompressionResult result;

    if (currentAlgorithm == CompressionAlgorithm::ZSTD) {
        result = compressWithZstd(folder);
    } else {
        result = compressWithLzma(folder);
    }
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    if (result.compressedSize > 0) {
        double compressionRatio = static_cast<double>(result.compressedSize) / result.originalSize;
        double savedSpace = (1.0 - compressionRatio) * 100.0;
    

        // Performance tracking disabled
    } else {
        // Logging disabled
    }
    
    return result;
}

bool CompressionModule::setCompressionAlgorithm(CompressionAlgorithm algorithm) {
    const char* oldAlgorithm = CompressionAlgorithmName(currentAlgorithm);
    const char* newAlgorithm = CompressionAlgorithmName(algorithm);
    
    // Logging disabled

    if (algorithm == CompressionAlgorithm::ZSTD) {
#ifdef ZSTD_FOUND
        currentAlgorithm = CompressionAlgorithm::ZSTD;
#else
        std::cerr << "ZSTD support not compiled in; requested algorithm ignored" << std::endl;
        return false;
#endif
    } else if (algorithm == CompressionAlgorithm::LZMA_HIGH) {
        currentAlgorithm = CompressionAlgorithm::LZMA_HIGH;
    } else {
        return false;
    }

    if (!compressionLevelExplicitlySet) {
        compressionLevel = GetDefaultCompressionLevel(currentAlgorithm);
    }
    return true;
}

bool CompressionModule::setCompressionLevel(int level) {
    if (currentAlgorithm == CompressionAlgorithm::LZMA_HIGH) {
        if (level < 0 || level > 9) {
            return false;
        }
    } else if (currentAlgorithm == CompressionAlgorithm::ZSTD) {
        if (level < 1 || level > 22) {
            return false;
        }
    }

    compressionLevel = level;
    compressionLevelExplicitlySet = true;
    return true;
}

bool CompressionModule::setThreadCount(int threadCount) {
    if (threadCount == 0 || threadCount < -1) {
        return false;
    }
    this->threadCount = threadCount;
    return true;
}

bool CompressionModule::setBlockSize(size_t blockSize) {
    this->blockSize = blockSize;
    return true;
}

CompressionResult CompressionModule::compressWithLzma(const FolderInfo& folder) {
    CompressionResult result;
    result.algorithm = CompressionAlgorithm::LZMA_HIGH;
    
#ifdef LibLZMA_FOUND
    if (!lzmaInitialized) {
        std::cerr << "LZMA encoder not initialized or library not loaded" << std::endl;
        return result;
    }
    

    // Logging disabled
    auto tarTimer = START_TIMER("CreateTarData");
    std::vector<FileIndexEntry> fileIndex;
    std::vector<uint8_t> tarData = createTarData(folder, fileIndex);
    if (tarData.empty()) {
        std::cerr << "Failed to create tar data for folder: " << folder.sourcePath << std::endl;
        return result;
    }
    
    result.originalSize = tarData.size();
    result.fileIndex = std::move(fileIndex);
    

    // Logging disabled
    auto compressionTimer = START_TIMER("LzmaCompression");
    
    result.compressedData = compressWithBlocksLzma(tarData);
    
    if (result.compressedData.empty()) {
        std::cerr << "Block LZMA compression failed" << std::endl;
        return CompressionResult{};
    }
    
    result.compressedSize = result.compressedData.size();
    

    if (result.compressedData.size() >= sizeof(uint32_t)) {
        uint32_t blockCount = *reinterpret_cast<const uint32_t*>(result.compressedData.data());
        size_t metaOffset = sizeof(uint32_t);
        size_t metaSize = static_cast<size_t>(blockCount) * 16;
        if (blockCount > 0 && metaOffset + metaSize <= result.compressedData.size()) {
            result.blockIndex.reserve(blockCount);
            for (uint32_t i = 0; i < blockCount; ++i) {
                size_t base = metaOffset + i * 16;
                BlockIndexEntry entry;
                entry.blockId = i;
                entry.offset = *reinterpret_cast<const uint32_t*>(result.compressedData.data() + base);
                entry.compressedSize = *reinterpret_cast<const uint32_t*>(result.compressedData.data() + base + 4);
                entry.originalSize = *reinterpret_cast<const uint32_t*>(result.compressedData.data() + base + 8);
                entry.checksum = *reinterpret_cast<const uint32_t*>(result.compressedData.data() + base + 12);
                result.blockIndex.push_back(entry);
            }
        }
    }
    

    result.checksum = calculateChecksum(tarData);
#else
    // Stub implementation - just copy data with minimal "compression"
    std::vector<FileIndexEntry> fileIndex;
    std::vector<uint8_t> tarData = createTarData(folder, fileIndex);
    if (tarData.empty()) {
        std::cerr << "Failed to create tar data for folder: " << folder.sourcePath << std::endl;
        return result;
    }
    
    result.originalSize = tarData.size();
    result.fileIndex = std::move(fileIndex);
    result.compressedData = tarData; // No actual compression
    result.compressedSize = tarData.size();
    
    // Stub checksum uses CRC32 for consistency
    result.checksum = calculateChecksum(result.compressedData);
    
    std::cout << "Using stub LZMA implementation (no actual compression)" << std::endl;
#endif
    
    return result;
}

CompressionResult CompressionModule::compressWithZstd(const FolderInfo& folder) {
    CompressionResult result;
    result.algorithm = CompressionAlgorithm::ZSTD;

#ifdef ZSTD_FOUND
    std::vector<FileIndexEntry> fileIndex;
    std::vector<uint8_t> tarData = createTarData(folder, fileIndex);
    if (tarData.empty()) {
        std::cerr << "Failed to create tar data for folder: " << folder.sourcePath << std::endl;
        return result;
    }

    result.originalSize = tarData.size();
    result.fileIndex = std::move(fileIndex);

    const size_t bound = ZSTD_compressBound(tarData.size());
    if (bound == 0) {
        std::cerr << "ZSTD failed to calculate compression bound" << std::endl;
        return result;
    }

    result.compressedData.resize(bound);
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (cctx == nullptr) {
        std::cerr << "ZSTD context allocation failed" << std::endl;
        result.compressedData.clear();
        return result;
    }

    const size_t compressedSize = ZSTD_compressCCtx(
        cctx,
        result.compressedData.data(),
        result.compressedData.size(),
        tarData.data(),
        tarData.size(),
        compressionLevel);

    ZSTD_freeCCtx(cctx);

    if (ZSTD_isError(compressedSize)) {
        std::cerr << "ZSTD compression failed: " << ZSTD_getErrorName(compressedSize) << std::endl;
        result.compressedData.clear();
        return result;
    }

    result.compressedData.resize(compressedSize);
    result.compressedSize = compressedSize;
    result.checksum = calculateChecksum(tarData);
    result.blockIndex.clear();
    return result;
#else
    std::cerr << "ZSTD support not compiled in" << std::endl;
    return result;
#endif
}

uint32_t CompressionModule::calculateChecksum(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xFFFFFFFF;
    const auto& table = GetCrc32Table();

    for (uint8_t byte : data) {
        crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFFu];
    }

    return ~crc;
}

std::vector<uint8_t> CompressionModule::createTarData(const FolderInfo& folder,
                                                      std::vector<FileIndexEntry>& fileIndex) {
    std::vector<uint8_t> tarData;
    fileIndex.clear();
    fileIndex.reserve(folder.files.size());
    tarData.reserve(folder.totalSize + folder.files.size() * (sizeof(uint32_t) * 2 + 64));
    size_t processedFiles = 0;
    const size_t sourcePrefixLength = folder.sourcePath.size();
    
    auto progressId = START_PROGRESS("Building TAR payload", folder.files.size());
    
    for (const auto& filePath : folder.files) {
        std::error_code sizeError;
        uint64_t fileSize64 = std::filesystem::file_size(PathFromUtf8(filePath), sizeError);
        if (sizeError) {
            std::cerr << "Failed to get file size: " << filePath << " (" << sizeError.message()
                      << ")" << std::endl;
            return {};
        }
        if (fileSize64 > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            std::cerr << "File too large for packager entry format: " << filePath << std::endl;
            return {};
        }
        uint32_t fileSize = static_cast<uint32_t>(fileSize64);

        std::string relativePath = filePath;
        if (relativePath.compare(0, sourcePrefixLength, folder.sourcePath) == 0) {
            relativePath = relativePath.substr(sourcePrefixLength);
            if (!relativePath.empty() && (relativePath[0] == '/' || relativePath[0] == '\\')) {
                relativePath = relativePath.substr(1);
            }
        }
        

        uint32_t pathLength = static_cast<uint32_t>(relativePath.length());
        size_t entryStart = tarData.size();
        size_t payloadOffset = entryStart + sizeof(uint32_t) + sizeof(uint32_t) + pathLength;
        tarData.resize(payloadOffset + fileSize);

        std::memcpy(tarData.data() + entryStart, &pathLength, sizeof(pathLength));
        std::memcpy(tarData.data() + entryStart + sizeof(uint32_t), &fileSize, sizeof(fileSize));
        if (pathLength > 0) {
            std::memcpy(tarData.data() + entryStart + sizeof(uint32_t) + sizeof(uint32_t),
                        relativePath.data(),
                        pathLength);
        }

        if (fileSize > 0) {
            std::ifstream file(PathFromUtf8(filePath), std::ios::binary);
            if (!file) {
                std::cerr << "Failed to open file: " << filePath << std::endl;
                return {};
            }
            file.read(reinterpret_cast<char*>(tarData.data() + payloadOffset),
                      static_cast<std::streamsize>(fileSize));
            if (!file) {
                std::cerr << "Failed to read file: " << filePath << std::endl;
                return {};
            }
        }

        FileIndexEntry entry;
        entry.relativePath = relativePath;
        entry.offset = static_cast<uint64_t>(payloadOffset);
        entry.size = static_cast<uint64_t>(fileSize);
        fileIndex.push_back(std::move(entry));
        processedFiles++;
        
        UPDATE_PROGRESS(progressId, processedFiles, relativePath);
        

        if (processedFiles % 50 == 0) {
            // Logging disabled
        }
    }
    
    COMPLETE_PROGRESS(progressId);
    
    // Logging disabled
    
    return tarData;
}

std::vector<uint8_t> CompressionModule::compressWithBlocksLzma(const std::vector<uint8_t>& data) {
#ifdef LibLZMA_FOUND
    std::vector<uint8_t> result;

    size_t totalBlocks = (data.size() + blockSize - 1) / blockSize;
    uint32_t blockCount = static_cast<uint32_t>(totalBlocks);
    if (blockCount == 0) {
        return result;
    }

    struct CompressedBlockResult {
        std::vector<uint8_t> data;
        uint32_t checksum = 0;
    };

    std::vector<CompressedBlockResult> blockResults(totalBlocks);
    unsigned int configuredThreads = ResolveCompressionThreads(threadCount);
    size_t workerCount = std::min(totalBlocks, static_cast<size_t>(configuredThreads));
    unsigned int encoderThreads = workerCount > 1 ? 1u : configuredThreads;

    auto compressBlock = [&](size_t blockIndex, CompressedBlockResult& output) -> bool {
        size_t blockStart = blockIndex * blockSize;
        size_t currentBlockSize =
            (blockSize < (data.size() - blockStart)) ? blockSize : (data.size() - blockStart);

        lzma_stream stream = LZMA_STREAM_INIT;
        lzma_ret ret = LZMA_OK;

#if LZMA_VERSION >= 500200
        if (lzmaSupportsMt) {
            lzma_mt mtOptions{};
            mtOptions.threads = encoderThreads == 0 ? 1u : encoderThreads;
            mtOptions.preset = static_cast<uint32_t>(compressionLevel);
            mtOptions.check = LZMA_CHECK_CRC32;
            mtOptions.block_size = static_cast<uint64_t>(currentBlockSize);
            ret = lzma_stream_encoder_mt(&stream, &mtOptions);
        } else {
            ret = lzma_easy_encoder(&stream, compressionLevel, LZMA_CHECK_CRC32);
        }
#else
        ret = lzma_easy_encoder(&stream, compressionLevel, LZMA_CHECK_CRC32);
#endif

        if (ret != LZMA_OK) {
            std::cerr << "Failed to initialize LZMA encoder for block " << blockIndex << ": "
                      << ret << std::endl;
            return false;
        }

        size_t compressedBound = currentBlockSize + (currentBlockSize / 10) + 1024;
        output.data.resize(compressedBound);
        stream.next_in = data.data() + blockStart;
        stream.avail_in = currentBlockSize;
        stream.next_out = output.data.data();
        stream.avail_out = output.data.size();

        ret = lzma_code(&stream, LZMA_FINISH);
        if (ret != LZMA_STREAM_END) {
            std::cerr << "Block " << blockIndex << " LZMA compression failed: " << ret
                      << std::endl;
            lzma_end(&stream);
            return false;
        }

        size_t compressedSize = output.data.size() - stream.avail_out;
        output.data.resize(compressedSize);
        lzma_end(&stream);
        output.checksum = calculateChecksum(output.data);
        return true;
    };

    std::cout << "Compressing " << totalBlocks << " blocks with LZMA using "
              << workerCount << " worker(s)..." << std::endl;

    bool ok = true;
    if (workerCount <= 1) {
        for (size_t i = 0; i < totalBlocks; ++i) {
            if (!compressBlock(i, blockResults[i])) {
                ok = false;
                break;
            }
            if ((i + 1) % 10 == 0 || i == totalBlocks - 1) {
                std::cout << "  Compressed " << (i + 1) << "/" << totalBlocks << " blocks"
                          << std::endl;
            }
        }
    } else {
        std::atomic<size_t> nextBlock{0};
        std::atomic<size_t> completedBlocks{0};
        std::atomic<bool> failed{false};
        std::vector<std::thread> workers;
        workers.reserve(workerCount);
        for (size_t worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([&]() {
                while (!failed.load(std::memory_order_relaxed)) {
                    size_t blockIndex = nextBlock.fetch_add(1, std::memory_order_relaxed);
                    if (blockIndex >= totalBlocks) {
                        break;
                    }
                    if (!compressBlock(blockIndex, blockResults[blockIndex])) {
                        failed.store(true, std::memory_order_relaxed);
                        break;
                    }
                    size_t done = completedBlocks.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (done % 10 == 0 || done == totalBlocks) {
                        std::cout << "  Compressed " << done << "/" << totalBlocks
                                  << " blocks" << std::endl;
                    }
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        ok = !failed.load(std::memory_order_relaxed);
    }

    if (!ok) {
        return {};
    }

    size_t reservedSize = sizeof(blockCount) + totalBlocks * 16;
    for (const auto& block : blockResults) {
        reservedSize += block.data.size();
    }
    result.reserve(reservedSize);

    result.insert(result.end(),
                  reinterpret_cast<const uint8_t*>(&blockCount),
                  reinterpret_cast<const uint8_t*>(&blockCount) + sizeof(blockCount));

    size_t metadataOffset = result.size();
    result.resize(result.size() + totalBlocks * 16);

    size_t currentOffset = result.size();
    for (size_t i = 0; i < totalBlocks; ++i) {
        size_t blockStart = i * blockSize;
        size_t currentBlockSize =
            (blockSize < (data.size() - blockStart)) ? blockSize : (data.size() - blockStart);
        size_t metadataPos = metadataOffset + i * 16;
        uint32_t offset = static_cast<uint32_t>(currentOffset);
        uint32_t compSize = static_cast<uint32_t>(blockResults[i].data.size());
        uint32_t origSize = static_cast<uint32_t>(currentBlockSize);
        uint32_t blockChecksum = blockResults[i].checksum;

        std::memcpy(result.data() + metadataPos, &offset, sizeof(offset));
        std::memcpy(result.data() + metadataPos + 4, &compSize, sizeof(compSize));
        std::memcpy(result.data() + metadataPos + 8, &origSize, sizeof(origSize));
        std::memcpy(result.data() + metadataPos + 12, &blockChecksum, sizeof(blockChecksum));

        result.insert(result.end(), blockResults[i].data.begin(), blockResults[i].data.end());
        currentOffset += blockResults[i].data.size();
    }
    
    std::cout << "LZMA block compression complete: " << totalBlocks << " blocks, " 
              << result.size() << " bytes" << std::endl;
    
    return result;
#else
    // Stub implementation - just return the original data
    return data;
#endif
}

} // namespace MultiThreadedInstaller
