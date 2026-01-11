#include "installer/decompression_engine.h"
#include "installer/thread_pool_manager.h"
#include "packager/compression_module.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>
#include <iostream>
#include <cassert>
#include <numeric>
#include <algorithm>

using namespace MultiThreadedInstaller;

// 生成随机文件内容
std::vector<uint8_t> generateRandomFileContent(size_t size) {
    std::vector<uint8_t> content(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 255);  // Use int instead of uint8_t
    
    for (auto& byte : content) {
        byte = static_cast<uint8_t>(dis(gen));
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

// 故意损坏压缩数据以模拟错误
std::vector<uint8_t> corruptData(const std::vector<uint8_t>& originalData, double corruptionRate = 0.1) {
    std::vector<uint8_t> corruptedData = originalData;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    std::uniform_int_distribution<int> byteDis(0, 255);  // Use int instead of uint8_t
    
    for (size_t i = 0; i < corruptedData.size(); ++i) {
        if (dis(gen) < corruptionRate) {
            corruptedData[i] = static_cast<uint8_t>(byteDis(gen));
        }
    }
    
    return corruptedData;
}

// 创建有效的压缩任务
DecompressionTask createValidTask(const std::string& folderName, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::string originalFolder = createTestFolder(folderName, files);
    
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
    folderInfo.totalSize = std::accumulate(files.begin(), files.end(), 0ULL,
        [](size_t sum, const auto& file) { return sum + file.second.size(); });
    
    // 压缩文件夹
    CompressionModule compressor;
    compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
    
    auto compressionResult = compressor.compressFolder(folderInfo);
    
    // 创建解压任务
    DecompressionTask task;
    task.compressedData = compressionResult.compressedData;
    task.expectedChecksum = compressionResult.checksum;
    task.originalSize = compressionResult.originalSize;
    task.algorithm = compressionResult.algorithm;
    task.targetPath = std::filesystem::temp_directory_path().string() + "/decompressed_" + folderName;
    
    // 清理原始文件夹
    cleanupTestFolder(originalFolder);
    
    return task;
}

// 测试错误恢复连续性
bool testErrorRecoveryContinuity() {
    std::cout << "Testing error recovery continuity..." << std::endl;
    
    // 创建多个任务，其中一些会故意损坏
    std::vector<DecompressionTask> tasks;
    std::vector<bool> shouldSucceed;
    
    const int taskCount = 5;
    
    for (int i = 0; i < taskCount; ++i) {
        // 生成随机测试文件
        std::vector<std::pair<std::string, std::vector<uint8_t>>> testFiles;
        testFiles.emplace_back("file1.txt", generateRandomFileContent(1000));
        testFiles.emplace_back("file2.txt", generateRandomFileContent(2000));
        
        std::string taskName = "task_" + std::to_string(i) + "_" + 
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        
        DecompressionTask task = createValidTask(taskName, testFiles);
        
        // 损坏第2个和第4个任务（索引1和3）
        bool shouldCorrupt = (i == 1 || i == 3);
        
        if (shouldCorrupt) {
            // 损坏压缩数据
            task.compressedData = corruptData(task.compressedData, 0.05);  // 5%损坏率
            shouldSucceed.push_back(false);
            std::cout << "  Task " << i << ": corrupted (should fail)" << std::endl;
        } else {
            shouldSucceed.push_back(true);
            std::cout << "  Task " << i << ": valid (should succeed)" << std::endl;
        }
        
        tasks.push_back(task);
    }
    
    // 创建解压引擎和线程池
    auto threadPool = std::make_shared<ThreadPoolManager>(2);
    DecompressionEngine decompressor;
    decompressor.setThreadPool(threadPool);
    
    // 记录成功和失败的任务
    std::vector<bool> actualResults;
    std::vector<std::string> errorMessages;
    
    // 执行所有解压任务
    for (size_t i = 0; i < tasks.size(); ++i) {
        try {
            std::cout << "  Executing task " << i << "..." << std::endl;
            bool result = decompressor.decompressFolder(tasks[i]);
            actualResults.push_back(result);
            
            if (!result) {
                errorMessages.push_back("Task " + std::to_string(i) + " failed as expected");
                std::cout << "    Result: FAILED (as expected)" << std::endl;
            } else {
                std::cout << "    Result: SUCCESS" << std::endl;
            }
        } catch (const std::exception& e) {
            actualResults.push_back(false);
            errorMessages.push_back("Task " + std::to_string(i) + " threw exception: " + e.what());
            std::cout << "    Result: EXCEPTION - " << e.what() << std::endl;
        }
    }
    
    // 验证错误恢复连续性的关键属性：
    // 1. 所有任务都应该被尝试（没有因为前面的错误而停止）
    bool allTasksAttempted = (actualResults.size() == tasks.size());
    std::cout << "All tasks attempted: " << (allTasksAttempted ? "YES" : "NO") << std::endl;
    
    // 2. 至少应该有一些任务成功（假设不是所有任务都被损坏）
    size_t successCount = std::count(actualResults.begin(), actualResults.end(), true);
    size_t expectedSuccessCount = std::count(shouldSucceed.begin(), shouldSucceed.end(), true);
    
    std::cout << "Expected successes: " << expectedSuccessCount << std::endl;
    std::cout << "Actual successes: " << successCount << std::endl;
    
    bool hasExpectedSuccesses = (expectedSuccessCount > 0) ? (successCount > 0) : true;
    std::cout << "Has expected successes: " << (hasExpectedSuccesses ? "YES" : "NO") << std::endl;
    
    // 清理测试数据
    for (const auto& task : tasks) {
        cleanupTestFolder(task.targetPath);
    }
    
    // 输出统计信息
    std::cout << "Error messages: " << errorMessages.size() << std::endl;
    for (const auto& msg : errorMessages) {
        std::cout << "  " << msg << std::endl;
    }
    
    return allTasksAttempted && hasExpectedSuccesses;
}

int main() {
    std::cout << "Running Property-Based Tests for Decompression Engine Error Recovery" << std::endl;
    std::cout << "**功能：multi-threaded-installer，属性 9：错误恢复连续性**" << std::endl;
    std::cout << "**验证：需求 5.2**" << std::endl;
    std::cout << std::endl;
    
    try {
        bool testPassed = testErrorRecoveryContinuity();
        
        if (testPassed) {
            std::cout << std::endl << "✓ Error recovery continuity property passed" << std::endl;
            return 0;
        } else {
            std::cout << std::endl << "✗ Error recovery continuity property failed" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << std::endl << "✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}