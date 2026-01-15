#include "installer/decompression_engine.h"
#include "installer/tar_stream_extractor.h"
#include "common/lzma_loader.h"
#include <iostream>
#include <vector>
#include <future>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#undef min
#undef max
#endif

namespace MultiThreadedInstaller {

namespace {

struct BlockMeta {
    uint32_t offset;
    uint32_t compressedSize;
    uint32_t originalSize;
    uint32_t checksum;
};

bool decompressLzmaBlock(const uint8_t* data, size_t dataSize, const BlockMeta& block,
                         std::vector<uint8_t>& output) {
#ifdef LibLZMA_FOUND
    static LzmaLoader loader;
    if (!loader.isLoaded()) {
        return false;
    }
    
    (void)dataSize;
    
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret = loader.lzma_auto_decoder_ptr
        ? loader.lzma_auto_decoder_ptr(&stream, UINT64_MAX, 0)
        : loader.lzma_stream_decoder_ptr(&stream, UINT64_MAX, 0);
    
    if (ret != LZMA_OK) {
        return false;
    }
    
    stream.next_in = data + block.offset;
    stream.avail_in = block.compressedSize;
    output.resize(block.originalSize);
    stream.next_out = output.data();
    stream.avail_out = output.size();
    
    ret = loader.lzma_code_ptr(&stream, LZMA_FINISH);
    loader.lzma_end_ptr(&stream);
    
    return ret == LZMA_STREAM_END && stream.avail_out == 0;
#else
    (void)data;
    (void)dataSize;
    (void)block;
    (void)output;
    return false;
#endif
}

} // namespace

DecompressionEngine::DecompressionEngine() = default;

DecompressionEngine::~DecompressionEngine() = default;

bool DecompressionEngine::decompressFolder(const DecompressionTask& task) {
    if (threadPool && threadPool->getTotalThreadCount() > 1) {
        auto future = threadPool->enqueue([this, task]() -> bool {
            TarStreamExtractor extractor(task.targetPath);
            Crc32Stream checksum;
            return decompressToStream(task, extractor, &checksum);
        });
        
        try {
            return future.get();
        } catch (const std::exception& e) {
            std::cerr << "Thread pool execution failed for " << task.targetPath
                      << ": " << e.what() << std::endl;
            return false;
        }
    }
    
    TarStreamExtractor extractor(task.targetPath);
    Crc32Stream checksum;
    return decompressToStream(task, extractor, &checksum);
}

void DecompressionEngine::setThreadPool(std::shared_ptr<ThreadPoolManager> threadPool) {
    this->threadPool = threadPool;
}

void DecompressionEngine::registerProgressCallback(ProgressCallback callback) {
    this->progressCallback = callback;
}

bool DecompressionEngine::decompressToStream(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum) {
    if (task.algorithm != CompressionAlgorithm::LZMA_HIGH) {
        std::cerr << "Unsupported compression algorithm for " << task.targetPath << std::endl;
        return false;
    }
    
    return decompressLzma(task, sink, checksum);
}

bool DecompressionEngine::decompressLzmaBlockData(const std::vector<uint8_t>& compressedData,
                                                  size_t originalSize,
                                                  std::vector<uint8_t>& output) {
#ifdef LibLZMA_FOUND
    static LzmaLoader loader;
    if (!loader.isLoaded()) {
        return false;
    }
    
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret = loader.lzma_auto_decoder_ptr
        ? loader.lzma_auto_decoder_ptr(&stream, UINT64_MAX, 0)
        : loader.lzma_stream_decoder_ptr(&stream, UINT64_MAX, 0);
    
    if (ret != LZMA_OK) {
        return false;
    }
    
    stream.next_in = compressedData.data();
    stream.avail_in = compressedData.size();
    output.resize(originalSize);
    stream.next_out = output.data();
    stream.avail_out = output.size();
    
    ret = loader.lzma_code_ptr(&stream, LZMA_FINISH);
    loader.lzma_end_ptr(&stream);
    
    return ret == LZMA_STREAM_END && stream.avail_out == 0;
#else
    (void)compressedData;
    (void)originalSize;
    (void)output;
    return false;
#endif
}

bool DecompressionEngine::decompressLzma(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum) {
#ifdef LibLZMA_FOUND
    static LzmaLoader loader;
    
    if (!loader.isLoaded()) {
        std::cerr << "LZMA library not available for decompression" << std::endl;
        return false;
    }
    
    if (task.compressedData.empty()) {
        std::cerr << "No compressed data provided for LZMA decompression" << std::endl;
        return false;
    }
    
    reportProgress(task.targetPath, 0.0f);
    
    bool useBlockDecompression = false;
    if (task.compressedData.size() >= sizeof(uint32_t)) {
        uint32_t firstWord = *reinterpret_cast<const uint32_t*>(task.compressedData.data());
        size_t expectedMetadataSize = sizeof(uint32_t) + static_cast<size_t>(firstWord) * sizeof(BlockMeta);
        if (firstWord > 0 && firstWord < 100000 && expectedMetadataSize < task.compressedData.size()) {
            useBlockDecompression = true;
        }
    }
    
    if (useBlockDecompression) {
        return decompressLzmaBlocks(task, sink, checksum);
    }
    
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret = loader.lzma_auto_decoder_ptr
        ? loader.lzma_auto_decoder_ptr(&stream, UINT64_MAX, 0)
        : loader.lzma_stream_decoder_ptr(&stream, UINT64_MAX, 0);
    
    if (ret != LZMA_OK) {
        std::cerr << "LZMA decoder init failed: " << ret << std::endl;
        return false;
    }
    
    stream.next_in = task.compressedData.data();
    stream.avail_in = task.compressedData.size();
    
    std::vector<uint8_t> outBuffer(64 * 1024);
    size_t totalOut = 0;
    
    while (true) {
        stream.next_out = outBuffer.data();
        stream.avail_out = outBuffer.size();
        
        ret = loader.lzma_code_ptr(&stream, LZMA_FINISH);
        
        size_t produced = outBuffer.size() - stream.avail_out;
        if (produced > 0) {
            if (!sink.write(outBuffer.data(), produced)) {
                loader.lzma_end_ptr(&stream);
                return false;
            }
            
            if (checksum) {
                checksum->update(outBuffer.data(), produced);
            }
            
            totalOut += produced;
            if (task.originalSize > 0) {
                float progress = std::min(0.99f, static_cast<float>(totalOut) / task.originalSize);
                reportProgress(task.targetPath, progress);
            }
        }
        
        if (ret == LZMA_STREAM_END) {
            break;
        }
        if (ret != LZMA_OK) {
            std::cerr << "LZMA decompression failed: " << ret << std::endl;
            loader.lzma_end_ptr(&stream);
            return false;
        }
    }
    
    loader.lzma_end_ptr(&stream);
    sink.flush();
    
    if (checksum && checksum->finalize() != task.expectedChecksum) {
        std::cerr << "Checksum verification failed for: " << task.targetPath << std::endl;
        return false;
    }
    
    reportProgress(task.targetPath, 1.0f);
    return true;
#else
    (void)task;
    (void)sink;
    (void)checksum;
    std::cerr << "LZMA support not compiled in" << std::endl;
    return false;
#endif
}

bool DecompressionEngine::decompressLzmaBlocks(const DecompressionTask& task, StreamSink& sink, Crc32Stream* checksum) {
#ifdef LibLZMA_FOUND
    if (task.compressedData.size() < sizeof(uint32_t)) {
        std::cerr << "Invalid block format: cannot read block count" << std::endl;
        return false;
    }
    
    size_t offset = 0;
    uint32_t blockCount = *reinterpret_cast<const uint32_t*>(task.compressedData.data() + offset);
    offset += sizeof(uint32_t);
    
    if (offset + blockCount * sizeof(BlockMeta) > task.compressedData.size()) {
        std::cerr << "Invalid block format: cannot read block metadata" << std::endl;
        return false;
    }
    
    std::vector<BlockMeta> blocks(blockCount);
    std::memcpy(blocks.data(), task.compressedData.data() + offset, blockCount * sizeof(BlockMeta));
    
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& block = blocks[i];
        if (block.offset + block.compressedSize > task.compressedData.size()) {
            std::cerr << "Invalid block " << i << ": offset " << block.offset
                      << " + size " << block.compressedSize
                      << " exceeds data size " << task.compressedData.size() << std::endl;
            return false;
        }
    }
    
    size_t totalOut = 0;
    
    if (threadPool && threadPool->getTotalThreadCount() > 1) {
        size_t totalThreads = threadPool->getTotalThreadCount();
        size_t blocksPerThreadMin = 4;
        size_t optimalThreads = (blocks.size() + blocksPerThreadMin - 1) / blocksPerThreadMin;
        if (optimalThreads > totalThreads) optimalThreads = totalThreads;
        if (optimalThreads > 8) optimalThreads = 8;
        if (optimalThreads < 1) optimalThreads = 1;
        
        size_t blocksPerThread = (blocks.size() + optimalThreads - 1) / optimalThreads;
        std::vector<std::future<std::vector<uint8_t>>> futures;
        
        for (size_t t = 0; t < optimalThreads; ++t) {
            size_t startBlock = t * blocksPerThread;
            size_t endBlock = std::min(startBlock + blocksPerThread, blocks.size());
            if (startBlock >= blocks.size()) break;
            
            futures.push_back(threadPool->enqueue([&task, &blocks, startBlock, endBlock]() -> std::vector<uint8_t> {
                std::vector<uint8_t> chunk;
                for (size_t i = startBlock; i < endBlock; ++i) {
                    std::vector<uint8_t> blockOut;
                    if (!decompressLzmaBlock(task.compressedData.data(), task.compressedData.size(), blocks[i], blockOut)) {
                        throw std::runtime_error("LZMA block decompression failed");
                    }
                    chunk.insert(chunk.end(), blockOut.begin(), blockOut.end());
                }
                return chunk;
            }));
        }
        
        for (auto& future : futures) {
            std::vector<uint8_t> chunk;
            try {
                chunk = future.get();
            } catch (const std::exception& e) {
                std::cerr << "LZMA block decompression failed: " << e.what() << std::endl;
                return false;
            }
            
            if (!chunk.empty()) {
                if (!sink.write(chunk.data(), chunk.size())) {
                    return false;
                }
                
                if (checksum) {
                    checksum->update(chunk.data(), chunk.size());
                }
                
                totalOut += chunk.size();
                if (task.originalSize > 0) {
                    float progress = std::min(0.99f, static_cast<float>(totalOut) / task.originalSize);
                    reportProgress(task.targetPath, progress);
                }
            }
        }
    } else {
        for (size_t i = 0; i < blocks.size(); ++i) {
            std::vector<uint8_t> blockOut;
            if (!decompressLzmaBlock(task.compressedData.data(), task.compressedData.size(), blocks[i], blockOut)) {
                std::cerr << "Block " << i << " LZMA decompression failed" << std::endl;
                return false;
            }
            
            if (!sink.write(blockOut.data(), blockOut.size())) {
                return false;
            }
            
            if (checksum) {
                checksum->update(blockOut.data(), blockOut.size());
            }
            
            totalOut += blockOut.size();
            if (task.originalSize > 0) {
                float progress = std::min(0.99f, static_cast<float>(totalOut) / task.originalSize);
                reportProgress(task.targetPath, progress);
            }
        }
    }
    
    sink.flush();
    
    if (checksum && checksum->finalize() != task.expectedChecksum) {
        std::cerr << "Checksum verification failed for: " << task.targetPath << std::endl;
        return false;
    }
    
    reportProgress(task.targetPath, 1.0f);
    return true;
#else
    (void)task;
    (void)sink;
    (void)checksum;
    std::cerr << "LZMA support not compiled in" << std::endl;
    return false;
#endif
}

void DecompressionEngine::reportProgress(const std::string& folderName, float progress) {
    if (progressCallback) {
        progressCallback(folderName, progress);
    }
}

} // namespace MultiThreadedInstaller
