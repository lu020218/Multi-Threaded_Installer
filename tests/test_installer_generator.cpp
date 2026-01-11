#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>
#include "packager/installer_generator.h"
#include "packager/metadata_generator.h"
#include "installer/metadata_parser.h"

using namespace MultiThreadedInstaller;

// 生成随机二进制数据
std::vector<uint8_t> generateRandomData(size_t size) {
    std::vector<uint8_t> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 255);  // Use int instead of uint8_t
    
    for (auto& byte : data) {
        byte = static_cast<uint8_t>(dis(gen));
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
            std::cerr << "Failed to open installer file: " << installerPath << std::endl;
            return false;
        }
        
        // 获取文件大小
        file.seekg(0, std::ios::end);
        std::streampos fileSize = file.tellg();
        
        // 从文件末尾读取DataLocator
        size_t locatorSize = sizeof(uint32_t) + sizeof(uint64_t) * 4 + sizeof(uint32_t); // DataLocator + end magic
        
        if (static_cast<size_t>(fileSize) < locatorSize) {
            std::cerr << "File too small to contain embedded data" << std::endl;
            return false;
        }
        
        // 读取末尾魔数
        file.seekg(-static_cast<std::streamoff>(sizeof(uint32_t)), std::ios::end);
        uint32_t endMagic;
        file.read(reinterpret_cast<char*>(&endMagic), sizeof(uint32_t));
        
        if (endMagic != Constants::MAGIC_NUMBER) {
            std::cerr << "Invalid end magic number" << std::endl;
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
        
        file.seekg(-static_cast<std::streamoff>(sizeof(DataLocator) + sizeof(uint32_t)), std::ios::end);
        DataLocator locator;
        file.read(reinterpret_cast<char*>(&locator), sizeof(DataLocator));
        
        if (locator.magic != Constants::MAGIC_NUMBER) {
            std::cerr << "Invalid locator magic number" << std::endl;
            return false;
        }
        
        // 验证元数据
        if (locator.metadataSize != expectedMetadata.size()) {
            std::cerr << "Metadata size mismatch: expected " << expectedMetadata.size() 
                      << ", got " << locator.metadataSize << std::endl;
            return false;
        }
        
        file.seekg(locator.metadataOffset);
        std::vector<uint8_t> actualMetadata(locator.metadataSize);
        file.read(reinterpret_cast<char*>(actualMetadata.data()), locator.metadataSize);
        
        if (actualMetadata != expectedMetadata) {
            std::cerr << "Metadata content mismatch" << std::endl;
            return false;
        }
        
        // 验证压缩数据
        uint64_t expectedDataSize = 0;
        for (const auto& data : expectedCompressedData) {
            expectedDataSize += data.size();
        }
        
        if (locator.dataSize != expectedDataSize) {
            std::cerr << "Data size mismatch: expected " << expectedDataSize 
                      << ", got " << locator.dataSize << std::endl;
            return false;
        }
        
        file.seekg(locator.dataOffset);
        for (const auto& expectedData : expectedCompressedData) {
            std::vector<uint8_t> actualData(expectedData.size());
            file.read(reinterpret_cast<char*>(actualData.data()), expectedData.size());
            
            if (actualData != expectedData) {
                std::cerr << "Compressed data content mismatch" << std::endl;
                return false;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error verifying installer integrity: " << e.what() << std::endl;
        return false;
    }
}

// 测试单个文件夹的安装程序生成
void testSingleFolderInstaller() {
    std::cout << "Testing single folder installer generation..." << std::endl;
    
    // 生成测试数据
    auto result = generateTestCompressionResult(5000, 2500);
    auto folderInfo = generateTestFolderInfo("test_folder", "target_folder", 5000);
    
    std::vector<CompressionResult> compressionResults = {result};
    std::vector<FolderInfo> folderInfos = {folderInfo};
    std::vector<std::vector<uint8_t>> compressedDataList = {result.compressedData};
    
    // 生成元数据
    MetadataGenerator metadataGen;
    auto metadata = metadataGen.generateMetadata(compressionResults, folderInfos);
    auto serializedMetadata = metadataGen.serializeMetadata(metadata);
    
    // 创建临时安装程序文件
    std::string tempDir = std::filesystem::temp_directory_path().string();
    std::string installerPath = tempDir + "/test_single_installer_" + 
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".exe";
    
    try {
        // 生成安装程序
        InstallerGenerator generator;
        bool success = generator.generateInstaller(installerPath, serializedMetadata, compressedDataList);
        
        assert(success && "Failed to generate installer");
        assert(std::filesystem::exists(installerPath) && "Installer file does not exist");
        
        // 验证文件大小合理
        auto fileSize = std::filesystem::file_size(installerPath);
        assert(fileSize > serializedMetadata.size() && "Installer file too small");
        
        // **功能：multi-threaded-installer，属性 5：安装程序完整嵌入**
        // **验证：需求 2.2, 2.3**
        bool integrityCheck = verifyInstallerIntegrity(installerPath, serializedMetadata, compressedDataList);
        assert(integrityCheck && "Installer integrity check failed");
        
        std::cout << "✓ Single folder installer test passed" << std::endl;
        
        // 清理临时文件
        std::filesystem::remove(installerPath);
        
    } catch (const std::exception& e) {
        // 确保清理临时文件
        if (std::filesystem::exists(installerPath)) {
            std::filesystem::remove(installerPath);
        }
        std::cerr << "Test exception: " << e.what() << std::endl;
        assert(false && "Test failed with exception");
    }
}

// 测试多个文件夹的安装程序生成
void testMultipleFoldersInstaller() {
    std::cout << "Testing multiple folders installer generation..." << std::endl;
    
    // 生成多个测试文件夹
    std::vector<CompressionResult> compressionResults;
    std::vector<FolderInfo> folderInfos;
    std::vector<std::vector<uint8_t>> compressedDataList;
    
    for (int i = 0; i < 3; ++i) {
        auto result = generateTestCompressionResult(3000 + i * 1000, 1500 + i * 500);
        auto folderInfo = generateTestFolderInfo("folder" + std::to_string(i), 
                                               "target" + std::to_string(i), 
                                               3000 + i * 1000);
        
        compressionResults.push_back(result);
        folderInfos.push_back(folderInfo);
        compressedDataList.push_back(result.compressedData);
    }
    
    // 生成元数据
    MetadataGenerator metadataGen;
    auto metadata = metadataGen.generateMetadata(compressionResults, folderInfos);
    auto serializedMetadata = metadataGen.serializeMetadata(metadata);
    
    // 创建临时安装程序文件
    std::string tempDir = std::filesystem::temp_directory_path().string();
    std::string installerPath = tempDir + "/test_multi_installer_" + 
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".exe";
    
    try {
        // 生成安装程序
        InstallerGenerator generator;
        bool success = generator.generateInstaller(installerPath, serializedMetadata, compressedDataList);
        
        assert(success && "Failed to generate installer");
        assert(std::filesystem::exists(installerPath) && "Installer file does not exist");
        
        // 验证文件大小合理
        auto fileSize = std::filesystem::file_size(installerPath);
        assert(fileSize > serializedMetadata.size() && "Installer file too small");
        
        // **功能：multi-threaded-installer，属性 5：安装程序完整嵌入**
        // **验证：需求 2.2, 2.3**
        bool integrityCheck = verifyInstallerIntegrity(installerPath, serializedMetadata, compressedDataList);
        assert(integrityCheck && "Installer integrity check failed");
        
        std::cout << "✓ Multiple folders installer test passed" << std::endl;
        
        // 清理临时文件
        std::filesystem::remove(installerPath);
        
    } catch (const std::exception& e) {
        // 确保清理临时文件
        if (std::filesystem::exists(installerPath)) {
            std::filesystem::remove(installerPath);
        }
        std::cerr << "Test exception: " << e.what() << std::endl;
        assert(false && "Test failed with exception");
    }
}

// 测试空数据的处理
void testEmptyDataHandling() {
    std::cout << "Testing empty data handling..." << std::endl;
    
    // 创建空的数据集
    std::vector<uint8_t> emptyMetadata;
    std::vector<std::vector<uint8_t>> emptyCompressedData;
    
    std::string tempDir = std::filesystem::temp_directory_path().string();
    std::string installerPath = tempDir + "/test_empty_installer_" + 
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".exe";
    
    try {
        InstallerGenerator generator;
        bool success = generator.generateInstaller(installerPath, emptyMetadata, emptyCompressedData);
        
        // 应该能够处理空数据（虽然可能不实用）
        assert(success && "Failed to handle empty data");
        
        if (std::filesystem::exists(installerPath)) {
            std::cout << "✓ Empty data handling test passed" << std::endl;
            std::filesystem::remove(installerPath);
        }
        
    } catch (const std::exception& e) {
        if (std::filesystem::exists(installerPath)) {
            std::filesystem::remove(installerPath);
        }
        std::cerr << "Test exception: " << e.what() << std::endl;
        assert(false && "Test failed with exception");
    }
}

int main() {
    std::cout << "Running Installer Generator Tests" << std::endl;
    std::cout << "=================================" << std::endl;
    
    try {
        testSingleFolderInstaller();
        testMultipleFoldersInstaller();
        testEmptyDataHandling();
        
        std::cout << std::endl << "All installer generator tests passed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test suite failed: " << e.what() << std::endl;
        return 1;
    }
}