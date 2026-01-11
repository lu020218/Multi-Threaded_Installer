#include <rapidcheck.h>
#include "packager/metadata_generator.h"
#include "installer/metadata_parser.h"
#include "packager/compression_module.h"
#include "packager/folder_scanner.h"
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

// 生成随机文件名
std::string generateRandomFileName() {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.size() - 1);
    std::uniform_int_distribution<> lengthDis(3, 15);
    
    int length = lengthDis(gen);
    std::string result;
    result.reserve(length);
    
    for (int i = 0; i < length; ++i) {
        result += chars[dis(gen)];
    }
    
    return result + ".txt";
}

// 生成随机文件夹名
std::string generateRandomFolderName() {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.size() - 1);
    std::uniform_int_distribution<> lengthDis(3, 10);
    
    int length = lengthDis(gen);
    std::string result;
    result.reserve(length);
    
    for (int i = 0; i < length; ++i) {
        result += chars[dis(gen)];
    }
    
    return result;
}

// 创建测试文件夹
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
    std::cout << "Running Metadata Management Property-Based Tests" << std::endl;
    
    // **功能：multi-threaded-installer，属性 4：元数据映射一致性**
    // **验证：需求 1.4**
    auto metadataMappingConsistencyProperty = rc::check("Metadata mapping consistency", []() {
        // 生成多个不同的文件夹进行压缩
        auto folderCount = *rc::gen::inRange(1, 4);  // 1-4个文件夹
        std::vector<std::string> testFolders;
        std::vector<FolderInfo> folderInfos;
        std::vector<CompressionResult> compressionResults;
        
        try {
            // 创建测试文件夹和压缩结果
            CompressionModule compressor;
            compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
            
            for (int i = 0; i < folderCount; ++i) {
                // 为每个文件夹生成随机文件
                auto fileCount = *rc::gen::inRange(1, 3);  // 1-3个文件
                std::vector<std::pair<std::string, std::vector<uint8_t>>> testFiles;
                
                for (int j = 0; j < fileCount; ++j) {
                    auto fileName = generateRandomFileName();
                    auto fileSize = *rc::gen::inRange(100, 2000);  // 100B-2KB文件
                    auto fileContent = generateRandomFileContent(fileSize);
                    testFiles.emplace_back(fileName, fileContent);
                }
                
                // 创建测试文件夹
                std::string testFolderName = "metadata_test_" + std::to_string(i) + "_" + 
                                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                std::string folderPath = createTestFolder(testFolderName, testFiles);
                testFolders.push_back(folderPath);
                
                // 创建FolderInfo
                std::string targetPath = "target_" + std::to_string(i);
                FolderInfo folderInfo = createFolderInfo(folderPath, targetPath);
                folderInfos.push_back(folderInfo);
                
                // 压缩文件夹
                auto compressionResult = compressor.compressFolder(folderInfo);
                RC_ASSERT(!compressionResult.compressedData.empty());
                RC_ASSERT(compressionResult.originalSize > 0);
                RC_ASSERT(compressionResult.compressedSize > 0);
                compressionResults.push_back(compressionResult);
            }
            
            // 生成元数据
            MetadataGenerator generator;
            InstallationMetadata metadata = generator.generateMetadata(compressionResults, folderInfos);
            
            // 验证元数据的基本一致性
            RC_ASSERT(metadata.version == Constants::VERSION);
            RC_ASSERT(metadata.folderCount == static_cast<uint32_t>(folderCount));
            RC_ASSERT(metadata.folderMappings.size() == static_cast<size_t>(folderCount));
            
            // 验证文件夹映射的一致性
            uint64_t expectedTotalSize = 0;
            uint64_t currentOffset = 0;
            
            for (size_t i = 0; i < metadata.folderMappings.size(); ++i) {
                const auto& mapping = metadata.folderMappings[i];
                const auto& originalFolderInfo = folderInfos[i];
                const auto& originalCompressionResult = compressionResults[i];
                
                // 验证映射信息与原始数据一致
                RC_ASSERT(mapping.folderName == originalFolderInfo.targetPath);
                RC_ASSERT(mapping.targetPath == originalFolderInfo.targetPath);
                RC_ASSERT(mapping.offset == currentOffset);
                RC_ASSERT(mapping.compressedSize == originalCompressionResult.compressedSize);
                RC_ASSERT(mapping.originalSize == originalCompressionResult.originalSize);
                RC_ASSERT(mapping.checksum == originalCompressionResult.checksum);
                RC_ASSERT(mapping.algorithm == originalCompressionResult.algorithm);
                
                expectedTotalSize += mapping.compressedSize;
                currentOffset += mapping.compressedSize;
            }
            
            // 验证总压缩大小一致
            RC_ASSERT(metadata.totalCompressedSize == expectedTotalSize);
            
            // 序列化元数据
            std::vector<uint8_t> serializedMetadata = generator.serializeMetadata(metadata);
            RC_ASSERT(!serializedMetadata.empty());
            RC_ASSERT(serializedMetadata.size() >= sizeof(BinaryMetadata));
            
            // 验证序列化数据的魔数
            const BinaryMetadata* header = reinterpret_cast<const BinaryMetadata*>(serializedMetadata.data());
            RC_ASSERT(header->magic == Constants::MAGIC_NUMBER);
            RC_ASSERT(header->version == Constants::VERSION);
            RC_ASSERT(header->folderCount == static_cast<uint32_t>(folderCount));
            
            // 使用MetadataParser反序列化并验证一致性
            MetadataParser parser;
            InstallationMetadata deserializedMetadata = parser.deserializeMetadata(serializedMetadata);
            
            // 验证反序列化的元数据与原始元数据一致
            RC_ASSERT(deserializedMetadata.version == metadata.version);
            RC_ASSERT(deserializedMetadata.folderCount == metadata.folderCount);
            RC_ASSERT(deserializedMetadata.folderMappings.size() == metadata.folderMappings.size());
            RC_ASSERT(deserializedMetadata.totalCompressedSize == metadata.totalCompressedSize);
            
            // 验证每个文件夹映射的一致性
            for (size_t i = 0; i < metadata.folderMappings.size(); ++i) {
                const auto& original = metadata.folderMappings[i];
                const auto& deserialized = deserializedMetadata.folderMappings[i];
                
                RC_ASSERT(deserialized.folderName == original.folderName);
                RC_ASSERT(deserialized.targetPath == original.targetPath);
                RC_ASSERT(deserialized.offset == original.offset);
                RC_ASSERT(deserialized.compressedSize == original.compressedSize);
                RC_ASSERT(deserialized.originalSize == original.originalSize);
                RC_ASSERT(deserialized.checksum == original.checksum);
                RC_ASSERT(deserialized.algorithm == original.algorithm);
            }
            
            // 验证元数据有效性
            bool isValid = parser.validateMetadata(deserializedMetadata);
            RC_ASSERT(isValid);
            
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
    
    // 运行测试并报告结果
    if (metadataMappingConsistencyProperty.succeed) {
        std::cout << "✓ Metadata mapping consistency property passed (" 
                  << metadataMappingConsistencyProperty.numSuccess << " tests)" << std::endl;
        return 0;
    } else {
        std::cout << "✗ Metadata mapping consistency property failed" << std::endl;
        if (!metadataMappingConsistencyProperty.failure.counterExample.empty()) {
            std::cout << "Counter-example: " << metadataMappingConsistencyProperty.failure.counterExample << std::endl;
        }
        if (!metadataMappingConsistencyProperty.failure.reason.empty()) {
            std::cout << "Reason: " << metadataMappingConsistencyProperty.failure.reason << std::endl;
        }
        return 1;
    }
}