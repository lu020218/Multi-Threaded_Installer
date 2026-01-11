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
    if (threadPool && threadPool->getActiveThreadCount() > 1) {
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
        if (threadPool && threadPool->getActiveThreadCount() > 1) {
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

bool DecompressionEngine::decompressLzma(const DecompressionTask& task) {
#ifdef LibLZMA_FOUND
    // Use dynamic loading for LZMA
    static LzmaLoader lzmaLoader;
    
    if (!lzmaLoader.isLoaded()) {
        std::cerr << "LZMA library not available for decompression" << std::endl;
        return false;
    }
    
    if (task.compressedData.empty()) {
        std::cerr << "No compressed data provided for LZMA decompression" << std::endl;
        return false;
    }
    
    reportProgress(task.targetPath, 0.0f);
    
    try {
        // Initialize LZMA stream
        lzmaStream = LZMA_STREAM_INIT;
        
        // For decompression, we need lzma_stream_decoder which is not in the loader
        // For now, we'll return false and log that LZMA decompression is not fully implemented
        std::cerr << "LZMA decompression requires additional function loading - not fully implemented" << std::endl;
        return false;
        
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
    if (threadPool && threadPool->getActiveThreadCount() > 1) {
        size_t threadCount = threadPool->getActiveThreadCount();
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