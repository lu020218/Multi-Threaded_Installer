#include "packager/compression_module.h"
#include "installer/decompression_engine.h"
#include "installer/thread_pool_manager.h"
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

using namespace MultiThreadedInstaller;

// 创建测试文件夹
bool createTestFolder(const std::string& folderPath, size_t fileSize, size_t fileCount) {
    std::filesystem::create_directories(folderPath);
    
    for (size_t i = 0; i < fileCount; ++i) {
        std::string filePath = folderPath + "/test_file_" + std::to_string(i) + ".dat";
        std::ofstream file(filePath, std::ios::binary);
        
        if (!file) {
            std::cerr << "Failed to create test file: " << filePath << std::endl;
            return false;
        }
        
        // 写入随机数据
        std::vector<uint8_t> data(fileSize);
        for (size_t j = 0; j < fileSize; ++j) {
            data[j] = static_cast<uint8_t>((i * fileSize + j) % 256);
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    
    return true;
}

int main() {
    std::cout << "=== ZSTD Block Compression/Decompression Test ===" << std::endl;
    
    // 创建测试文件夹
    std::string testFolder = "test_data_zstd_blocks";
    size_t fileSize = 16 * 1024 * 1024; // 16MB per file
    size_t fileCount = 10; // 10 files = 160MB total (> 128MB threshold)
    
    std::cout << "\n1. Creating test folder with " << fileCount << " files (" 
              << (fileSize * fileCount / (1024 * 1024)) << " MB total)..." << std::endl;
    
    if (!createTestFolder(testFolder, fileSize, fileCount)) {
        std::cerr << "Failed to create test folder" << std::endl;
        return 1;
    }
    
    // 扫描文件夹
    FolderInfo folderInfo;
    folderInfo.sourcePath = testFolder;
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(testFolder)) {
        if (entry.is_regular_file()) {
            folderInfo.files.push_back(entry.path().string());
        }
    }
    
    std::cout << "   Found " << folderInfo.files.size() << " files" << std::endl;
    
    // 压缩测试
    std::cout << "\n2. Compressing with ZSTD (block compression for large files)..." << std::endl;
    
    CompressionModule compressor;
    compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
    compressor.setCompressionLevel(3);
    compressor.setBlockSize(1 * 1024 * 1024); // 1MB blocks
    
    auto compressStart = std::chrono::steady_clock::now();
    CompressionResult result = compressor.compressFolder(folderInfo);
    auto compressEnd = std::chrono::steady_clock::now();
    
    if (result.compressedSize == 0) {
        std::cerr << "Compression failed" << std::endl;
        return 1;
    }
    
    auto compressDuration = std::chrono::duration_cast<std::chrono::milliseconds>(compressEnd - compressStart);
    double compressionRatio = static_cast<double>(result.compressedSize) / result.originalSize;
    double compressionSpeed = (result.originalSize / (1024.0 * 1024.0)) / (compressDuration.count() / 1000.0);
    
    std::cout << "   Original size: " << (result.originalSize / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "   Compressed size: " << (result.compressedSize / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "   Compression ratio: " << (compressionRatio * 100) << "%" << std::endl;
    std::cout << "   Compression time: " << compressDuration.count() << " ms" << std::endl;
    std::cout << "   Compression speed: " << compressionSpeed << " MB/s" << std::endl;
    
    // 检查是否使用了块压缩格式
    bool isBlockFormat = false;
    if (result.compressedData.size() >= 4) {
        uint32_t firstBytes;
        std::memcpy(&firstBytes, result.compressedData.data(), sizeof(firstBytes));
        // ZSTD magic number is 0xFD2FB528
        if (firstBytes != 0x28B52FFD) {
            isBlockFormat = true;
            std::cout << "   Format: Block-based (custom format)" << std::endl;
        } else {
            std::cout << "   Format: Standard ZSTD" << std::endl;
        }
    }
    
    // 解压测试
    std::cout << "\n3. Decompressing..." << std::endl;
    
    std::string outputFolder = "test_output_zstd_blocks";
    std::filesystem::create_directories(outputFolder);
    
    // 创建线程池以启用并行解压
    auto threadPool = std::make_shared<ThreadPoolManager>(std::thread::hardware_concurrency());
    std::cout << "   Thread pool created with " << threadPool->getTotalThreadCount() << " threads" << std::endl;
    
    DecompressionEngine decompressor;
    decompressor.setThreadPool(threadPool);
    
    // 创建解压任务
    DecompressionTask task;
    task.compressedData = result.compressedData;
    task.targetPath = outputFolder;
    task.algorithm = CompressionAlgorithm::ZSTD_FAST;
    task.originalSize = result.originalSize;
    task.expectedChecksum = result.checksum; // 设置校验和
    
    auto decompressStart = std::chrono::steady_clock::now();
    bool decompressSuccess = decompressor.decompressFolder(task);
    auto decompressEnd = std::chrono::steady_clock::now();
    
    if (!decompressSuccess) {
        std::cerr << "Decompression failed" << std::endl;
        return 1;
    }
    
    auto decompressDuration = std::chrono::duration_cast<std::chrono::milliseconds>(decompressEnd - decompressStart);
    double decompressionSpeed = (result.originalSize / (1024.0 * 1024.0)) / (decompressDuration.count() / 1000.0);
    
    std::cout << "   Decompression time: " << decompressDuration.count() << " ms" << std::endl;
    std::cout << "   Decompression speed: " << decompressionSpeed << " MB/s" << std::endl;
    
    // 验证文件
    std::cout << "\n4. Verifying decompressed files..." << std::endl;
    
    size_t verifiedFiles = 0;
    size_t totalFiles = 0;
    
    for (const auto& originalFile : folderInfo.files) {
        totalFiles++;
        
        // 构建输出文件路径
        std::string relativePath = originalFile.substr(testFolder.length());
        if (!relativePath.empty() && (relativePath[0] == '/' || relativePath[0] == '\\')) {
            relativePath = relativePath.substr(1);
        }
        std::string outputFile = outputFolder + "/" + relativePath;
        
        // 读取原始文件
        std::ifstream origStream(originalFile, std::ios::binary);
        std::vector<uint8_t> origData((std::istreambuf_iterator<char>(origStream)),
                                      std::istreambuf_iterator<char>());
        
        // 读取解压文件
        std::ifstream outStream(outputFile, std::ios::binary);
        if (!outStream) {
            std::cerr << "   Failed to open decompressed file: " << outputFile << std::endl;
            continue;
        }
        
        std::vector<uint8_t> outData((std::istreambuf_iterator<char>(outStream)),
                                     std::istreambuf_iterator<char>());
        
        // 比较
        if (origData == outData) {
            verifiedFiles++;
        } else {
            std::cerr << "   File mismatch: " << relativePath << std::endl;
        }
    }
    
    std::cout << "   Verified: " << verifiedFiles << "/" << totalFiles << " files" << std::endl;
    
    // 清理
    std::cout << "\n5. Cleaning up..." << std::endl;
    std::filesystem::remove_all(testFolder);
    std::filesystem::remove_all(outputFolder);
    
    // 总结
    std::cout << "\n=== Test Summary ===" << std::endl;
    if (verifiedFiles == totalFiles) {
        std::cout << "✓ All tests passed!" << std::endl;
        std::cout << "✓ Block compression format: " << (isBlockFormat ? "YES" : "NO (standard ZSTD)") << std::endl;
        std::cout << "✓ Compression speed: " << compressionSpeed << " MB/s" << std::endl;
        std::cout << "✓ Decompression speed: " << decompressionSpeed << " MB/s" << std::endl;
        return 0;
    } else {
        std::cerr << "✗ Test failed: " << (totalFiles - verifiedFiles) << " files did not match" << std::endl;
        return 1;
    }
}
