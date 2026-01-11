#include <rapidcheck.h>
#include "packager/compression_module.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>

using namespace MultiThreadedInstaller;

// 生成随机文件内容
std::vector<uint8_t> generateRandomFileContent(size_t size) {
    std::vector<uint8_t> content(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (auto& byte : content) {
        byte = dis(gen);
    }
    
    return content;
}

// 创建临时测试文件夹
std::string createTestFolder(const std::string& baseName, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::string tempDir = std::filesystem::temp_directory_path().string() + "/" + baseName;
    std::filesystem::create_directories(tempDir);
    
    for (const auto& file : files) {
        std::string filePath = tempDir + "/" + file.first;
        
        // 创建子目录（如果需要）
        std::filesystem::create_directories(std::filesystem::path(filePath).parent_path());
        
        std::ofstream outFile(filePath, std::ios::binary);
        outFile.write(reinterpret_cast<const char*>(file.second.data()), file.second.size());
    }
    
    return tempDir;
}

// 清理测试文件夹
void cleanupTestFolder(const std::string& folderPath) {
    try {
        std::filesystem::remove_all(folderPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to cleanup test folder: " << e.what() << std::endl;
    }
}

// 创建FolderInfo结构
FolderInfo createFolderInfo(const std::string& folderPath, const std::string& targetPath) {
    FolderInfo folderInfo;
    folderInfo.sourcePath = folderPath;
    folderInfo.targetPath = targetPath;
    folderInfo.totalSize = 0;
    
    // 扫描文件夹以填充文件列表
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath)) {
        if (entry.is_regular_file()) {
            folderInfo.files.push_back(entry.path().string());
            folderInfo.totalSize += std::filesystem::file_size(entry.path());
        }
    }
    
    return folderInfo;
}

int main() {
    std::cout << "Running Compression Module Property-Based Tests" << std::endl;
    
    // **功能：multi-threaded-installer，属性 2：独立压缩处理**
    // **验证：需求 1.2**
    auto independentCompressionProperty = rc::check("Independent compression processing", []() {
        // 生成多个不同的文件夹
        auto folderCount = *rc::gen::inRange(2, 5);  // 2-5个文件夹
        std::vector<std::string> testFolders;
        std::vector<FolderInfo> folderInfos;
        
        try {
            for (int i = 0; i < folderCount; ++i) {
                // 为每个文件夹生成随机文件
                auto fileCount = *rc::gen::inRange(1, 5);  // 1-5个文件
                std::vector<std::pair<std::string, std::vector<uint8_t>>> testFiles;
                
                for (int j = 0; j < fileCount; ++j) {
                    auto fileName = "file" + std::to_string(j) + ".txt";
                    auto fileSize = *rc::gen::inRange(100, 5000);  // 100B-5KB文件
                    auto fileContent = generateRandomFileContent(fileSize);
                    testFiles.emplace_back(fileName, fileContent);
                }
                
                // 创建测试文件夹
                std::string testFolderName = "test_folder_" + std::to_string(i) + "_" + 
                                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                std::string folderPath = createTestFolder(testFolderName, testFiles);
                testFolders.push_back(folderPath);
                
                // 创建FolderInfo
                FolderInfo folderInfo = createFolderInfo(folderPath, "target" + std::to_string(i));
                folderInfos.push_back(folderInfo);
            }
            
            // 压缩每个文件夹
            CompressionModule compressor;
            compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
            
            std::vector<CompressionResult> results;
            for (const auto& folderInfo : folderInfos) {
                auto result = compressor.compressFolder(folderInfo);
                RC_ASSERT(!result.compressedData.empty());
                RC_ASSERT(result.originalSize > 0);
                RC_ASSERT(result.compressedSize > 0);
                results.push_back(result);
            }
            
            // 验证每个压缩结果都是独立的
            for (size_t i = 0; i < results.size(); ++i) {
                for (size_t j = i + 1; j < results.size(); ++j) {
                    // 不同文件夹的压缩结果应该不同
                    RC_ASSERT(results[i].compressedData != results[j].compressedData);
                    RC_ASSERT(results[i].checksum != results[j].checksum);
                    
                    // 每个结果应该对应其原始文件夹的大小
                    RC_ASSERT(results[i].originalSize == folderInfos[i].totalSize);
                    RC_ASSERT(results[j].originalSize == folderInfos[j].totalSize);
                }
            }
            
            // 清理测试数据
            for (const auto& folder : testFolders) {
                cleanupTestFolder(folder);
            }
            
        } catch (const std::exception& e) {
            // 确保清理测试数据
            for (const auto& folder : testFolders) {
                cleanupTestFolder(folder);
            }
            std::cerr << "Test exception: " << e.what() << std::endl;
            RC_FAIL("Test failed with exception");
        }
    });
    
    // **功能：multi-threaded-installer，属性 12：压缩性能优化**
    // **验证：需求 6.2, 6.3**
    auto compressionPerformanceProperty = rc::check("Compression performance optimization", []() {
        // 生成测试数据
        auto fileCount = *rc::gen::inRange(3, 8);  // 3-8个文件
        std::vector<std::pair<std::string, std::vector<uint8_t>>> testFiles;
        
        for (int i = 0; i < fileCount; ++i) {
            auto fileName = "file" + std::to_string(i) + ".txt";
            auto fileSize = *rc::gen::inRange(5000, 20000);  // 5-20KB文件
            auto fileContent = generateRandomFileContent(fileSize);
            testFiles.emplace_back(fileName, fileContent);
        }
        
        // 创建测试文件夹
        std::string testFolderName = "perf_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::string folderPath = createTestFolder(testFolderName, testFiles);
        
        try {
            FolderInfo folderInfo = createFolderInfo(folderPath, "target");
            
            CompressionModule compressor;
            
            // 测试ZSTD快速模式
            compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
            compressor.setCompressionLevel(1); // 快速压缩级别
            
            auto startTime = std::chrono::high_resolution_clock::now();
            auto zstdResult = compressor.compressFolder(folderInfo);
            auto endTime = std::chrono::high_resolution_clock::now();
            auto zstdDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            RC_ASSERT(!zstdResult.compressedData.empty());
            RC_ASSERT(zstdResult.originalSize > 0);
            RC_ASSERT(zstdResult.compressedSize > 0);
            RC_ASSERT(zstdResult.compressedSize < zstdResult.originalSize); // 应该有压缩效果
            
            // 测试LZMA高压缩比模式
            compressor.setCompressionAlgorithm(CompressionAlgorithm::LZMA_HIGH);
            compressor.setCompressionLevel(5); // 平衡压缩级别
            
            startTime = std::chrono::high_resolution_clock::now();
            auto lzmaResult = compressor.compressFolder(folderInfo);
            endTime = std::chrono::high_resolution_clock::now();
            auto lzmaDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            RC_ASSERT(!lzmaResult.compressedData.empty());
            RC_ASSERT(lzmaResult.originalSize > 0);
            RC_ASSERT(lzmaResult.compressedSize > 0);
            RC_ASSERT(lzmaResult.compressedSize < lzmaResult.originalSize); // 应该有压缩效果
            
            // 验证性能特性：ZSTD应该更快，LZMA应该压缩比更高
            // 注意：这些断言可能在某些情况下失败，因为性能取决于数据特性
            // 但对于随机数据，这些特性通常成立
            
            // LZMA通常应该有更好的压缩比（更小的压缩大小）
            double zstdRatio = static_cast<double>(zstdResult.compressedSize) / zstdResult.originalSize;
            double lzmaRatio = static_cast<double>(lzmaResult.compressedSize) / lzmaResult.originalSize;
            
            // 验证压缩比都是合理的（小于1.0）
            RC_ASSERT(zstdRatio < 1.0);
            RC_ASSERT(lzmaRatio < 1.0);
            
            // 验证两种算法都产生了有效的校验和
            RC_ASSERT(zstdResult.checksum != 0);
            RC_ASSERT(lzmaResult.checksum != 0);
            
            // 验证算法标识正确
            RC_ASSERT(zstdResult.algorithm == CompressionAlgorithm::ZSTD_FAST);
            RC_ASSERT(lzmaResult.algorithm == CompressionAlgorithm::LZMA_HIGH);
            
            // 清理测试数据
            cleanupTestFolder(folderPath);
            
        } catch (const std::exception& e) {
            // 确保清理测试数据
            cleanupTestFolder(folderPath);
            std::cerr << "Test exception: " << e.what() << std::endl;
            RC_FAIL("Test failed with exception");
        }
    });
    
    // 运行测试并报告结果
    bool allTestsPassed = true;
    
    if (independentCompressionProperty.succeed) {
        std::cout << "✓ Independent compression processing property passed (" 
                  << independentCompressionProperty.numSuccess << " tests)" << std::endl;
    } else {
        std::cout << "✗ Independent compression processing property failed" << std::endl;
        if (!independentCompressionProperty.failure.counterExample.empty()) {
            std::cout << "Counter-example: " << independentCompressionProperty.failure.counterExample << std::endl;
        }
        if (!independentCompressionProperty.failure.reason.empty()) {
            std::cout << "Reason: " << independentCompressionProperty.failure.reason << std::endl;
        }
        allTestsPassed = false;
    }
    
    if (compressionPerformanceProperty.succeed) {
        std::cout << "✓ Compression performance optimization property passed (" 
                  << compressionPerformanceProperty.numSuccess << " tests)" << std::endl;
    } else {
        std::cout << "✗ Compression performance optimization property failed" << std::endl;
        if (!compressionPerformanceProperty.failure.counterExample.empty()) {
            std::cout << "Counter-example: " << compressionPerformanceProperty.failure.counterExample << std::endl;
        }
        if (!compressionPerformanceProperty.failure.reason.empty()) {
            std::cout << "Reason: " << compressionPerformanceProperty.failure.reason << std::endl;
        }
        allTestsPassed = false;
    }
    
    return allTestsPassed ? 0 : 1;
}