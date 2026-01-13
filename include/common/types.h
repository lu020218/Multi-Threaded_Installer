#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace MultiThreadedInstaller {

// 压缩算法枚举
enum class CompressionAlgorithm {
    ZSTD_FAST,    // Zstandard快速模式
    LZMA_HIGH     // 7z LZMA高压缩比模式
};

// 文件夹信息结构
struct FolderInfo {
    std::string sourcePath;
    std::string targetPath;
    std::vector<std::string> files;
    size_t totalSize;
    
    FolderInfo() : totalSize(0) {}
    
    FolderInfo(const std::string& source, const std::string& target)
        : sourcePath(source), targetPath(target), totalSize(0) {}
};

// 压缩结果结构
struct CompressionResult {
    std::vector<uint8_t> compressedData;
    uint32_t checksum;
    size_t originalSize;
    size_t compressedSize;
    CompressionAlgorithm algorithm;
    
    CompressionResult() : checksum(0), originalSize(0), compressedSize(0), 
                         algorithm(CompressionAlgorithm::ZSTD_FAST) {}
};

// 文件夹映射结构
struct FolderMapping {
    std::string folderName;
    std::string targetPath;
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint32_t checksum;
    CompressionAlgorithm algorithm;
    
    FolderMapping() : offset(0), compressedSize(0), originalSize(0), 
                     checksum(0), algorithm(CompressionAlgorithm::ZSTD_FAST) {}
};

// 安装元数据结构
struct InstallationMetadata {
    uint32_t version;
    uint32_t folderCount;
    std::vector<FolderMapping> folderMappings;
    uint64_t totalCompressedSize;
    
    InstallationMetadata() : version(1), folderCount(0), totalCompressedSize(0) {}
};

// 二进制元数据头结构
struct BinaryMetadata {
    uint32_t magic;           // 魔数标识: 0x4D544950 ("MTIP")
    uint32_t version;         // 版本号
    uint32_t folderCount;     // 文件夹数量
    uint64_t metadataSize;    // 元数据总大小
    uint64_t dataOffset;      // 压缩数据起始偏移
    
    BinaryMetadata() : magic(0x4D544950), version(1), folderCount(0), 
                      metadataSize(0), dataOffset(0) {}
};

// 解压任务结构
struct DecompressionTask {
    std::vector<uint8_t> compressedData;
    std::string targetPath;
    uint32_t expectedChecksum;
    size_t originalSize;
    CompressionAlgorithm algorithm;
    
    DecompressionTask() : expectedChecksum(0), originalSize(0), 
                         algorithm(CompressionAlgorithm::ZSTD_FAST) {}
};

// 进度回调函数类型
using ProgressCallback = std::function<void(const std::string&, float)>;

// 常量定义
namespace Constants {
    constexpr uint32_t MAGIC_NUMBER = 0x4D544950;  // "MTIP"
    constexpr uint32_t VERSION = 1;
    
    // 块大小配置 (优化后)
    constexpr size_t DEFAULT_BLOCK_SIZE = 2 * 1024 * 1024;  // 2MB (从 64KB 优化)
    constexpr size_t MIN_BLOCK_SIZE = 1 * 1024 * 1024;      // 1MB
    constexpr size_t MAX_BLOCK_SIZE = 8 * 1024 * 1024;      // 8MB
    
    constexpr int DEFAULT_ZSTD_LEVEL = 1;          // 快速压缩
    constexpr int DEFAULT_LZMA_LEVEL = 5;          // 平衡压缩
}

} // namespace MultiThreadedInstaller