#include <rapidcheck.h>
#include "packager/folder_scanner.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <algorithm>

using namespace MultiThreadedInstaller;

// 生成随机文件名（避免特殊字符）
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

// 创建测试目录结构
struct TestDirectoryStructure {
    std::string basePath;
    std::set<std::string> expectedFolders;
    std::map<std::string, std::vector<std::string>> folderFiles;
    std::map<std::string, size_t> folderSizes;
    
    TestDirectoryStructure(const std::string& base) : basePath(base) {}
    
    void addFolder(const std::string& folderName, const std::vector<std::pair<std::string, size_t>>& files) {
        expectedFolders.insert(folderName);
        std::string folderPath = basePath + "/" + folderName;
        std::filesystem::create_directories(folderPath);
        
        size_t totalSize = 0;
        std::vector<std::string> filePaths;
        
        for (const auto& file : files) {
            std::string filePath = folderPath + "/" + file.first;
            filePaths.push_back(filePath);
            
            // 创建子目录（如果需要）
            std::filesystem::create_directories(std::filesystem::path(filePath).parent_path());
            
            // 创建文件
            auto content = generateRandomFileContent(file.second);
            std::ofstream outFile(filePath, std::ios::binary);
            outFile.write(reinterpret_cast<const char*>(content.data()), content.size());
            
            totalSize += file.second;
        }
        
        folderFiles[folderName] = filePaths;
        folderSizes[folderName] = totalSize;
    }
    
    ~TestDirectoryStructure() {
        try {
            std::filesystem::remove_all(basePath);
        } catch (const std::exception& e) {
            std::cerr << "Failed to cleanup test directory: " << e.what() << std::endl;
        }
    }
};

int main() {
    std::cout << "Running Property-Based Tests for FolderScanner" << std::endl;
    
    // **功能：multi-threaded-installer，属性 1：文件夹扫描完整性**
    // **验证：需求 1.1**
    auto folderScanIntegrityProperty = rc::check("Folder scan integrity", []() {
        // 生成随机测试数据
        auto folderCount = *rc::gen::inRange(1, 5);  // 1-5个文件夹
        
        // 创建唯一的测试目录
        std::string testBaseName = "test_scan_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::string testBasePath = std::filesystem::temp_directory_path().string() + "/" + testBaseName;
        
        TestDirectoryStructure testStructure(testBasePath);
        
        try {
            // 创建随机文件夹结构
            for (int i = 0; i < folderCount; ++i) {
                std::string folderName = generateRandomFolderName() + "_" + std::to_string(i);
                
                // 每个文件夹包含1-5个文件
                auto fileCount = *rc::gen::inRange(1, 5);
                std::vector<std::pair<std::string, size_t>> files;
                
                for (int j = 0; j < fileCount; ++j) {
                    std::string fileName = generateRandomFileName();
                    auto fileSize = *rc::gen::inRange(1, 1000);  // 1-1000字节
                    files.emplace_back(fileName, fileSize);
                }
                
                testStructure.addFolder(folderName, files);
            }
            
            // 使用FolderScanner扫描目录
            FolderScanner scanner;
            auto scannedFolders = scanner.scanInputDirectory(testBasePath);
            
            // 验证扫描结果的完整性
            
            // 1. 扫描到的文件夹数量应该与创建的文件夹数量相同
            RC_ASSERT(scannedFolders.size() == testStructure.expectedFolders.size());
            
            // 2. 所有预期的文件夹都应该被扫描到
            std::set<std::string> scannedFolderNames;
            for (const auto& folder : scannedFolders) {
                scannedFolderNames.insert(folder.targetPath);
            }
            RC_ASSERT(scannedFolderNames == testStructure.expectedFolders);
            
            // 3. 每个文件夹的文件列表应该完整且正确
            for (const auto& folder : scannedFolders) {
                const std::string& folderName = folder.targetPath;
                
                // 验证文件数量
                const auto& expectedFiles = testStructure.folderFiles[folderName];
                RC_ASSERT(folder.files.size() == expectedFiles.size());
                
                // 验证所有文件都被扫描到
                std::set<std::string> scannedFiles(folder.files.begin(), folder.files.end());
                std::set<std::string> expectedFileSet(expectedFiles.begin(), expectedFiles.end());
                RC_ASSERT(scannedFiles == expectedFileSet);
                
                // 验证文件夹大小计算正确
                size_t expectedSize = testStructure.folderSizes[folderName];
                RC_ASSERT(folder.totalSize == expectedSize);
                
                // 验证源路径正确
                std::string expectedSourcePath = testBasePath + "/" + folderName;
                RC_ASSERT(folder.sourcePath == expectedSourcePath);
            }
            
            // 4. 验证文件夹结构验证功能
            bool isValid = scanner.validateFolderStructure(scannedFolders);
            RC_ASSERT(isValid);
            
        } catch (const std::exception& e) {
            std::cerr << "Test exception: " << e.what() << std::endl;
            RC_FAIL("Test failed with exception");
        }
    });
    
    if (folderScanIntegrityProperty.succeed) {
        std::cout << "✓ Folder scan integrity property passed (" 
                  << folderScanIntegrityProperty.numSuccess << " tests)" << std::endl;
        return 0;
    } else {
        std::cout << "✗ Folder scan integrity property failed" << std::endl;
        if (!folderScanIntegrityProperty.failure.counterExample.empty()) {
            std::cout << "Counter-example: " << folderScanIntegrityProperty.failure.counterExample << std::endl;
        }
        if (!folderScanIntegrityProperty.failure.reason.empty()) {
            std::cout << "Reason: " << folderScanIntegrityProperty.failure.reason << std::endl;
        }
        return 1;
    }
}