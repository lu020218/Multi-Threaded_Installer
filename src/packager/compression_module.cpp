#include "packager/compression_module.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <thread>
#include <algorithm>
#include <chrono>

// Stub macros for disabled logging
#define START_TIMER(name) 0
#define START_PROGRESS(name, total) 0
#define UPDATE_PROGRESS(id, completed, current) do {} while(0)
#define COMPLETE_PROGRESS(id) do {} while(0)
#define LOG_WARNINGF(module, format, ...) do {} while(0)

namespace MultiThreadedInstaller {

CompressionModule::CompressionModule() 
    : currentAlgorithm(CompressionAlgorithm::LZMA_HIGH)
    , compressionLevel(Constants::DEFAULT_LZMA_LEVEL)
    , blockSize(Constants::DEFAULT_BLOCK_SIZE)
    , lzmaInitialized(false) {
    
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
#ifdef LibLZMA_FOUND
    if (lzmaInitialized && lzmaLoader && lzmaLoader->isLoaded()) {
        lzmaLoader->lzma_end_ptr(&lzmaStream);
    }
#endif
}

CompressionResult CompressionModule::compressFolder(const FolderInfo& folder) {
    // Performance tracking disabled
    const char* algorithmName = "LZMA";
    
    auto startTime = std::chrono::steady_clock::now();
    CompressionResult result;
    
    result = compressWithLzma(folder);
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    if (result.compressedSize > 0) {
        double compressionRatio = static_cast<double>(result.compressedSize) / result.originalSize;
        double savedSpace = (1.0 - compressionRatio) * 100.0;
    
        // 记录性能指标
        // Performance tracking disabled
    } else {
        // Logging disabled
    }
    
    return result;
}

bool CompressionModule::setCompressionAlgorithm(CompressionAlgorithm algorithm) {
    const char* oldAlgorithm = "LZMA";
    const char* newAlgorithm = "LZMA";
    
    // Logging disabled
    
    currentAlgorithm = CompressionAlgorithm::LZMA_HIGH;
    return algorithm == CompressionAlgorithm::LZMA_HIGH;
}

bool CompressionModule::setCompressionLevel(int level) {
    compressionLevel = level;
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
    if (!lzmaInitialized || !lzmaLoader || !lzmaLoader->isLoaded()) {
        std::cerr << "LZMA encoder not initialized or library not loaded" << std::endl;
        return result;
    }
    
    // 创建tar格式的数据
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
    
    // 块级压缩
    // Logging disabled
    auto compressionTimer = START_TIMER("LzmaCompression");
    
    result.compressedData = compressWithBlocksLzma(tarData);
    
    if (result.compressedData.empty()) {
        std::cerr << "Block LZMA compression failed" << std::endl;
        return CompressionResult{};
    }
    
    result.compressedSize = result.compressedData.size();
    
    // 解析块索引
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
    
    // 计算原始数据的CRC32校验和
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

std::vector<uint8_t> CompressionModule::createTarData(const FolderInfo& folder,
                                                      std::vector<FileIndexEntry>& fileIndex) {
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
        
        // 记录文件索引（内容起始偏移）
        FileIndexEntry entry;
        entry.relativePath = relativePath;
        entry.offset = static_cast<uint64_t>(
            tarData.size() + sizeof(uint32_t) + sizeof(uint32_t) + relativePath.size());
        entry.size = static_cast<uint64_t>(fileContent.size());
        fileIndex.push_back(std::move(entry));
        
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

std::vector<uint8_t> CompressionModule::compressWithBlocksLzma(const std::vector<uint8_t>& data) {
#ifdef LibLZMA_FOUND
    if (!lzmaLoader || !lzmaLoader->isLoaded()) {
        std::cerr << "LZMA library not loaded" << std::endl;
        return {};
    }
    
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
    
    std::cout << "Compressing " << totalBlocks << " blocks with LZMA..." << std::endl;
    
    // 压缩每个块
    size_t currentOffset = result.size();
    for (size_t i = 0; i < totalBlocks; ++i) {
        size_t blockStart = i * blockSize;
        size_t currentBlockSize = (blockSize < (data.size() - blockStart)) ? blockSize : (data.size() - blockStart);
        
        // 为每个块创建独立的 LZMA 流
        lzma_stream stream = LZMA_STREAM_INIT;
        lzma_ret ret = lzmaLoader->lzma_easy_encoder_ptr(&stream, compressionLevel, LZMA_CHECK_CRC32);
        
        if (ret != LZMA_OK) {
            std::cerr << "Failed to initialize LZMA encoder for block " << i << ": " << ret << std::endl;
            return {};
        }
        
        // 估算压缩后大小
        size_t compressedBound = currentBlockSize + (currentBlockSize / 10) + 1024;
        std::vector<uint8_t> compressedBlock(compressedBound);
        
        // 设置输入和输出
        stream.next_in = data.data() + blockStart;
        stream.avail_in = currentBlockSize;
        stream.next_out = compressedBlock.data();
        stream.avail_out = compressedBlock.size();
        
        // 压缩当前块
        ret = lzmaLoader->lzma_code_ptr(&stream, LZMA_FINISH);
        
        if (ret != LZMA_STREAM_END) {
            std::cerr << "Block " << i << " LZMA compression failed: " << ret << std::endl;
            lzmaLoader->lzma_end_ptr(&stream);
            return {};
        }
        
        size_t compressedSize = compressedBlock.size() - stream.avail_out;
        compressedBlock.resize(compressedSize);
        
        // 清理流
        lzmaLoader->lzma_end_ptr(&stream);
        
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
        
        // 进度输出
        if ((i + 1) % 10 == 0 || i == totalBlocks - 1) {
            std::cout << "  Compressed " << (i + 1) << "/" << totalBlocks << " blocks" << std::endl;
        }
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
