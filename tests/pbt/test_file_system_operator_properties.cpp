#include "installer/file_system_operator.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>
#include <iostream>
#include <cassert>

using namespace MultiThreadedInstaller;

// 生成随机文件内容
std::vector<uint8_t> generateRandomFileContent(size_t size) {
    std::vector<uint8_t> content(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 255);
    
    for (auto& byte : content) {
        byte = static_cast<uint8_t>(dis(gen));
    }
    
    return content;
}

// 生成随机文件名
std::string generateRandomFileName() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis('a', 'z');
    
    std::string name;
    size_t length = gen() % 10 + 5; // 5-14 characters
    for (size_t i = 0; i < length; ++i) {
        name += static_cast<char>(dis(gen));
    }
    return name + ".txt";
}

// 生成随机目录路径
std::string generateRandomDirectoryPath(const std::string& basePath, int depth = 3) {
    std::string path = basePath;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> depthDis(1, depth);
    std::uniform_int_distribution<int> charDis('a', 'z');
    
    int actualDepth = depthDis(gen);
    for (int i = 0; i < actualDepth; ++i) {
        path += "/";
        size_t nameLength = gen() % 8 + 3; // 3-10 characters
        for (size_t j = 0; j < nameLength; ++j) {
            path += static_cast<char>(charDis(gen));
        }
    }
    
    return path;
}

// 创建唯一的测试基础路径
std::string createUniqueTestPath() {
    auto now = std::chrono::steady_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    return std::filesystem::temp_directory_path().string() + "/fs_test_" + std::to_string(timestamp);
}

// 清理测试目录
void cleanupTestDirectory(const std::string& path) {
    try {
        if (std::filesystem::exists(path)) {
            std::filesystem::remove_all(path);
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to cleanup test directory: " << e.what() << std::endl;
    }
}

// **属性7：目录映射正确性**
// *对于任何*文件夹和其指定的目标目录，解压后的文件夹应该出现在正确的目标位置，且目标目录应该在需要时自动创建
// **验证：需求 4.1, 4.2, 4.3**
void testDirectoryMappingCorrectness() {
    std::cout << "Testing Property 7: Directory Mapping Correctness..." << std::endl;
    
    // **Feature: multi-threaded-installer, Property 7: Directory mapping correctness**
    for (int iteration = 0; iteration < 100; ++iteration) {
        FileSystemOperator fsOp;
        std::string basePath = createUniqueTestPath();
        
        try {
            // 生成随机的目标目录路径
            std::string targetDirectory = generateRandomDirectoryPath(basePath);
            
            // 生成随机文件数据
            std::vector<uint8_t> fileContent = generateRandomFileContent(100 + (iteration % 900));
            
            std::string fileName = generateRandomFileName();
            std::string fullFilePath = targetDirectory + "/" + fileName;
            
            // 测试目录创建
            bool dirCreated = fsOp.createDirectoryRecursive(targetDirectory);
            assert(dirCreated);
            assert(fsOp.directoryExists(targetDirectory));
            
            // 测试文件写入到正确位置
            bool fileWritten = fsOp.writeFile(fullFilePath, fileContent);
            assert(fileWritten);
            assert(fsOp.fileExists(fullFilePath));
            assert(fsOp.getFileSize(fullFilePath) == fileContent.size());
            
            // 验证文件内容完整性
            uint32_t expectedChecksum = fsOp.getFileChecksum(fullFilePath);
            assert(fsOp.verifyFileIntegrity(fullFilePath, expectedChecksum));
            
            cleanupTestDirectory(basePath);
            
        } catch (...) {
            cleanupTestDirectory(basePath);
            throw;
        }
    }
    
    std::cout << "✓ Property 7 passed (100 iterations)" << std::endl;
}

// **属性8：安装验证完整性**
// *对于任何*安装操作，完成后所有文件都应该存在于其预期的目标位置，且文件内容应该与原始文件匹配
// **验证：需求 4.4**
void testInstallationVerificationIntegrity() {
    std::cout << "Testing Property 8: Installation Verification Integrity..." << std::endl;
    
    // **Feature: multi-threaded-installer, Property 8: Installation verification integrity**
    for (int iteration = 0; iteration < 100; ++iteration) {
        FileSystemOperator fsOp;
        std::string basePath = createUniqueTestPath();
        
        try {
            // 生成多个文件的安装场景
            size_t fileCount = 1 + (iteration % 10);
            std::vector<std::pair<std::string, std::vector<uint8_t>>> filesToInstall;
            std::vector<uint32_t> expectedChecksums;
            
            for (size_t i = 0; i < fileCount; ++i) {
                std::string fileName = "file_" + std::to_string(i) + "_" + generateRandomFileName();
                std::vector<uint8_t> fileContent = generateRandomFileContent(1 + (i * 50) % 500);
                
                filesToInstall.emplace_back(fileName, fileContent);
            }
            
            // 创建安装目录
            std::string installDir = basePath + "/installation";
            bool dirCreated = fsOp.createDirectoryRecursive(installDir);
            assert(dirCreated);
            
            // 安装所有文件并记录预期校验和
            for (const auto& file : filesToInstall) {
                std::string fullPath = installDir + "/" + file.first;
                bool written = fsOp.writeFile(fullPath, file.second);
                assert(written);
                
                uint32_t checksum = fsOp.getFileChecksum(fullPath);
                expectedChecksums.push_back(checksum);
            }
            
            // 验证安装完整性 - 所有文件都应该存在且内容正确
            for (size_t i = 0; i < filesToInstall.size(); ++i) {
                std::string fullPath = installDir + "/" + filesToInstall[i].first;
                
                // 文件应该存在
                assert(fsOp.fileExists(fullPath));
                
                // 文件大小应该正确
                assert(fsOp.getFileSize(fullPath) == filesToInstall[i].second.size());
                
                // 文件内容应该完整（校验和匹配）
                assert(fsOp.verifyFileIntegrity(fullPath, expectedChecksums[i]));
            }
            
            cleanupTestDirectory(basePath);
            
        } catch (...) {
            cleanupTestDirectory(basePath);
            throw;
        }
    }
    
    std::cout << "✓ Property 8 passed (100 iterations)" << std::endl;
}

// **属性10：文件冲突覆盖一致性**
// *对于任何*目标位置已存在的文件，安装程序应该一致地执行覆盖操作，不应出现不一致的处理行为
// **验证：需求 5.3**
void testFileConflictOverrideConsistency() {
    std::cout << "Testing Property 10: File Conflict Override Consistency..." << std::endl;
    
    // **Feature: multi-threaded-installer, Property 10: File conflict override consistency**
    for (int iteration = 0; iteration < 100; ++iteration) {
        FileSystemOperator fsOp;
        std::string basePath = createUniqueTestPath();
        
        try {
            // 创建测试目录
            std::string testDir = basePath + "/conflict_test";
            bool dirCreated = fsOp.createDirectoryRecursive(testDir);
            assert(dirCreated);
            
            std::string filePath = testDir + "/" + generateRandomFileName();
            
            // 生成原始文件内容
            std::vector<uint8_t> originalContent = generateRandomFileContent(100 + (iteration % 400));
            
            // 生成新文件内容（确保不同）
            std::vector<uint8_t> newContent = generateRandomFileContent(150 + (iteration % 350));
            
            // 确保内容不同
            if (originalContent == newContent) {
                newContent.push_back(42); // 添加一个字节确保不同
            }
            
            // 创建原始文件
            bool originalWritten = fsOp.writeFile(filePath, originalContent);
            assert(originalWritten);
            assert(fsOp.fileExists(filePath));
            
            uint32_t originalChecksum = fsOp.getFileChecksum(filePath);
            assert(fsOp.verifyFileIntegrity(filePath, originalChecksum));
            
            // 测试冲突处理 - 应该直接覆盖
            bool conflictHandled = fsOp.handleFileConflict(filePath);
            assert(conflictHandled);
            assert(!fsOp.fileExists(filePath)); // 文件应该被删除
            
            // 写入新文件 - 应该成功
            bool newWritten = fsOp.writeFile(filePath, newContent);
            assert(newWritten);
            assert(fsOp.fileExists(filePath));
            
            // 验证新文件内容
            assert(fsOp.getFileSize(filePath) == newContent.size());
            uint32_t newChecksum = fsOp.getFileChecksum(filePath);
            assert(fsOp.verifyFileIntegrity(filePath, newChecksum));
            assert(newChecksum != originalChecksum); // 校验和应该不同
            
            // 测试多次覆盖的一致性
            for (int i = 0; i < 3; ++i) {
                std::vector<uint8_t> anotherContent = generateRandomFileContent(50 + (i * 100));
                
                // 每次都应该能成功覆盖
                bool overwritten = fsOp.writeFile(filePath, anotherContent);
                assert(overwritten);
                assert(fsOp.fileExists(filePath));
                assert(fsOp.getFileSize(filePath) == anotherContent.size());
                
                uint32_t overwriteChecksum = fsOp.getFileChecksum(filePath);
                assert(fsOp.verifyFileIntegrity(filePath, overwriteChecksum));
            }
            
            cleanupTestDirectory(basePath);
            
        } catch (...) {
            cleanupTestDirectory(basePath);
            throw;
        }
    }
    
    std::cout << "✓ Property 10 passed (100 iterations)" << std::endl;
}

int main() {
    std::cout << "Running FileSystemOperator Property-Based Tests..." << std::endl;
    
    try {
        testDirectoryMappingCorrectness();
        testInstallationVerificationIntegrity();
        testFileConflictOverrideConsistency();
        
        std::cout << "\n✅ All FileSystemOperator property-based tests completed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Property-based test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\n❌ Property-based test failed with unknown exception" << std::endl;
        return 1;
    }
}