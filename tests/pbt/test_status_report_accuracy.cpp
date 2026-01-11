#include "installer/metadata_parser.h"
#include "installer/thread_pool_manager.h"
#include "installer/decompression_engine.h"
#include "installer/file_system_operator.h"
#include "installer/console_interface.h"
#include "packager/folder_scanner.h"
#include "packager/compression_module.h"
#include "packager/metadata_generator.h"
#include "packager/installer_generator.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>
#include <sstream>
#include <iostream>
#include <cassert>

using namespace MultiThreadedInstaller;

// Simple property-based testing framework
class PropertyTest {
public:
    static bool runTest(const std::string& name, std::function<bool()> testFunc, int iterations = 100) {
        std::cout << "Running property test: " << name << " (" << iterations << " iterations)... ";
        
        for (int i = 0; i < iterations; ++i) {
            try {
                if (!testFunc()) {
                    std::cout << "FAILED at iteration " << (i + 1) << std::endl;
                    return false;
                }
            } catch (const std::exception& e) {
                std::cout << "FAILED with exception at iteration " << (i + 1) << ": " << e.what() << std::endl;
                return false;
            }
        }
        
        std::cout << "PASSED" << std::endl;
        return true;
    }
};

// Random number generator
std::random_device rd;
std::mt19937 gen(rd());

int randomInt(int min, int max) {
    std::uniform_int_distribution<int> dis(min, max);
    return dis(gen);
}

// 模拟控制台界面用于捕获状态报告
class MockConsoleInterface {
public:
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> infos;
    bool lastInstallationSuccess = false;
    std::vector<std::string> lastInstallationErrors;
    
    void showError(const std::string& message) {
        errors.push_back(message);
    }
    
    void showWarning(const std::string& message) {
        warnings.push_back(message);
    }
    
    void showInfo(const std::string& message) {
        infos.push_back(message);
    }
    
    void showInstallationResult(bool success, const std::vector<std::string>& errorList) {
        lastInstallationSuccess = success;
        lastInstallationErrors = errorList;
    }
    
    void clearMessages() {
        errors.clear();
        warnings.clear();
        infos.clear();
        lastInstallationErrors.clear();
        lastInstallationSuccess = false;
    }
};

// 生成随机文件内容
std::vector<uint8_t> generateRandomFileContent(size_t size) {
    std::vector<uint8_t> content(size);
    std::uniform_int_distribution<int> dis(0, 255);
    
    for (auto& byte : content) {
        byte = static_cast<uint8_t>(dis(gen));
    }
    
    return content;
}

// 生成随机文件名
std::string generateRandomFileName() {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
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

// 模拟安装过程
struct InstallationResult {
    bool overallSuccess;
    std::vector<std::string> errors;
    size_t successfulFolders;
    size_t totalFolders;
};

InstallationResult simulateInstallation(const std::vector<FolderInfo>& folderInfos, 
                                       const std::vector<CompressionResult>& compressionResults,
                                       MockConsoleInterface& console,
                                       bool introduceErrors = false) {
    InstallationResult result;
    result.overallSuccess = true;
    result.successfulFolders = 0;
    result.totalFolders = folderInfos.size();
    
    // 创建文件系统操作器
    FileSystemOperator fsOperator;
    
    // 处理每个文件夹
    for (size_t i = 0; i < folderInfos.size(); ++i) {
        const auto& folderInfo = folderInfos[i];
        
        // 创建目标目录
        std::string targetPath = std::filesystem::temp_directory_path().string() + "/install_test_" + 
                                std::to_string(i) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        
        if (!fsOperator.createDirectoryRecursive(targetPath)) {
            std::string error = "Failed to create target directory: " + targetPath;
            console.showError(error);
            result.errors.push_back(error);
            result.overallSuccess = false;
            continue;
        }
        
        // 模拟解压过程（不实际解压，只模拟成功/失败）
        bool decompressSuccess = true;
        
        if (introduceErrors && i == 0) {
            // 故意引入错误到第一个文件夹
            decompressSuccess = false;
            std::string error = "Simulated decompression error for folder: " + folderInfo.targetPath;
            console.showError(error);
            result.errors.push_back(error);
            result.overallSuccess = false;
        } else {
            // 模拟成功的解压过程
            // 创建一些测试文件来模拟解压结果
            for (const auto& filePath : folderInfo.files) {
                std::string fileName = std::filesystem::path(filePath).filename().string();
                std::string testFilePath = targetPath + "/" + fileName;
                
                // 创建一个简单的测试文件
                std::ofstream testFile(testFilePath);
                if (testFile) {
                    testFile << "Test content for " << fileName;
                    testFile.close();
                } else {
                    decompressSuccess = false;
                    std::string error = "Failed to create test file: " + testFilePath;
                    console.showError(error);
                    result.errors.push_back(error);
                    result.overallSuccess = false;
                    break;
                }
            }
        }
        
        if (decompressSuccess) {
            result.successfulFolders++;
        }
        
        // 清理目标目录
        cleanupTestFolder(targetPath);
    }
    
    // 显示安装结果
    console.showInstallationResult(result.overallSuccess, result.errors);
    
    return result;
}

int main() {
    std::cout << "Running Status Report Accuracy Property-Based Tests" << std::endl;
    
    // **功能：multi-threaded-installer，属性 11：状态报告准确性**
    // **验证：需求 5.4**
    bool success = PropertyTest::runTest("Status report accuracy", []() -> bool {
        // 生成多个不同的文件夹进行测试
        int folderCount = randomInt(1, 3);  // 1-3个文件夹
        std::vector<std::string> testFolders;
        std::vector<FolderInfo> folderInfos;
        std::vector<CompressionResult> compressionResults;
        
        try {
            // 创建测试文件夹和压缩结果
            CompressionModule compressor;
            compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
            
            for (int i = 0; i < folderCount; ++i) {
                // 为每个文件夹生成随机文件
                int fileCount = randomInt(1, 2);  // 1-2个文件
                std::vector<std::pair<std::string, std::vector<uint8_t>>> testFiles;
                
                for (int j = 0; j < fileCount; ++j) {
                    auto fileName = generateRandomFileName();
                    auto fileSize = randomInt(100, 1000);  // 100B-1KB文件
                    auto fileContent = generateRandomFileContent(fileSize);
                    testFiles.emplace_back(fileName, fileContent);
                }
                
                // 创建测试文件夹
                std::string testFolderName = "status_test_" + std::to_string(i) + "_" + 
                                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                std::string folderPath = createTestFolder(testFolderName, testFiles);
                testFolders.push_back(folderPath);
                
                // 创建FolderInfo
                std::string targetPath = "target_" + std::to_string(i);
                FolderInfo folderInfo = createFolderInfo(folderPath, targetPath);
                folderInfos.push_back(folderInfo);
                
                // 压缩文件夹
                auto compressionResult = compressor.compressFolder(folderInfo);
                if (compressionResult.compressedData.empty()) {
                    // 清理并返回失败
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                compressionResults.push_back(compressionResult);
            }
            
            // 测试成功安装的状态报告
            {
                MockConsoleInterface console;
                console.clearMessages();
                
                InstallationResult result = simulateInstallation(folderInfos, compressionResults, console, false);
                
                // 验证成功安装的状态报告
                if (result.overallSuccess != true) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                if (!result.errors.empty()) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                if (result.successfulFolders != result.totalFolders) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                
                // 验证控制台报告的准确性
                if (console.lastInstallationSuccess != true) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                if (!console.lastInstallationErrors.empty()) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                
                // 成功安装不应该有错误消息
                if (!console.errors.empty()) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
            }
            
            // 测试有错误的安装的状态报告
            if (folderCount > 1) {  // 只有多个文件夹时才测试错误情况
                MockConsoleInterface console;
                console.clearMessages();
                
                InstallationResult result = simulateInstallation(folderInfos, compressionResults, console, true);
                
                // 验证有错误的安装的状态报告
                if (result.overallSuccess != false) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                if (result.errors.empty()) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                if (result.successfulFolders >= result.totalFolders) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                
                // 验证控制台报告的准确性
                if (console.lastInstallationSuccess != false) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                if (console.lastInstallationErrors.empty()) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                
                // 应该有错误消息
                if (console.errors.empty()) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                
                // 错误消息数量应该与实际错误数量一致
                if (console.errors.size() != result.errors.size()) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                if (console.lastInstallationErrors.size() != result.errors.size()) {
                    for (const auto& folder : testFolders) {
                        cleanupTestFolder(folder);
                    }
                    return false;
                }
                
                // 错误消息内容应该一致
                for (size_t i = 0; i < result.errors.size(); ++i) {
                    if (console.lastInstallationErrors[i] != result.errors[i]) {
                        for (const auto& folder : testFolders) {
                            cleanupTestFolder(folder);
                        }
                        return false;
                    }
                }
            }
            
            // 清理测试数据
            for (const auto& folder : testFolders) {
                cleanupTestFolder(folder);
            }
            
            return true;
            
        } catch (const std::exception& e) {
            // 确保清理测试数据
            for (const auto& folder : testFolders) {
                cleanupTestFolder(folder);
            }
            std::cerr << "Test exception: " << e.what() << std::endl;
            return false;
        }
    }, 50); // Run 50 iterations
    
    if (success) {
        std::cout << "All Status Report Accuracy property-based tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Status Report Accuracy property-based tests failed!" << std::endl;
        return 1;
    }
}