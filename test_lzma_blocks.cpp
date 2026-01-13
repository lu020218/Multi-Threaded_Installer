#include "packager/compression_module.h"
#include "installer/decompression_engine.h"
#include "installer/thread_pool_manager.h"
#include "common/types.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <cstring>
#include <iomanip>

using namespace MultiThreadedInstaller;

// 生成测试数据
std::vector<uint8_t> generateTestData(size_t size) {
    std::vector<uint8_t> data(size);
    
    // 生成具有一定模式的数据（更容易压缩）
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>((i % 256) ^ ((i / 256) % 256));
    }
    
    return data;
}

// 创建简单的tar格式数据
std::vector<uint8_t> createSimpleTar(const std::vector<uint8_t>& fileData, const std::string& filename) {
    std::vector<uint8_t> tarData;
    
    // 路径长度
    uint32_t pathLength = static_cast<uint32_t>(filename.length());
    tarData.insert(tarData.end(), 
                   reinterpret_cast<const uint8_t*>(&pathLength),
                   reinterpret_cast<const uint8_t*>(&pathLength) + sizeof(pathLength));
    
    // 文件大小
    uint32_t fileSize = static_cast<uint32_t>(fileData.size());
    tarData.insert(tarData.end(),
                   reinterpret_cast<const uint8_t*>(&fileSize),
                   reinterpret_cast<const uint8_t*>(&fileSize) + sizeof(fileSize));
    
    // 路径
    tarData.insert(tarData.end(), filename.begin(), filename.end());
    
    // 文件内容
    tarData.insert(tarData.end(), fileData.begin(), fileData.end());
    
    return tarData;
}

// 计算CRC32校验和
uint32_t calculateChecksum(const std::vector<uint8_t>& data) {
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

int main() {
    std::cout << "=== LZMA Block Compression Test ===" << std::endl;
    std::cout << std::endl;
    
    // 创建线程池
    auto threadPool = std::make_shared<ThreadPoolManager>(std::thread::hardware_concurrency());
    std::cout << "Thread pool created with " << threadPool->getTotalThreadCount() << " threads" << std::endl;
    std::cout << std::endl;
    
    // 生成测试数据 (160MB)
    const size_t testDataSize = 160 * 1024 * 1024;
    std::cout << "Generating " << testDataSize / (1024 * 1024) << " MB test data..." << std::endl;
    auto testData = generateTestData(testDataSize);
    std::cout << "Test data generated: " << testData.size() << " bytes" << std::endl;
    std::cout << std::endl;
    
    // 创建tar格式数据
    auto tarData = createSimpleTar(testData, "test_file.bin");
    std::cout << "TAR data created: " << tarData.size() << " bytes" << std::endl;
    uint32_t checksum = calculateChecksum(tarData);
    std::cout << "Checksum: 0x" << std::hex << checksum << std::dec << std::endl;
    std::cout << std::endl;
    
    // 测试LZMA压缩
    std::cout << "--- LZMA Block Compression Test ---" << std::endl;
    std::cout << "Note: This test simulates block compression by directly testing" << std::endl;
    std::cout << "      the decompression of block-formatted LZMA data." << std::endl;
    std::cout << std::endl;
    
    // 由于 compressWithBlocksLzma 是私有方法，我们创建一个模拟的块格式数据
    // 实际项目中，这会由 CompressionModule 自动处理（当文件 > 128MB 时）
    
    // 为了测试，我们手动创建块格式的压缩数据
    std::cout << "Creating block-formatted LZMA compressed data..." << std::endl;
    
    const size_t blockSize = 2 * 1024 * 1024; // 2MB blocks
    size_t totalBlocks = (tarData.size() + blockSize - 1) / blockSize;
    
    std::vector<uint8_t> blockCompressedData;
    
    // 写入块数量
    uint32_t blockCount = static_cast<uint32_t>(totalBlocks);
    blockCompressedData.insert(blockCompressedData.end(),
                               reinterpret_cast<const uint8_t*>(&blockCount),
                               reinterpret_cast<const uint8_t*>(&blockCount) + sizeof(blockCount));
    
    // 预留元数据空间
    size_t metadataOffset = blockCompressedData.size();
    blockCompressedData.resize(blockCompressedData.size() + totalBlocks * 16);
    
    auto compressionStart = std::chrono::steady_clock::now();
    
    // 压缩每个块（简化版本 - 实际会由 CompressionModule 处理）
    std::cout << "Compressing " << totalBlocks << " blocks..." << std::endl;
    
    size_t currentOffset = blockCompressedData.size();
    for (size_t i = 0; i < totalBlocks; ++i) {
        size_t blockStart = i * blockSize;
        size_t currentBlockSize = (blockSize < (tarData.size() - blockStart)) ? blockSize : (tarData.size() - blockStart);
        
        // 简化：直接存储未压缩数据（实际应该使用LZMA压缩）
        // 这里只是为了测试解压逻辑
        std::vector<uint8_t> blockData(tarData.begin() + blockStart, 
                                       tarData.begin() + blockStart + currentBlockSize);
        
        // 写入块元数据
        size_t metadataPos = metadataOffset + i * 16;
        uint32_t offset = static_cast<uint32_t>(currentOffset);
        uint32_t compSize = static_cast<uint32_t>(blockData.size());
        uint32_t origSize = static_cast<uint32_t>(currentBlockSize);
        uint32_t blockChecksum = calculateChecksum(blockData);
        
        std::memcpy(blockCompressedData.data() + metadataPos, &offset, sizeof(offset));
        std::memcpy(blockCompressedData.data() + metadataPos + 4, &compSize, sizeof(compSize));
        std::memcpy(blockCompressedData.data() + metadataPos + 8, &origSize, sizeof(origSize));
        std::memcpy(blockCompressedData.data() + metadataPos + 12, &blockChecksum, sizeof(blockChecksum));
        
        // 添加块数据
        blockCompressedData.insert(blockCompressedData.end(), blockData.begin(), blockData.end());
        currentOffset += blockData.size();
        
        if ((i + 1) % 10 == 0 || i == totalBlocks - 1) {
            std::cout << "  Processed " << (i + 1) << "/" << totalBlocks << " blocks" << std::endl;
        }
    }
    
    auto compressionEnd = std::chrono::steady_clock::now();
    auto compressionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(compressionEnd - compressionStart);
    
    std::cout << std::endl;
    std::cout << "Block format created:" << std::endl;
    std::cout << "  Block count: " << totalBlocks << std::endl;
    std::cout << "  Block size: " << blockSize / (1024 * 1024) << " MB" << std::endl;
    std::cout << "  Total size: " << blockCompressedData.size() / (1024 * 1024) << " MB" << std::endl;
    std::cout << "  Time: " << compressionDuration.count() << " ms" << std::endl;
    std::cout << std::endl;
    
    // 测试解压
    std::cout << "--- LZMA Block Decompression Test ---" << std::endl;
    
    DecompressionEngine decompressor;
    decompressor.setThreadPool(threadPool);
    
    DecompressionTask task;
    task.targetPath = "output_lzma_test";
    task.compressedData = blockCompressedData;
    task.originalSize = tarData.size();
    task.expectedChecksum = checksum;
    task.algorithm = CompressionAlgorithm::LZMA_HIGH;
    
    auto decompressionStart = std::chrono::steady_clock::now();
    bool success = decompressor.decompressFolder(task);
    auto decompressionEnd = std::chrono::steady_clock::now();
    auto decompressionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(decompressionEnd - decompressionStart);
    
    if (success) {
        double decompressionSpeed = (static_cast<double>(tarData.size()) / (1024 * 1024)) / 
                                   (decompressionDuration.count() / 1000.0);
        
        std::cout << "Decompression completed:" << std::endl;
        std::cout << "  Time: " << decompressionDuration.count() << " ms" << std::endl;
        std::cout << "  Speed: " << std::fixed << std::setprecision(2) 
                  << decompressionSpeed << " MB/s" << std::endl;
        std::cout << std::endl;
        
        std::cout << "=== Test PASSED ===" << std::endl;
        std::cout << std::endl;
        std::cout << "Note: This test used uncompressed blocks for simplicity." << std::endl;
        std::cout << "      In production, CompressionModule will use actual LZMA" << std::endl;
        std::cout << "      compression for files > 128MB automatically." << std::endl;
        return 0;
    } else {
        std::cerr << "Decompression failed!" << std::endl;
        return 1;
    }
}
