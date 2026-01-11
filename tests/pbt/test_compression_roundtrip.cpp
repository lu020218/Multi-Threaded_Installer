#include <rapidcheck.h>
#include "packager/compression_module.h"
#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include <filesystem>
#include <fstream>
#include <random>

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

// 比较两个文件夹的内容
bool compareFolders(const std::string& folder1, const std::string& folder2) {
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder1)) {
            if (entry.is_regular_file()) {
                std::string relativePath = std::filesystem::relative(entry.path(), folder1).string();
                std::string correspondingFile = folder2 + "/" + relativePath;
                
                if (!std::filesystem::exists(correspondingFile)) {
                    return false;
                }
                
                // 比较文件大小
                if (std::filesystem::file_size(entry.path()) != std::filesystem::file_size(correspondingFile)) {
                    return false;
                }
                
                // 比较文件内容
                std::ifstream file1(entry.path(), std::ios::binary);
                std::ifstream file2(correspondingFile, std::ios::binary);
                
                if (!file1 || !file2) {
                    return false;
                }
                
                std::vector<uint8_t> content1((std::istreambuf_iterator<char>(file1)), std::istreambuf_iterator<char>());
                std::vector<uint8_t> content2((std::istreambuf_iterator<char>(file2)), std::istreambuf_iterator<char>());
                
                if (content1 != content2) {
                    return false;
                }
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error comparing folders: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Running Property-Based Tests for Multi-Threaded Installer" << std::endl;
    
    // **功能：multi-threaded-installer，属性 3：压缩往返完整性**
    // **验证：需求 1.3, 6.4**
    auto compressionRoundtripProperty = rc::check("Compression roundtrip integrity", []() {
        // 生成随机测试数据
        auto fileCount = *rc::gen::inRange(1, 10);  // 1-10个文件
        std::vector<std::pair<std::string, std::vector<uint8_t>>> testFiles;
        
        for (int i = 0; i < fileCount; ++i) {
            auto fileName = "file" + std::to_string(i) + ".txt";
            auto fileSize = *rc::gen::inRange(1, 10000);  // 1-10KB文件
            auto fileContent = generateRandomFileContent(fileSize);
            testFiles.emplace_back(fileName, fileContent);
        }
        
        // 创建测试文件夹
        std::string testFolderName = "test_folder_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::string originalFolder = createTestFolder(testFolderName, testFiles);
        
        try {
            // 创建FolderInfo
            FolderInfo folderInfo;
            folderInfo.sourcePath = originalFolder;
            folderInfo.targetPath = "test_target";
            
            // 扫描文件夹以填充文件列表
            for (const auto& entry : std::filesystem::recursive_directory_iterator(originalFolder)) {
                if (entry.is_regular_file()) {
                    folderInfo.files.push_back(entry.path().string());
                }
            }
            folderInfo.totalSize = std::accumulate(testFiles.begin(), testFiles.end(), 0ULL,
                [](size_t sum, const auto& file) { return sum + file.second.size(); });
            
            // 压缩文件夹
            CompressionModule compressor;
            compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
            
            auto compressionResult = compressor.compressFolder(folderInfo);
            RC_ASSERT(!compressionResult.compressedData.empty());
            RC_ASSERT(compressionResult.originalSize > 0);
            RC_ASSERT(compressionResult.compressedSize > 0);
            RC_ASSERT(compressionResult.checksum != 0);
            
            // 创建解压任务
            DecompressionTask task;
            task.compressedData = compressionResult.compressedData;
            task.expectedChecksum = compressionResult.checksum;
            task.originalSize = compressionResult.originalSize;
            task.algorithm = compressionResult.algorithm;
            
            // 创建目标文件夹
            std::string decompressedFolder = std::filesystem::temp_directory_path().string() + "/decompressed_" + testFolderName;
            task.targetPath = decompressedFolder;
            
            // 解压文件夹
            DecompressionEngine decompressor;
            bool decompressSuccess = decompressor.decompressFolder(task);
            RC_ASSERT(decompressSuccess);
            
            // 验证解压后的文件夹与原始文件夹内容相同
            bool foldersMatch = compareFolders(originalFolder, decompressedFolder);
            RC_ASSERT(foldersMatch);
            
            // 清理测试数据
            cleanupTestFolder(originalFolder);
            cleanupTestFolder(decompressedFolder);
            
        } catch (const std::exception& e) {
            // 确保清理测试数据
            cleanupTestFolder(originalFolder);
            std::cerr << "Test exception: " << e.what() << std::endl;
            RC_FAIL("Test failed with exception");
        }
    });
    
    if (compressionRoundtripProperty.succeed) {
        std::cout << "✓ Compression roundtrip integrity property passed (" 
                  << compressionRoundtripProperty.numSuccess << " tests)" << std::endl;
        return 0;
    } else {
        std::cout << "✗ Compression roundtrip integrity property failed" << std::endl;
        if (!compressionRoundtripProperty.failure.counterExample.empty()) {
            std::cout << "Counter-example: " << compressionRoundtripProperty.failure.counterExample << std::endl;
        }
        if (!compressionRoundtripProperty.failure.reason.empty()) {
            std::cout << "Reason: " << compressionRoundtripProperty.failure.reason << std::endl;
        }
        return 1;
    }
}