#include <rapidcheck.h>
#include "packager/installer_generator.h"
#include "packager/metadata_generator.h"
#include "installer/metadata_parser.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>

using namespace MultiThreadedInstaller;

// 生成随机二进制数据
std::vector<uint8_t> generateRandomData(size_t size) {
    std::vector<uint8_t> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (auto& byte : data) {
        byte = dis(gen);
    }
    
    return data;
}

// 生成测试用的压缩结果
CompressionResult generateTestCompressionResult(size_t originalSize, size_t compressedSize) {
    CompressionResult result;
    result.compressedData = generateRandomData(compressedSize);
    result.originalSize = originalSize;
    result.compressedSize = compressedSize;
    result.algorithm = CompressionAlgorithm::ZSTD_FAST;
    
    // 计算简单校验和
    uint32_t checksum = 0;
    for (const auto& byte : result.compressedData) {
        checksum = (checksum * 31 + byte) % 0xFFFFFFFF;
    }
    result.checksum = checksum;
    
    return result;
}

// 生成测试用的文件夹信息
FolderInfo generateTestFolderInfo(const std::string& folderName, const std::string& targetPath, size_t totalSize) {
    FolderInfo folderInfo;
    folderInfo.sourcePath = folderName;
    folderInfo.targetPath = targetPath;
    folderInfo.totalSize = totalSize;
    
    // 添加一些虚拟文件路径
    int fileCount = (totalSize / 1000) + 1; // 每1KB一个文件
    for (int i = 0; i < fileCount; ++i) {
        folderInfo.files.push_back(folderName + "/file" + std::to_string(i) + ".txt");
    }
    
    return folderInfo;
}

// 验证安装程序文件的完整性
bool verifyInstallerIntegrity(const std::string& installerPath, 
                             const std::vector<uint8_t>& expectedMetadata,
                             const std::vector<std::vector<uint8_t>>& expectedCompressedData) {
    try {
        std::ifstream file(installerPath, std::ios::binary);
        if (!file) {
            return false;
        }
        
        // 获取文件大小
        file.seekg(0, std::ios::end);
        std::streampos fileSize = file.tellg();
        
        // 从文件末尾读取DataLocator
        size_t locatorSize = sizeof(uint64_t) * 4 + sizeof(uint32_t) + sizeof(uint32_t); // DataLocator + end magic
        
        if (static_cast<size_t>(fileSize) < locatorSize) {
            return false;
        }
        
        // 读取末尾魔数
        file.seekg(-static_cast<std::streamoff>(sizeof(uint32_t)), std::ios::end);
        uint32_t endMagic;
        file.read(reinterpret_cast<char*>(&endMagic), sizeof(uint32_t));
        
        if (endMagic != Constants::MAGIC_NUMBER) {
            return false;
        }
        
        // 读取DataLocator结构
        struct DataLocator {
            uint32_t magic;
            uint64_t metadataOffset;
            uint64_t metadataSize;
            uint64_t dataOffset;
            uint64_t dataSize;
        };
        
        file.seekg(-static_cast<std::streamoff>(locatorSize), std::ios::end);
        DataLocator locator;
        file.read(reinterpret_cast<char*>(&locator), sizeof(DataLocator));
        
        if (locator.magic != Constants::MAGIC_NUMBER) {
            return false;
        }
        
        // 验证元数据
        if (locator.metadataSize != expectedMetadata.size()) {
            return false;
        }
        
        file.seekg(locator.metadataOffset);
        std::vector<uint8_t> actualMetadata(locator.metadataSize);
        file.read(reinterpret_cast<char*>(actualMetadata.data()), locator.metadataSize);
        
        if (actualMetadata != expectedMetadata) {
            return false;
        }
        
        // 验证压缩数据
        uint64_t expectedDataSize = 0;
        for (const auto& data : expectedCompressedData) {
            expectedDataSize += data.size();
        }
        
        if (locator.dataSize != expectedDataSize) {
            return false;
        }
        
        file.seekg(locator.dataOffset);
        for (const auto& expectedData : expectedCompressedData) {
            std::vector<uint8_t> actualData(expectedData.size());
            file.read(reinterpret_cast<char*>(actualData.data()), expectedData.size());
            
            if (actualData != expectedData) {
                return false;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error verifying installer integrity: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Running Installer Generator Property-Based Tests" << std::endl;
    
    // **功能：multi-threaded-installer，属性 5：安装程序完整嵌入**
    // **验证：需求 2.2, 2.3**
    auto completeEmbeddingProperty = rc::check("Complete installer embedding", []() {
        // 生成随机测试数据
        auto folderCount = *rc::gen::inRange(1, 4);  // 1-4个文件夹
        std::vector<CompressionResult> compressionResults;
        std::vector<FolderInfo> folderInfos;
        std::vector<std::vector<uint8_t>> compressedDataList;
        
        for (int i = 0; i < folderCount; ++i) {
            auto originalSize = *rc::gen::inRange(1000, 10000);  // 1-10KB
            auto compressedSize = *rc::gen::inRange(500, originalSize);  // 压缩后应该更小
            
            auto result = generateTestCompressionResult(originalSize, compressedSize);
            compressionResults.push_back(result);
            compressedDataList.push_back(result.compressedData);
            
            auto folderInfo = generateTestFolderInfo("folder" + std::to_string(i), 
                                                   "target" + std::to_string(i), 
                                                   originalSize);
            folderInfos.push_back(folderInfo);
        }
        
        // 生成元数据
        MetadataGenerator metadataGen;
        auto metadata = metadataGen.generateMetadata(compressionResults, folderInfos);
        auto serializedMetadata = metadataGen.serializeMetadata(metadata);
        
        // 创建临时安装程序文件
        std::string tempDir = std::filesystem::temp_directory_path().string();
        std::string installerPath = tempDir + "/test_installer_" + 
                                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".exe";
        
        try {
            // 生成安装程序
            InstallerGenerator generator;
            bool success = generator.generateInstaller(installerPath, serializedMetadata, compressedDataList);
            
            RC_ASSERT(success);
            RC_ASSERT(std::filesystem::exists(installerPath));
            
            // 验证文件大小合理
            auto fileSize = std::filesystem::file_size(installerPath);
            RC_ASSERT(fileSize > serializedMetadata.size()); // 应该包含模板 + 元数据 + 数据
            
            // 验证安装程序的完整性
            bool integrityCheck = verifyInstallerIntegrity(installerPath, serializedMetadata, compressedDataList);
            RC_ASSERT(integrityCheck);
            
            // 验证安装程序可以被元数据解析器读取
            // 注意：这里我们不能直接运行安装程序，但可以验证数据结构
            
            // 清理临时文件
            std::filesystem::remove(installerPath);
            
        } catch (const std::exception& e) {
            // 确保清理临时文件
            if (std::filesystem::exists(installerPath)) {
                std::filesystem::remove(installerPath);
            }
            std::cerr << "Test exception: " << e.what() << std::endl;
            RC_FAIL("Test failed with exception");
        }
    });
    
    // 运行测试并报告结果
    bool allTestsPassed = true;
    
    if (completeEmbeddingProperty.succeed) {
        std::cout << "✓ Complete installer embedding property passed (" 
                  << completeEmbeddingProperty.numSuccess << " tests)" << std::endl;
    } else {
        std::cout << "✗ Complete installer embedding property failed" << std::endl;
        if (!completeEmbeddingProperty.failure.counterExample.empty()) {
            std::cout << "Counter-example: " << completeEmbeddingProperty.failure.counterExample << std::endl;
        }
        if (!completeEmbeddingProperty.failure.reason.empty()) {
            std::cout << "Reason: " << completeEmbeddingProperty.failure.reason << std::endl;
        }
        allTestsPassed = false;
    }
    
    return allTestsPassed ? 0 : 1;
}