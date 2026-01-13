#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include "common/lzma_loader.h"
#include <iostream>
#include <fstream>
#include <algorithm>

namespace MultiThreadedInstaller {

DecompressionEngine::DecompressionEngine() 
    : zstdContext(nullptr)
#ifdef LibLZMA_FOUND
    , lzmaInitialized(false)
#else
    , lzmaDecoder(nullptr)
#endif
{
    
    // 初始化Zstandard解压上下文
    zstdContext = ZSTD_createDCtx();
    if (!zstdContext) {
        std::cerr << "Failed to create ZSTD decompression context" << std::endl;
    }
    
    // 初始化LZMA解压器 - 使用动态加载
#ifdef LibLZMA_FOUND
    lzmaStream = LZMA_STREAM_INIT;
    lzmaInitialized = false;  // Will be initialized when needed
#endif
}

DecompressionEngine::~DecompressionEngine() {
    if (zstdContext) {
        ZSTD_freeDCtx(zstdContext);
    }
    
#ifdef LibLZMA_FOUND
    if (lzmaInitialized) {
        // LZMA cleanup will be handled by the dynamic loader
        lzmaInitialized = false;
    }
#endif
}

bool DecompressionEngine::decompressFolder(const DecompressionTask& task) {
    // 对于多线程处理，将任务提交到线程池
    if (threadPool && threadPool->getTotalThreadCount() > 1) {
        // 为每个文件夹创建独立的解压任务
        auto future = threadPool->enqueue([this, task]() -> bool {
            switch (task.algorithm) {
                case CompressionAlgorithm::ZSTD_FAST:
                    return decompressZstd(task);
                case CompressionAlgorithm::LZMA_HIGH:
                    return decompressLzma(task);
                default:
                    std::cerr << "Unknown compression algorithm for " << task.targetPath << std::endl;
                    return false;
            }
        });
        
        try {
            return future.get();
        } catch (const std::exception& e) {
            std::cerr << "Thread pool execution failed for " << task.targetPath 
                      << ": " << e.what() << std::endl;
            return false;
        }
    } else {
        // 单线程处理
        switch (task.algorithm) {
            case CompressionAlgorithm::ZSTD_FAST:
                return decompressZstd(task);
            case CompressionAlgorithm::LZMA_HIGH:
                return decompressLzma(task);
            default:
                std::cerr << "Unknown compression algorithm for " << task.targetPath << std::endl;
                return false;
        }
    }
}

void DecompressionEngine::setThreadPool(std::shared_ptr<ThreadPoolManager> threadPool) {
    this->threadPool = threadPool;
}

void DecompressionEngine::registerProgressCallback(ProgressCallback callback) {
    this->progressCallback = callback;
}

bool DecompressionEngine::decompressZstd(const DecompressionTask& task) {
    if (!zstdContext) {
        std::cerr << "ZSTD context not initialized" << std::endl;
        return false;
    }
    
    if (task.compressedData.empty()) {
        std::cerr << "No compressed data provided" << std::endl;
        return false;
    }
    
    reportProgress(task.targetPath, 0.0f);
    
    try {
        // 改进的格式检测逻辑
        bool useBlockDecompression = false;
        
        if (task.compressedData.size() >= 4) {
            uint32_t firstWord = *reinterpret_cast<const uint32_t*>(task.compressedData.data());
            
            // 详细日志用于诊断
            std::cout << "Format detection for: " << task.targetPath << std::endl;
            std::cout << "  First word: 0x" << std::hex << firstWord << std::dec << std::endl;
            std::cout << "  Data size: " << task.compressedData.size() << " bytes" << std::endl;
            
            // ZSTD 魔数（小端字节序）
            const uint32_t ZSTD_MAGIC = 0x28B52FFD;
            
            if (firstWord == ZSTD_MAGIC) {
                std::cout << "  Format: Standard ZSTD" << std::endl;
                useBlockDecompression = false;
            } else if (firstWord > 0 && firstWord < 100000) {
                // 可能是块格式，进一步验证块元数据大小
                size_t expectedMetadataSize = sizeof(uint32_t) + firstWord * 16;
                if (expectedMetadataSize < task.compressedData.size()) {
                    std::cout << "  Format: Block-based ZSTD (" << firstWord << " blocks)" << std::endl;
                    useBlockDecompression = true;
                } else {
                    std::cout << "  Format: Invalid block metadata (treating as standard ZSTD)" << std::endl;
                    useBlockDecompression = false;
                }
            } else {
                std::cout << "  Format: Unknown (treating as standard ZSTD)" << std::endl;
                useBlockDecompression = false;
            }
        }
        
        if (useBlockDecompression) {
            std::cout << "Using block-level decompression" << std::endl;
            return decompressZstdBlocks(task);
        }
        
        std::cout << "Using standard ZSTD decompression" << std::endl;
        
        // 标准ZSTD解压流程
        // 估算解压后大小
        size_t decompressedSize = ZSTD_getFrameContentSize(task.compressedData.data(), task.compressedData.size());
        if (decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
            std::cerr << "Invalid ZSTD frame for: " << task.targetPath << std::endl;
            return false;
        }
        
        if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
            decompressedSize = task.originalSize;
        }
        
        // 验证预期大小
        if (decompressedSize != task.originalSize && task.originalSize > 0) {
            std::cerr << "Size mismatch for " << task.targetPath 
                      << ": expected " << task.originalSize 
                      << ", got " << decompressedSize << std::endl;
        }
        
        reportProgress(task.targetPath, 0.2f);
        
        // 分配解压缓冲区
        std::vector<uint8_t> decompressedData(decompressedSize);
        
        reportProgress(task.targetPath, 0.3f);
        
        // 执行解压 - 使用多线程上下文如果可用
        size_t actualSize;
        if (threadPool && threadPool->getTotalThreadCount() > 1) {
            // 对于大文件，使用流式解压以支持多线程处理
            if (decompressedSize > 1024 * 1024) { // 1MB threshold
                actualSize = decompressZstdStreaming(task.compressedData, decompressedData);
            } else {
                actualSize = ZSTD_decompress(decompressedData.data(), decompressedData.size(),
                                           task.compressedData.data(), task.compressedData.size());
            }
        } else {
            actualSize = ZSTD_decompress(decompressedData.data(), decompressedData.size(),
                                       task.compressedData.data(), task.compressedData.size());
        }
        
        if (ZSTD_isError(actualSize)) {
            std::cerr << "ZSTD decompression failed for " << task.targetPath 
                      << ": " << ZSTD_getErrorName(actualSize) << std::endl;
            return false;
        }
        
        decompressedData.resize(actualSize);
        
        reportProgress(task.targetPath, 0.7f);
        
        // 验证校验和
        if (!verifyChecksum(decompressedData, task.expectedChecksum)) {
            std::cerr << "Checksum verification failed for: " << task.targetPath << std::endl;
            return false;
        }
        
        reportProgress(task.targetPath, 0.9f);
        
        // 提取tar数据到目标路径
        bool extractSuccess = extractTarData(decompressedData, task.targetPath);
        
        reportProgress(task.targetPath, 1.0f);
        
        return extractSuccess;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception during ZSTD decompression of " << task.targetPath 
                  << ": " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Unknown exception during ZSTD decompression of " << task.targetPath << std::endl;
        return false;
    }
}

bool DecompressionEngine::decompressZstdBlocks(const DecompressionTask& task) {
    if (!zstdContext) {
        std::cerr << "ZSTD context not initialized" << std::endl;
        return false;
    }
    
    if (task.compressedData.empty()) {
        std::cerr << "No compressed data provided for block decompression" << std::endl;
        return false;
    }
    
    reportProgress(task.targetPath, 0.0f);
    
    try {
        // 解析块级格式
        size_t offset = 0;
        
        // 读取块数量
        if (offset + sizeof(uint32_t) > task.compressedData.size()) {
            std::cerr << "Invalid block format: cannot read block count" << std::endl;
            return false;
        }
        
        uint32_t blockCount = *reinterpret_cast<const uint32_t*>(task.compressedData.data() + offset);
        offset += sizeof(uint32_t);
        
        std::cout << "Decompressing " << blockCount << " blocks in parallel..." << std::endl;
        
        // 读取块元数据
        struct BlockMeta {
            uint32_t offset;
            uint32_t compressedSize;
            uint32_t originalSize;
            uint32_t checksum;
        };
        
        if (offset + blockCount * sizeof(BlockMeta) > task.compressedData.size()) {
            std::cerr << "Invalid block format: cannot read block metadata" << std::endl;
            return false;
        }
        
        std::vector<BlockMeta> blocks(blockCount);
        std::memcpy(blocks.data(), task.compressedData.data() + offset, blockCount * sizeof(BlockMeta));
        offset += blockCount * sizeof(BlockMeta);
        
        reportProgress(task.targetPath, 0.1f);
        
        // 验证块元数据
        for (size_t i = 0; i < blocks.size(); ++i) {
            const auto& block = blocks[i];
            if (block.offset + block.compressedSize > task.compressedData.size()) {
                std::cerr << "Invalid block " << i << ": offset " << block.offset 
                          << " + size " << block.compressedSize 
                          << " exceeds data size " << task.compressedData.size() << std::endl;
                return false;
            }
        }
        
        reportProgress(task.targetPath, 0.2f);
        
        // 并行解压每个块
        std::vector<std::future<std::vector<uint8_t>>> futures;
        
        if (threadPool && threadPool->getTotalThreadCount() > 1) {
            size_t totalThreads = threadPool->getTotalThreadCount();
            
            // 计算最优线程数: 每个线程至少处理 4 个块
            size_t blocksPerThreadMin = 4;
            size_t optimalThreads = (blocks.size() + blocksPerThreadMin - 1) / blocksPerThreadMin;
            if (optimalThreads > totalThreads) optimalThreads = totalThreads;
            if (optimalThreads > 8) optimalThreads = 8;  // 最多 8 个线程
            if (optimalThreads < 1) optimalThreads = 1;  // 至少 1 个线程
            
            // 批量处理: 每个线程处理多个块
            size_t blocksPerThread = (blocks.size() + optimalThreads - 1) / optimalThreads;
            
            std::cout << "Using " << optimalThreads << " threads (of " << totalThreads 
                      << " available) for " << blocks.size() << " blocks" << std::endl;
            std::cout << "Each thread processes ~" << blocksPerThread << " blocks" << std::endl;
            
            for (size_t t = 0; t < optimalThreads; ++t) {
                size_t startBlock = t * blocksPerThread;
                size_t endBlock = startBlock + blocksPerThread;
                if (endBlock > blocks.size()) endBlock = blocks.size();
                
                if (startBlock >= blocks.size()) break;
                
                futures.push_back(threadPool->enqueue([this, &task, &blocks, startBlock, endBlock]() -> std::vector<uint8_t> {
                    // 为每个线程创建独立的解压上下文
                    ZSTD_DCtx* localContext = ZSTD_createDCtx();
                    if (!localContext) {
                        throw std::runtime_error("Failed to create ZSTD context");
                    }
                    
                    try {
                        std::vector<uint8_t> threadResult;
                        threadResult.reserve((endBlock - startBlock) * blocks[startBlock].originalSize);
                        
                        // 处理分配给这个线程的所有块
                        for (size_t i = startBlock; i < endBlock; ++i) {
                            const auto& block = blocks[i];
                            std::vector<uint8_t> decompressed(block.originalSize);
                            
                            size_t result = ZSTD_decompressDCtx(
                                localContext,
                                decompressed.data(), decompressed.size(),
                                task.compressedData.data() + block.offset, block.compressedSize
                            );
                            
                            if (ZSTD_isError(result)) {
                                ZSTD_freeDCtx(localContext);
                                throw std::runtime_error(
                                    std::string("Block ") + std::to_string(i) + 
                                    " decompression failed: " + ZSTD_getErrorName(result)
                                );
                            }
                            
                            if (result != block.originalSize) {
                                ZSTD_freeDCtx(localContext);
                                throw std::runtime_error(
                                    std::string("Block ") + std::to_string(i) + 
                                    " size mismatch: expected " + std::to_string(block.originalSize) +
                                    ", got " + std::to_string(result)
                                );
                            }
                            
                            threadResult.insert(threadResult.end(), decompressed.begin(), decompressed.end());
                        }
                        
                        ZSTD_freeDCtx(localContext);
                        return threadResult;
                        
                    } catch (...) {
                        ZSTD_freeDCtx(localContext);
                        throw;
                    }
                }));
            }
        } else {
            // 单线程顺序解压
            std::cout << "Using single-threaded decompression" << std::endl;
            
            for (size_t i = 0; i < blocks.size(); ++i) {
                const auto& block = blocks[i];
                
                std::vector<uint8_t> decompressed(block.originalSize);
                
                size_t result = ZSTD_decompress(
                    decompressed.data(), decompressed.size(),
                    task.compressedData.data() + block.offset, block.compressedSize
                );
                
                if (ZSTD_isError(result)) {
                    std::cerr << "Block " << i << " decompression failed: " 
                              << ZSTD_getErrorName(result) << std::endl;
                    return false;
                }
                
                if (result != block.originalSize) {
                    std::cerr << "Block " << i << " size mismatch: expected " 
                              << block.originalSize << ", got " << result << std::endl;
                    return false;
                }
                
                // 创建一个已完成的 future
                std::promise<std::vector<uint8_t>> promise;
                promise.set_value(std::move(decompressed));
                futures.push_back(promise.get_future());
            }
        }
        
        reportProgress(task.targetPath, 0.3f);
        
        // 收集并合并结果
        std::vector<uint8_t> decompressedData;
        size_t totalSize = 0;
        for (const auto& block : blocks) {
            totalSize += block.originalSize;
        }
        decompressedData.reserve(totalSize);
        
        for (size_t i = 0; i < futures.size(); ++i) {
            try {
                auto blockData = futures[i].get();
                decompressedData.insert(decompressedData.end(), blockData.begin(), blockData.end());
                
                // 报告进度
                float progress = 0.3f + (0.4f * static_cast<float>(i + 1) / futures.size());
                reportProgress(task.targetPath, progress);
                
            } catch (const std::exception& e) {
                std::cerr << "Failed to get block " << i << " result: " << e.what() << std::endl;
                return false;
            }
        }
        
        reportProgress(task.targetPath, 0.8f);
        
        std::cout << "Successfully decompressed " << blockCount << " blocks, total size: " 
                  << decompressedData.size() << " bytes" << std::endl;
        
        // 验证校验和
        if (!verifyChecksum(decompressedData, task.expectedChecksum)) {
            std::cerr << "Checksum verification failed for: " << task.targetPath << std::endl;
            return false;
        }
        
        reportProgress(task.targetPath, 0.9f);
        
        // 提取tar数据到目标路径
        bool extractSuccess = extractTarData(decompressedData, task.targetPath);
        
        reportProgress(task.targetPath, 1.0f);
        
        return extractSuccess;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception during ZSTD block decompression of " << task.targetPath 
                  << ": " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Unknown exception during ZSTD block decompression of " << task.targetPath << std::endl;
        return false;
    }
}

bool DecompressionEngine::decompressLzma(const DecompressionTask& task) {
#ifdef LibLZMA_FOUND
    // Use dynamic loading for LZMA
    static LzmaLoader lzmaLoader;
    
    if (!lzmaLoader.isLoaded()) {
        std::cerr << "LZMA library not available for decompression" << std::endl;
        return false;
    }
    
    if (!lzmaLoader.lzma_stream_decoder_ptr || !lzmaLoader.lzma_code_ptr || !lzmaLoader.lzma_end_ptr) {
        std::cerr << "LZMA decompression functions not available" << std::endl;
        return false;
    }
    
    if (task.compressedData.empty()) {
        std::cerr << "No compressed data provided for LZMA decompression" << std::endl;
        return false;
    }
    
    reportProgress(task.targetPath, 0.0f);
    
    try {
        // Initialize LZMA stream
        lzma_stream stream = LZMA_STREAM_INIT;
        
        // Initialize decoder with auto-detection (supports .xz and .lzma formats)
        // Use UINT64_MAX for unlimited memory
        lzma_ret ret;
        if (lzmaLoader.lzma_auto_decoder_ptr) {
            ret = lzmaLoader.lzma_auto_decoder_ptr(&stream, UINT64_MAX, 0);
        } else {
            ret = lzmaLoader.lzma_stream_decoder_ptr(&stream, UINT64_MAX, 0);
        }
        
        if (ret != LZMA_OK) {
            std::cerr << "Failed to initialize LZMA decoder: " << ret << std::endl;
            return false;
        }
        
        reportProgress(task.targetPath, 0.1f);
        
        // Allocate output buffer (use expected size or estimate)
        size_t outputSize = task.originalSize > 0 ? task.originalSize : task.compressedData.size() * 10;
        std::vector<uint8_t> decompressedData;
        decompressedData.reserve(outputSize);
        
        // Setup input buffer
        stream.next_in = task.compressedData.data();
        stream.avail_in = task.compressedData.size();
        
        reportProgress(task.targetPath, 0.2f);
        
        // Decompress in chunks
        const size_t chunkSize = 64 * 1024; // 64KB chunks
        std::vector<uint8_t> outBuffer(chunkSize);
        
        lzma_action action = LZMA_RUN;
        
        while (true) {
            stream.next_out = outBuffer.data();
            stream.avail_out = outBuffer.size();
            
            // If we've consumed all input, finish
            if (stream.avail_in == 0) {
                action = LZMA_FINISH;
            }
            
            ret = lzmaLoader.lzma_code_ptr(&stream, action);
            
            // Copy decompressed data
            size_t produced = outBuffer.size() - stream.avail_out;
            if (produced > 0) {
                decompressedData.insert(decompressedData.end(), 
                                       outBuffer.begin(), 
                                       outBuffer.begin() + produced);
            }
            
            // Report progress
            float progress = 0.2f + (0.5f * static_cast<float>(stream.total_in) / task.compressedData.size());
            reportProgress(task.targetPath, progress);
            
            // Check for completion or errors
            if (ret == LZMA_STREAM_END) {
                break; // Decompression complete
            }
            
            if (ret != LZMA_OK) {
                std::cerr << "LZMA decompression error: " << ret << std::endl;
                lzmaLoader.lzma_end_ptr(&stream);
                return false;
            }
            
            // Safety check: prevent infinite loops
            if (decompressedData.size() > outputSize * 2 && outputSize > 0) {
                std::cerr << "LZMA decompression produced too much data" << std::endl;
                lzmaLoader.lzma_end_ptr(&stream);
                return false;
            }
        }
        
        // Clean up LZMA stream
        lzmaLoader.lzma_end_ptr(&stream);
        
        reportProgress(task.targetPath, 0.8f);
        
        // Verify size if expected size was provided
        if (task.originalSize > 0 && decompressedData.size() != task.originalSize) {
            std::cerr << "LZMA decompression size mismatch for " << task.targetPath 
                      << ": expected " << task.originalSize 
                      << ", got " << decompressedData.size() << std::endl;
            // Continue anyway, as size might be approximate
        }
        
        // Verify checksum
        if (!verifyChecksum(decompressedData, task.expectedChecksum)) {
            std::cerr << "Checksum verification failed for: " << task.targetPath << std::endl;
            return false;
        }
        
        reportProgress(task.targetPath, 0.9f);
        
        // Extract tar data to target path
        bool extractSuccess = extractTarData(decompressedData, task.targetPath);
        
        reportProgress(task.targetPath, 1.0f);
        
        return extractSuccess;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception during LZMA decompression of " << task.targetPath 
                  << ": " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Unknown exception during LZMA decompression of " << task.targetPath << std::endl;
        return false;
    }
    
#else
    std::cerr << "LZMA support not compiled in" << std::endl;
    return false;
#endif
}

bool DecompressionEngine::verifyChecksum(const std::vector<uint8_t>& data, uint32_t expectedChecksum) {
    uint32_t actualChecksum = calculateChecksum(data);
    return actualChecksum == expectedChecksum;
}

uint32_t DecompressionEngine::calculateChecksum(const std::vector<uint8_t>& data) {
    // 简单的CRC32实现（与压缩模块中的实现相同）
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; i++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return ~crc;
}

size_t DecompressionEngine::decompressZstdStreaming(const std::vector<uint8_t>& compressedData, 
                                                    std::vector<uint8_t>& decompressedData) {
    if (!zstdContext) {
        return 0;
    }
    
    // 重置解压上下文
    ZSTD_DCtx_reset(zstdContext, ZSTD_reset_session_only);
    
    // 设置多线程参数
    if (threadPool && threadPool->getTotalThreadCount() > 1) {
        size_t threadCount = threadPool->getTotalThreadCount();
        size_t numThreads = (threadCount > 4) ? 4 : threadCount;
        // Note: ZSTD_d_nbWorkers may not be available in all versions
        // ZSTD_DCtx_setParameter(zstdContext, ZSTD_d_nbWorkers, numThreads);
    }
    
    const size_t bufferSize = 64 * 1024; // 64KB chunks
    size_t totalDecompressed = 0;
    size_t inputPos = 0;
    
    ZSTD_inBuffer input = { compressedData.data(), compressedData.size(), 0 };
    ZSTD_outBuffer output = { decompressedData.data(), decompressedData.size(), 0 };
    
    while (input.pos < input.size && output.pos < output.size) {
        size_t result = ZSTD_decompressStream(zstdContext, &output, &input);
        
        if (ZSTD_isError(result)) {
            std::cerr << "ZSTD streaming decompression error: " << ZSTD_getErrorName(result) << std::endl;
            return 0;
        }
        
        // 如果需要更多输出空间，扩展缓冲区
        if (output.pos == output.size && result > 0) {
            size_t newSize = decompressedData.size() * 2;
            decompressedData.resize(newSize);
            output.dst = decompressedData.data();
            output.size = newSize;
        }
        
        // 报告进度
        float progress = 0.3f + (0.4f * static_cast<float>(input.pos) / input.size);
        reportProgress("streaming", progress);
        
        if (result == 0) {
            break; // 解压完成
        }
    }
    
    return output.pos;
}

bool DecompressionEngine::extractTarData(const std::vector<uint8_t>& tarData, const std::string& targetPath) {
    FileSystemOperator fsOperator;
    
    if (!fsOperator.createDirectoryRecursive(targetPath)) {
        std::cerr << "Failed to create target directory: " << targetPath << std::endl;
        return false;
    }
    
    size_t offset = 0;
    
    while (offset < tarData.size()) {
        // 读取路径长度
        if (offset + sizeof(uint32_t) > tarData.size()) {
            break;
        }
        
        uint32_t pathLength = *reinterpret_cast<const uint32_t*>(tarData.data() + offset);
        offset += sizeof(uint32_t);
        
        // 读取文件大小
        if (offset + sizeof(uint32_t) > tarData.size()) {
            break;
        }
        
        uint32_t fileSize = *reinterpret_cast<const uint32_t*>(tarData.data() + offset);
        offset += sizeof(uint32_t);
        
        // 读取路径
        if (offset + pathLength > tarData.size()) {
            break;
        }
        
        std::string relativePath(reinterpret_cast<const char*>(tarData.data() + offset), pathLength);
        offset += pathLength;
        
        // 读取文件内容
        if (offset + fileSize > tarData.size()) {
            break;
        }
        
        std::vector<uint8_t> fileContent(tarData.data() + offset, tarData.data() + offset + fileSize);
        offset += fileSize;
        
        // 写入文件
        std::string fullPath = targetPath + "/" + relativePath;
        
        // 创建父目录
        size_t lastSlash = fullPath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            std::string parentDir = fullPath.substr(0, lastSlash);
            fsOperator.createDirectoryRecursive(parentDir);
        }
        
        if (!fsOperator.writeFile(fullPath, fileContent)) {
            std::cerr << "Failed to write file: " << fullPath << std::endl;
            return false;
        }
    }
    
    return true;
}

void DecompressionEngine::reportProgress(const std::string& folderName, float progress) {
    if (progressCallback) {
        progressCallback(folderName, progress);
    }
}

} // namespace MultiThreadedInstaller