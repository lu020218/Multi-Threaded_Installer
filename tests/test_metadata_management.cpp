#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include "packager/metadata_generator.h"
#include "installer/metadata_parser.h"
#include "packager/compression_module.h"

using namespace MultiThreadedInstaller;

// 创建测试文件夹
std::string createTestFolder(const std::string& baseName, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::string tempDir = std::filesystem::temp_directory_path().string() + "/" + baseName;
    std::filesystem::create_directories(tempDir);
    
    for (const auto& file : files) {
        std::string filePath = tempDir + "/" + file.first;
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
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folderPath)) {
        if (entry.is_regular_file()) {
            folderInfo.files.push_back(entry.path().string());
            folderInfo.totalSize += std::filesystem::file_size(entry.path());
        }
    }
    
    return folderInfo;
}

// **功能：multi-threaded-installer，属性 4：元数据映射一致性**
// **验证：需求 1.4**
bool testMetadataMappingConsistency() {
    std::cout << "Testing metadata mapping consistency..." << std::endl;
    
    std::vector<std::string> testFolders;
    
    try {
        // 创建测试数据
        std::vector<FolderInfo> folderInfos;
        std::vector<CompressionResult> compressionResults;
        
        CompressionModule compressor;
        compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
        
        // 创建两个测试文件夹
        for (int i = 0; i < 2; ++i) {
            std::vector<std::pair<std::string, std::vector<uint8_t>>> testFiles;
            
            // 创建测试文件
            std::string fileName = "test_file_" + std::to_string(i) + ".txt";
            std::vector<uint8_t> fileContent(1000, static_cast<uint8_t>('A' + i));
            testFiles.emplace_back(fileName, fileContent);
            
            // 创建测试文件夹
            std::string testFolderName = "metadata_test_" + std::to_string(i);
            std::string folderPath = createTestFolder(testFolderName, testFiles);
            testFolders.push_back(folderPath);
            
            // 创建FolderInfo
            std::string targetPath = "target_" + std::to_string(i);
            FolderInfo folderInfo = createFolderInfo(folderPath, targetPath);
            folderInfos.push_back(folderInfo);
            
            // 压缩文件夹
            auto compressionResult = compressor.compressFolder(folderInfo);
            assert(!compressionResult.compressedData.empty());
            assert(compressionResult.originalSize > 0);
            assert(compressionResult.compressedSize > 0);
            compressionResults.push_back(compressionResult);
        }
        
        // 生成元数据
        MetadataGenerator generator;
        InstallationMetadata metadata = generator.generateMetadata(compressionResults, folderInfos);
        
        // 验证元数据的基本一致性
        assert(metadata.version == Constants::VERSION);
        assert(metadata.folderCount == 2);
        assert(metadata.folderMappings.size() == 2);
        
        // 验证文件夹映射的一致性
        uint64_t expectedTotalSize = 0;
        uint64_t currentOffset = 0;
        
        for (size_t i = 0; i < metadata.folderMappings.size(); ++i) {
            const auto& mapping = metadata.folderMappings[i];
            const auto& originalFolderInfo = folderInfos[i];
            const auto& originalCompressionResult = compressionResults[i];
            
            // 验证映射信息与原始数据一致
            assert(mapping.folderName == originalFolderInfo.targetPath);
            assert(mapping.targetPath == originalFolderInfo.targetPath);
            assert(mapping.offset == currentOffset);
            assert(mapping.compressedSize == originalCompressionResult.compressedSize);
            assert(mapping.originalSize == originalCompressionResult.originalSize);
            assert(mapping.checksum == originalCompressionResult.checksum);
            assert(mapping.algorithm == originalCompressionResult.algorithm);
            
            expectedTotalSize += mapping.compressedSize;
            currentOffset += mapping.compressedSize;
        }
        
        // 验证总压缩大小一致
        assert(metadata.totalCompressedSize == expectedTotalSize);
        
        // 序列化元数据
        std::vector<uint8_t> serializedMetadata = generator.serializeMetadata(metadata);
        assert(!serializedMetadata.empty());
        assert(serializedMetadata.size() >= sizeof(BinaryMetadata));
        
        // 验证序列化数据的魔数
        const BinaryMetadata* header = reinterpret_cast<const BinaryMetadata*>(serializedMetadata.data());
        assert(header->magic == Constants::MAGIC_NUMBER);
        assert(header->version == Constants::VERSION);
        assert(header->folderCount == 2);
        
        // 使用MetadataParser反序列化并验证一致性
        MetadataParser parser;
        InstallationMetadata deserializedMetadata = parser.deserializeMetadata(serializedMetadata);
        
        // 验证反序列化的元数据与原始元数据一致
        assert(deserializedMetadata.version == metadata.version);
        assert(deserializedMetadata.folderCount == metadata.folderCount);
        assert(deserializedMetadata.folderMappings.size() == metadata.folderMappings.size());
        assert(deserializedMetadata.totalCompressedSize == metadata.totalCompressedSize);
        
        // 验证每个文件夹映射的一致性
        for (size_t i = 0; i < metadata.folderMappings.size(); ++i) {
            const auto& original = metadata.folderMappings[i];
            const auto& deserialized = deserializedMetadata.folderMappings[i];
            
            assert(deserialized.folderName == original.folderName);
            assert(deserialized.targetPath == original.targetPath);
            assert(deserialized.offset == original.offset);
            assert(deserialized.compressedSize == original.compressedSize);
            assert(deserialized.originalSize == original.originalSize);
            assert(deserialized.checksum == original.checksum);
            assert(deserialized.algorithm == original.algorithm);
        }
        
        // 验证元数据有效性
        bool isValid = parser.validateMetadata(deserializedMetadata);
        assert(isValid);
        
        // 清理测试数据
        for (const auto& folder : testFolders) {
            cleanupTestFolder(folder);
        }
        
        std::cout << "✓ Metadata mapping consistency test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        // 确保清理测试数据
        for (const auto& folder : testFolders) {
            cleanupTestFolder(folder);
        }
        std::cerr << "✗ Metadata mapping consistency test failed: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Running Metadata Management Tests" << std::endl;
    
    bool allTestsPassed = true;
    
    if (!testMetadataMappingConsistency()) {
        allTestsPassed = false;
    }
    
    if (allTestsPassed) {
        std::cout << "All metadata management tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some metadata management tests failed!" << std::endl;
        return 1;
    }
}