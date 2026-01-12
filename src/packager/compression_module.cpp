#include "packager/compression_module.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <thread>
#include <algorithm>
#include <chrono>

namespace MultiThreadedInstaller {

CompressionModule::CompressionModule() 
    : currentAlgorithm(CompressionAlgorithm::ZSTD_FAST)
    , compressionLevel(Constants::DEFAULT_ZSTD_LEVEL)
    , blockSize(Constants::DEFAULT_BLOCK_SIZE)
    , zstdContext(nullptr)
    , lzmaInitialized(false) {
    
#ifdef ZSTD_FOUND
    // 初始化Zstandard上下文
    zstdContext = ZSTD_createCCtx();
    if (!zstdContext) {
        std::cerr << "Failed to create ZSTD compression context" << std::endl;
    }
#else
    std::cerr << "ZSTD not available - using stub implementation" << std::endl;
#endif
    
#ifdef LibLZMA_FOUND
    // 初始化LZMA动态加载器
    lzmaLoader = std::make_unique<LzmaLoader>();
    if (lzmaLoader->isLoaded()) {
        // 初始化LZMA流
        lzmaStream = LZMA_STREAM_INIT;
        lzma_ret ret = lzmaLoader->lzma_easy_encoder_ptr(&lzmaStream, Constants::DEFAULT_LZMA_LEVEL, LZMA_CHECK_SHA256);
        if (ret == LZMA_OK) {
            lzmaInitialized = true;
            std::cout << "LZMA encoder initialized successfully" << std::endl;
        } else {
            std::cerr << "Failed to initialize LZMA encoder: " << ret << std::endl;
        }
    } else {
        std::cerr << "LZMA library not loaded - using stub implementation" << std::endl;
    }
#else
    std::cerr << "LZMA not available - using stub implementation" << std::endl;
#endif
}

CompressionModule::~CompressionModule() {
#ifdef ZSTD_FOUND
    if (zstdContext) {
        ZSTD_freeCCtx(zstdContext);
    }
#endif
#ifdef LibLZMA_FOUND
    if (lzmaInitialized && lzmaLoader && lzmaLoader->isLoaded()) {
        lzmaLoader->lzma_end_ptr(&lzmaStream);
    }
#endif
}

CompressionResult CompressionModule::compressFolder(const FolderInfo& folder) {
    // Performance tracking disabled
    
    // Logging disabled
    // Logging disabled
    
    const char* algorithmName = (currentAlgorithm == CompressionAlgorithm::ZSTD_FAST) ? "ZSTD" : "LZMA";
    // Logging disabled
    
    auto startTime = std::chrono::steady_clock::now();
    CompressionResult result;
    
    switch (currentAlgorithm) {
        case CompressionAlgorithm::ZSTD_FAST:
            result = compressWithZstd(folder);
            break;
        case CompressionAlgorithm::LZMA_HIGH:
            result = compressWithLzma(folder);
            break;
        default:
            std::cerr << "Unknown compression algorithm" << std::endl;
            return CompressionResult{};
    }
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    if (result.compressedSize > 0) {
        double compressionRatio = static_cast<double>(result.compressedSize) / result.originalSize;
        double savedSpace = (1.0 - compressionRatio) * 100.0;
        
        // Logging disabled
        // Logging disabled
        // Logging disabled
        
        // 记录性能指标
        // Performance tracking disabled
    } else {
        // Logging disabled
    }
    
    return result;
}

bool CompressionModule::setCompressionAlgorithm(CompressionAlgorithm algorithm) {
    const char* oldAlgorithm = (currentAlgorithm == CompressionAlgorithm::ZSTD_FAST) ? "ZSTD" : "LZMA";
    const char* newAlgorithm = (algorithm == CompressionAlgorithm::ZSTD_FAST) ? "ZSTD" : "LZMA";
    
    // Logging disabled
    
    currentAlgorithm = algorithm;
    return true;
}

bool CompressionModule::setCompressionLevel(int level) {
    compressionLevel = level;
    return true;
}

bool CompressionModule::setBlockSize(size_t blockSize) {
    this->blockSize = blockSize;
    return true;
}

CompressionResult CompressionModule::compressWithZstd(const FolderInfo& folder) {
    CompressionResult result;
    result.algorithm = CompressionAlgorithm::ZSTD_FAST;
    
#ifdef ZSTD_FOUND
    if (!zstdContext) {
        std::cerr << "ZSTD context not initialized" << std::endl;
        return result;
    }
    
    // 创建tar格式的数据
    // Logging disabled
    auto tarTimer = // Performance tracking disabled
    std::vector<uint8_t> tarData = createTarData(folder);
    if (tarData.empty()) {
        std::cerr << "Failed to create tar data for folder: " << folder.sourcePath << std::endl;
        return result;
    }
    
    result.originalSize = tarData.size();
    
    // 设置压缩参数 - 快速模式优化
    ZSTD_CCtx_setParameter(zstdContext, ZSTD_c_compressionLevel, compressionLevel);
    ZSTD_CCtx_setParameter(zstdContext, ZSTD_c_nbWorkers, std::thread::hardware_concurrency());
    
    // 启用块级压缩以支持随机访问
    ZSTD_CCtx_setParameter(zstdContext, ZSTD_c_jobSize, blockSize);
    ZSTD_CCtx_setParameter(zstdContext, ZSTD_c_overlapLog, 0); // 无重叠以支持随机访问
    
    // 启用校验和
    ZSTD_CCtx_setParameter(zstdContext, ZSTD_c_checksumFlag, 1);
    
    // 执行标准ZSTD压缩（而不是块级压缩）
    size_t compressedBound = ZSTD_compressBound(tarData.size());
    result.compressedData.resize(compressedBound);
    
    // Logging disabled
    auto compressionTimer = // Performance tracking disabled
    
    size_t compressedSize = ZSTD_compress2(zstdContext,
                                          result.compressedData.data(),
                                          result.compressedData.size(),
                                          tarData.data(),
                                          tarData.size());
    
    if (ZSTD_isError(compressedSize)) {
        std::cerr << "ZSTD compression failed: " << ZSTD_getErrorName(compressedSize) << std::endl;
        return CompressionResult{};
    }
    
    result.compressedData.resize(compressedSize);
    
    result.compressedSize = result.compressedData.size();
    result.checksum = calculateChecksum(tarData); // 对原始数据计算校验和
#else
    // Stub implementation - just copy data with minimal "compression"
    std::vector<uint8_t> tarData = createTarData(folder);
    if (tarData.empty()) {
        std::cerr << "Failed to create tar data for folder: " << folder.sourcePath << std::endl;
        return result;
    }
    
    result.originalSize = tarData.size();
    result.compressedData = tarData; // No actual compression
    result.compressedSize = tarData.size();
    result.checksum = calculateChecksum(tarData); // 对原始数据计算校验和
    
    std::cout << "Using stub ZSTD implementation (no actual compression)" << std::endl;
#endif
    
    return result;
}

CompressionResult CompressionModule::compressWithLzma(const FolderInfo& folder) {
    CompressionResult result;
    result.algorithm = CompressionAlgorithm::LZMA_HIGH;
    
#ifdef LibLZMA_FOUND
    if (!lzmaInitialized || !lzmaLoader || !lzmaLoader->isLoaded()) {
        std::cerr << "LZMA encoder not initialized or library not loaded" << std::endl;
        return result;
    }
    
    // 创建tar格式的数据
    // Logging disabled
    auto tarTimer = // Performance tracking disabled
    std::vector<uint8_t> tarData = createTarData(folder);
    if (tarData.empty()) {
        std::cerr << "Failed to create tar data for folder: " << folder.sourcePath << std::endl;
        return result;
    }
    
    result.originalSize = tarData.size();
    
    // 重新初始化LZMA流以确保干净状态
    if (lzmaLoader && lzmaLoader->isLoaded()) {
        lzmaLoader->lzma_end_ptr(&lzmaStream);
        lzmaStream = LZMA_STREAM_INIT;
        lzma_ret ret = lzmaLoader->lzma_easy_encoder_ptr(&lzmaStream, compressionLevel, LZMA_CHECK_SHA256);
        if (ret != LZMA_OK) {
            std::cerr << "Failed to reinitialize LZMA encoder: " << ret << std::endl;
            return result;
        }
    } else {
        std::cerr << "LZMA library not loaded" << std::endl;
        return result;
    }
    
    // 估算压缩后大小（LZMA通常需要更多空间）
    size_t outputSize = tarData.size() + (tarData.size() / 10) + 1024;
    result.compressedData.resize(outputSize);
    
    // 设置输入和输出缓冲区
    lzmaStream.next_in = tarData.data();
    lzmaStream.avail_in = tarData.size();
    lzmaStream.next_out = result.compressedData.data();
    lzmaStream.avail_out = result.compressedData.size();
    
    // 执行压缩
    // Logging disabled
    auto compressionTimer = // Performance tracking disabled
    
    lzma_ret ret = lzmaLoader->lzma_code_ptr(&lzmaStream, LZMA_FINISH);
    
    if (ret != LZMA_STREAM_END) {
        std::cerr << "LZMA compression failed: " << ret << std::endl;
        return CompressionResult{};
    }
    
    // 调整输出大小
    size_t compressedSize = result.compressedData.size() - lzmaStream.avail_out;
    result.compressedData.resize(compressedSize);
    result.compressedSize = compressedSize;
    
    // 计算原始数据的CRC32校验和（与ZSTD保持一致）
    result.checksum = calculateChecksum(tarData);
#else
    // Stub implementation - just copy data with minimal "compression"
    std::vector<uint8_t> tarData = createTarData(folder);
    if (tarData.empty()) {
        std::cerr << "Failed to create tar data for folder: " << folder.sourcePath << std::endl;
        return result;
    }
    
    result.originalSize = tarData.size();
    result.compressedData = tarData; // No actual compression
    result.compressedSize = tarData.size();
    
    // 计算SHA-256校验和
    std::vector<uint8_t> sha256Hash = calculateSHA256(result.compressedData);
    if (sha256Hash.size() >= 4) {
        result.checksum = *reinterpret_cast<const uint32_t*>(sha256Hash.data());
    }
    
    std::cout << "Using stub LZMA implementation (no actual compression)" << std::endl;
#endif
    
    return result;
}

uint32_t CompressionModule::calculateChecksum(const std::vector<uint8_t>& data) {
    // 简单的CRC32实现
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

std::vector<uint8_t> CompressionModule::readFileContent(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        return {};
    }
    
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> content(fileSize);
    file.read(reinterpret_cast<char*>(content.data()), fileSize);
    
    if (!file) {
        // Logging disabled
        return {};
    }
    
    // Logging disabled
    return content;
}

std::vector<uint8_t> CompressionModule::createTarData(const FolderInfo& folder) {
    // 简化的tar格式实现
    // 实际实现中应该使用标准的tar格式
    std::vector<uint8_t> tarData;
    size_t processedFiles = 0;
    size_t totalSize = 0;
    
    auto progressId = START_PROGRESS("创建TAR数据", folder.files.size());
    
    for (const auto& filePath : folder.files) {
        // 读取文件内容
        std::vector<uint8_t> fileContent = readFileContent(filePath);
        if (fileContent.empty()) {
            LOG_WARNINGF("CompressionModule", "跳过空文件: %s", filePath.c_str());
            continue;
        }
        
        // 计算相对路径
        std::string relativePath = filePath;
        if (relativePath.find(folder.sourcePath) == 0) {
            relativePath = relativePath.substr(folder.sourcePath.length());
            if (!relativePath.empty() && (relativePath[0] == '/' || relativePath[0] == '\\')) {
                relativePath = relativePath.substr(1);
            }
        }
        
        // 添加文件头信息（简化格式）
        uint32_t pathLength = static_cast<uint32_t>(relativePath.length());
        uint32_t fileSize = static_cast<uint32_t>(fileContent.size());
        
        // 写入路径长度
        tarData.insert(tarData.end(), 
                      reinterpret_cast<const uint8_t*>(&pathLength), 
                      reinterpret_cast<const uint8_t*>(&pathLength) + sizeof(pathLength));
        
        // 写入文件大小
        tarData.insert(tarData.end(), 
                      reinterpret_cast<const uint8_t*>(&fileSize), 
                      reinterpret_cast<const uint8_t*>(&fileSize) + sizeof(fileSize));
        
        // 写入路径
        tarData.insert(tarData.end(), relativePath.begin(), relativePath.end());
        
        // 写入文件内容
        tarData.insert(tarData.end(), fileContent.begin(), fileContent.end());
        
        totalSize += fileContent.size();
        processedFiles++;
        
        UPDATE_PROGRESS(progressId, processedFiles, relativePath);
        
        // 每处理50个文件记录一次进度
        if (processedFiles % 50 == 0) {
            // Logging disabled
        }
    }
    
    COMPLETE_PROGRESS(progressId);
    
    // Logging disabled
    
    return tarData;
}

std::vector<uint8_t> CompressionModule::compressWithBlocks(const std::vector<uint8_t>& data) {
#ifdef ZSTD_FOUND
    std::vector<uint8_t> result;
    
    // 块头信息：块数量 (4字节)
    size_t totalBlocks = (data.size() + blockSize - 1) / blockSize;
    uint32_t blockCount = static_cast<uint32_t>(totalBlocks);
    
    result.insert(result.end(), 
                  reinterpret_cast<const uint8_t*>(&blockCount),
                  reinterpret_cast<const uint8_t*>(&blockCount) + sizeof(blockCount));
    
    // 为每个块的元数据预留空间 (偏移量4字节 + 压缩大小4字节 + 原始大小4字节 + 校验和4字节)
    size_t metadataOffset = result.size();
    result.resize(result.size() + totalBlocks * 16); // 16字节每个块的元数据
    
    // 压缩每个块
    size_t currentOffset = result.size();
    for (size_t i = 0; i < totalBlocks; ++i) {
        size_t blockStart = i * blockSize;
        size_t currentBlockSize = (blockSize < (data.size() - blockStart)) ? blockSize : (data.size() - blockStart);
        
        // 压缩当前块
        size_t compressedBound = ZSTD_compressBound(currentBlockSize);
        std::vector<uint8_t> compressedBlock(compressedBound);
        
        size_t compressedSize = ZSTD_compress2(zstdContext,
                                              compressedBlock.data(),
                                              compressedBlock.size(),
                                              data.data() + blockStart,
                                              currentBlockSize);
        
        if (ZSTD_isError(compressedSize)) {
            std::cerr << "Block compression failed: " << ZSTD_getErrorName(compressedSize) << std::endl;
            return {};
        }
        
        compressedBlock.resize(compressedSize);
        
        // 计算块校验和
        uint32_t blockChecksum = calculateChecksum(compressedBlock);
        
        // 写入块元数据
        size_t metadataPos = metadataOffset + i * 16;
        uint32_t offset = static_cast<uint32_t>(currentOffset);
        uint32_t compSize = static_cast<uint32_t>(compressedSize);
        uint32_t origSize = static_cast<uint32_t>(currentBlockSize);
        
        std::memcpy(result.data() + metadataPos, &offset, sizeof(offset));
        std::memcpy(result.data() + metadataPos + 4, &compSize, sizeof(compSize));
        std::memcpy(result.data() + metadataPos + 8, &origSize, sizeof(origSize));
        std::memcpy(result.data() + metadataPos + 12, &blockChecksum, sizeof(blockChecksum));
        
        // 添加压缩块数据
        result.insert(result.end(), compressedBlock.begin(), compressedBlock.end());
        currentOffset += compressedSize;
    }
    
    return result;
#else
    // Stub implementation - just return the original data
    return data;
#endif
}

std::vector<uint8_t> CompressionModule::calculateSHA256(const std::vector<uint8_t>& data) {
    // 简化的SHA-256实现 - 在实际项目中应使用OpenSSL或其他加密库
    // 这里使用一个简单的哈希函数作为占位符
    std::vector<uint8_t> hash(32, 0); // SHA-256产生32字节哈希
    
    // 简单的哈希算法（不是真正的SHA-256，仅用于演示）
    uint64_t h = 0x6a09e667f3bcc908ULL; // SHA-256初始值之一
    
    for (size_t i = 0; i < data.size(); ++i) {
        h = h * 31 + data[i];
        h ^= (h >> 16);
    }
    
    // 将哈希值分布到32字节中
    for (int i = 0; i < 32; i += 8) {
        uint64_t chunk = h;
        for (int j = 0; j < 8 && i + j < 32; ++j) {
            hash[i + j] = static_cast<uint8_t>(chunk & 0xFF);
            chunk >>= 8;
        }
        h = h * 1103515245 + 12345; // 线性同余生成器
    }
    
    return hash;
}

} // namespace MultiThreadedInstaller