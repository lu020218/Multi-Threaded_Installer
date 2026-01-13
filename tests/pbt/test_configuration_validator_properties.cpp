#include <iostream>
#include <cassert>
#include <filesystem>
#include <random>
#include <vector>
#include <string>
#include "packager/configuration_validator.h"

namespace fs = std::filesystem;
using namespace MultiThreadedInstaller;

// 随机生成器
class RandomGenerator {
public:
    RandomGenerator() : gen(rd()), dist(0, 1000) {}
    
    int getInt() {
        return dist(gen);
    }
    
    std::string getString(size_t length = 10) {
        const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += charset[dist(gen) % (sizeof(charset) - 1)];
        }
        return result;
    }
    
    bool getBool() {
        return dist(gen) % 2 == 0;
    }
    
    std::string getValidDirectoryType() {
        std::vector<std::string> types = {
            "installDirectory",
            "%ProgramFiles%",
            "%AppData%\\Roaming",
            "%LocalAppData%",
            "%ProgramData%",
            "%USERPROFILE%"
        };
        return types[dist(gen) % types.size()];
    }
    
    std::string getInvalidDirectoryType() {
        std::vector<std::string> types = {
            "%InvalidVar%",
            "%UNKNOWN%",
            "%BadEnv%"
        };
        return types[dist(gen) % types.size()];
    }
    
    char getIllegalChar() {
        const char illegalChars[] = "<>:\"|?*";
        return illegalChars[dist(gen) % (sizeof(illegalChars) - 1)];
    }
    
private:
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_int_distribution<> dist;
};

// 测试辅助类
class PropertyTestHelper {
public:
    PropertyTestHelper() {
        // 创建临时测试目录
        testDir = fs::temp_directory_path() / ("pbt_validator_test_" + std::to_string(std::random_device{}()));
        fs::create_directories(testDir);
    }
    
    ~PropertyTestHelper() {
        // 清理测试目录
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
    }
    
    void createFolder(const std::string& folderName) {
        fs::path folderPath = testDir / folderName;
        fs::create_directories(folderPath);
    }
    
    std::string getTestDir() const {
        return testDir.string();
    }
    
private:
    fs::path testDir;
};

// Feature: packager-config-file, Property 3: Application Name Requirement
// Validates: Requirements 1.6, 9.1
// Property: For any configuration with missing or empty applicationName,
// the validator should return an error
bool testApplicationNameRequirement() {
    std::cout << "Testing Property 3: Application Name Requirement..." << std::endl;
    std::cout << "Feature: packager-config-file, Property 3" << std::endl;
    std::cout << "Validates: Requirements 1.6, 9.1" << std::endl;
    
    const int NUM_ITERATIONS = 100;
    int successCount = 0;
    RandomGenerator rng;
    
    try {
        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
            PropertyTestHelper helper;
            
            // 创建配置，应用程序名为空
            PackagerConfiguration config;
            config.applicationName = "";  // 空应用程序名
            
            // 随机添加一些文件夹目标（但不影响应用程序名验证）
            int numFolders = rng.getInt() % 5;
            for (int i = 0; i < numFolders; ++i) {
                std::string folderName = "folder_" + std::to_string(i);
                helper.createFolder(folderName);
                
                FolderTargetConfig ftc;
                ftc.folderName = folderName;
                ftc.targetDirectory = rng.getValidDirectoryType();
                config.folderTargets.push_back(ftc);
            }
            
            // 验证配置
            ConfigurationValidator validator;
            auto result = validator.validate(config, helper.getTestDir());
            
            // 验证返回错误
            if (result.isValid) {
                std::cerr << "  Iteration " << iteration << " failed: Empty app name should fail validation" << std::endl;
                continue;
            }
            
            // 验证错误信息包含applicationName
            bool hasAppNameError = false;
            for (const auto& error : result.errors) {
                if (error.find("applicationName") != std::string::npos) {
                    hasAppNameError = true;
                    break;
                }
            }
            
            if (!hasAppNameError) {
                std::cerr << "  Iteration " << iteration << " failed: Error should mention applicationName" << std::endl;
                continue;
            }
            
            successCount++;
        }
        
        std::cout << "  Passed " << successCount << "/" << NUM_ITERATIONS << " iterations" << std::endl;
        
        if (successCount == NUM_ITERATIONS) {
            std::cout << "✓ Property 3: Application Name Requirement test passed" << std::endl;
            return true;
        } else {
            std::cerr << "✗ Property 3: Application Name Requirement test failed" << std::endl;
            std::cerr << "  Only " << successCount << "/" << NUM_ITERATIONS << " iterations passed" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Property 3 test failed with exception: " << e.what() << std::endl;
        return false;
    }
}

// Feature: packager-config-file, Property 6: Folder Existence Validation
// Validates: Requirements 9.4
// Property: For any folder target configuration, if the folder does not exist
// in the input directory, the validator should return an error
bool testFolderExistenceValidation() {
    std::cout << "Testing Property 6: Folder Existence Validation..." << std::endl;
    std::cout << "Feature: packager-config-file, Property 6" << std::endl;
    std::cout << "Validates: Requirements 9.4" << std::endl;
    
    const int NUM_ITERATIONS = 100;
    int successCount = 0;
    RandomGenerator rng;
    
    try {
        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
            PropertyTestHelper helper;
            
            // 创建配置
            PackagerConfiguration config;
            config.applicationName = "TestApp_" + rng.getString(5);
            
            // 添加一些存在的文件夹
            int numExistingFolders = rng.getInt() % 3;
            for (int i = 0; i < numExistingFolders; ++i) {
                std::string folderName = "existing_" + std::to_string(i);
                helper.createFolder(folderName);
                
                FolderTargetConfig ftc;
                ftc.folderName = folderName;
                ftc.targetDirectory = rng.getValidDirectoryType();
                config.folderTargets.push_back(ftc);
            }
            
            // 添加一个不存在的文件夹
            std::string nonExistentFolder = "nonexistent_" + rng.getString(8);
            FolderTargetConfig ftc;
            ftc.folderName = nonExistentFolder;
            ftc.targetDirectory = rng.getValidDirectoryType();
            config.folderTargets.push_back(ftc);
            
            // 验证配置
            ConfigurationValidator validator;
            auto result = validator.validate(config, helper.getTestDir());
            
            // 验证返回错误
            if (result.isValid) {
                std::cerr << "  Iteration " << iteration << " failed: Nonexistent folder should fail validation" << std::endl;
                continue;
            }
            
            // 验证错误信息包含"does not exist"或"Folder"
            bool hasFolderError = false;
            for (const auto& error : result.errors) {
                if (error.find("does not exist") != std::string::npos ||
                    error.find("Folder") != std::string::npos) {
                    hasFolderError = true;
                    break;
                }
            }
            
            if (!hasFolderError) {
                std::cerr << "  Iteration " << iteration << " failed: Error should mention folder existence" << std::endl;
                continue;
            }
            
            successCount++;
        }
        
        std::cout << "  Passed " << successCount << "/" << NUM_ITERATIONS << " iterations" << std::endl;
        
        if (successCount == NUM_ITERATIONS) {
            std::cout << "✓ Property 6: Folder Existence Validation test passed" << std::endl;
            return true;
        } else {
            std::cerr << "✗ Property 6: Folder Existence Validation test failed" << std::endl;
            std::cerr << "  Only " << successCount << "/" << NUM_ITERATIONS << " iterations passed" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Property 6 test failed with exception: " << e.what() << std::endl;
        return false;
    }
}

// Feature: packager-config-file, Property 12: Configuration Validation Completeness
// Validates: Requirements 9.1, 9.2, 9.5
// Property: For any invalid configuration (missing app name, invalid paths, etc.),
// the validator should detect and report all errors
bool testConfigurationValidationCompleteness() {
    std::cout << "Testing Property 12: Configuration Validation Completeness..." << std::endl;
    std::cout << "Feature: packager-config-file, Property 12" << std::endl;
    std::cout << "Validates: Requirements 9.1, 9.2, 9.5" << std::endl;
    
    const int NUM_ITERATIONS = 100;
    int successCount = 0;
    RandomGenerator rng;
    
    try {
        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
            PropertyTestHelper helper;
            
            // 创建一个包含多种错误的配置
            PackagerConfiguration config;
            
            // 随机决定是否包含各种错误
            bool hasEmptyAppName = rng.getBool();
            bool hasInvalidAppName = rng.getBool();
            bool hasNonexistentFolder = rng.getBool();
            bool hasInvalidTargetDir = rng.getBool();
            
            // 至少要有一个错误
            if (!hasEmptyAppName && !hasInvalidAppName && !hasNonexistentFolder && !hasInvalidTargetDir) {
                hasEmptyAppName = true;
            }
            
            int expectedErrorCount = 0;
            
            // 设置应用程序名
            if (hasEmptyAppName) {
                config.applicationName = "";
                expectedErrorCount++;
            } else if (hasInvalidAppName) {
                config.applicationName = "App" + std::string(1, rng.getIllegalChar()) + "Name";
                expectedErrorCount++;
            } else {
                config.applicationName = "ValidApp_" + rng.getString(5);
            }
            
            // 添加不存在的文件夹
            if (hasNonexistentFolder) {
                FolderTargetConfig ftc;
                ftc.folderName = "nonexistent_" + rng.getString(8);
                ftc.targetDirectory = rng.getValidDirectoryType();
                config.folderTargets.push_back(ftc);
                expectedErrorCount++;
            }
            
            // 添加无效的目标目录
            if (hasInvalidTargetDir) {
                std::string folderName = "folder_" + rng.getString(5);
                helper.createFolder(folderName);
                
                FolderTargetConfig ftc;
                ftc.folderName = folderName;
                ftc.targetDirectory = rng.getInvalidDirectoryType();
                config.folderTargets.push_back(ftc);
                expectedErrorCount++;
            }
            
            // 验证配置
            ConfigurationValidator validator;
            auto result = validator.validate(config, helper.getTestDir());
            
            // 验证返回错误
            if (result.isValid) {
                std::cerr << "  Iteration " << iteration << " failed: Invalid config should fail validation" << std::endl;
                continue;
            }
            
            // 验证至少检测到了一些错误
            if (result.errors.empty()) {
                std::cerr << "  Iteration " << iteration << " failed: Should have error messages" << std::endl;
                continue;
            }
            
            // 验证错误数量至少等于预期（可能有更多，因为一个配置项可能有多个错误）
            if (result.errors.size() < static_cast<size_t>(expectedErrorCount)) {
                std::cerr << "  Iteration " << iteration << " failed: Expected at least " 
                         << expectedErrorCount << " errors, got " << result.errors.size() << std::endl;
                continue;
            }
            
            successCount++;
        }
        
        std::cout << "  Passed " << successCount << "/" << NUM_ITERATIONS << " iterations" << std::endl;
        
        if (successCount == NUM_ITERATIONS) {
            std::cout << "✓ Property 12: Configuration Validation Completeness test passed" << std::endl;
            return true;
        } else {
            std::cerr << "✗ Property 12: Configuration Validation Completeness test failed" << std::endl;
            std::cerr << "  Only " << successCount << "/" << NUM_ITERATIONS << " iterations passed" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Property 12 test failed with exception: " << e.what() << std::endl;
        return false;
    }
}

// 测试有效配置总是通过验证
// Property: For any valid configuration (valid app name, existing folders, valid target dirs),
// the validator should return success
bool testValidConfigurationAlwaysPasses() {
    std::cout << "Testing Property: Valid Configuration Always Passes..." << std::endl;
    
    const int NUM_ITERATIONS = 100;
    int successCount = 0;
    RandomGenerator rng;
    
    try {
        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
            PropertyTestHelper helper;
            
            // 创建完全有效的配置
            PackagerConfiguration config;
            config.applicationName = "ValidApp_" + rng.getString(8);
            config.defaultInstallDir = rng.getValidDirectoryType();
            
            // 添加一些有效的文件夹目标
            int numFolders = rng.getInt() % 5 + 1;  // 1-5个文件夹
            for (int i = 0; i < numFolders; ++i) {
                std::string folderName = "folder_" + std::to_string(i);
                helper.createFolder(folderName);
                
                FolderTargetConfig ftc;
                ftc.folderName = folderName;
                ftc.targetDirectory = rng.getValidDirectoryType();
                config.folderTargets.push_back(ftc);
            }
            
            // 验证配置
            ConfigurationValidator validator;
            auto result = validator.validate(config, helper.getTestDir());
            
            // 验证通过
            if (!result.isValid) {
                std::cerr << "  Iteration " << iteration << " failed: Valid config should pass validation" << std::endl;
                std::cerr << "  Errors: " << std::endl;
                for (const auto& error : result.errors) {
                    std::cerr << "    " << error << std::endl;
                }
                continue;
            }
            
            // 验证没有错误
            if (!result.errors.empty()) {
                std::cerr << "  Iteration " << iteration << " failed: Valid config should have no errors" << std::endl;
                continue;
            }
            
            successCount++;
        }
        
        std::cout << "  Passed " << successCount << "/" << NUM_ITERATIONS << " iterations" << std::endl;
        
        if (successCount == NUM_ITERATIONS) {
            std::cout << "✓ Valid Configuration Always Passes property test passed" << std::endl;
            return true;
        } else {
            std::cerr << "✗ Valid Configuration Always Passes property test failed" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Valid Configuration Always Passes property test failed with exception: " << e.what() << std::endl;
        return false;
    }
}

// 测试非法字符检测
// Property: For any application name containing illegal characters,
// the validator should return an error
bool testIllegalCharacterDetection() {
    std::cout << "Testing Property: Illegal Character Detection..." << std::endl;
    
    const int NUM_ITERATIONS = 100;
    int successCount = 0;
    RandomGenerator rng;
    
    try {
        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
            PropertyTestHelper helper;
            
            // 创建包含非法字符的应用程序名
            std::string validPart = rng.getString(5);
            char illegalChar = rng.getIllegalChar();
            std::string appName = validPart + std::string(1, illegalChar) + rng.getString(3);
            
            PackagerConfiguration config;
            config.applicationName = appName;
            
            // 验证配置
            ConfigurationValidator validator;
            auto result = validator.validate(config, helper.getTestDir());
            
            // 验证返回错误
            if (result.isValid) {
                std::cerr << "  Iteration " << iteration << " failed: App name with illegal char should fail" << std::endl;
                std::cerr << "  App name: " << appName << std::endl;
                continue;
            }
            
            // 验证错误信息提到了非法字符
            bool hasIllegalCharError = false;
            for (const auto& error : result.errors) {
                if (error.find("illegal") != std::string::npos ||
                    error.find("Invalid") != std::string::npos) {
                    hasIllegalCharError = true;
                    break;
                }
            }
            
            if (!hasIllegalCharError) {
                std::cerr << "  Iteration " << iteration << " failed: Error should mention illegal character" << std::endl;
                continue;
            }
            
            successCount++;
        }
        
        std::cout << "  Passed " << successCount << "/" << NUM_ITERATIONS << " iterations" << std::endl;
        
        if (successCount == NUM_ITERATIONS) {
            std::cout << "✓ Illegal Character Detection property test passed" << std::endl;
            return true;
        } else {
            std::cerr << "✗ Illegal Character Detection property test failed" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Illegal Character Detection property test failed with exception: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Running Configuration Validator Property-Based Tests" << std::endl;
    std::cout << "=====================================================" << std::endl;
    
    bool allTestsPassed = true;
    
    if (!testApplicationNameRequirement()) {
        allTestsPassed = false;
    }
    
    if (!testFolderExistenceValidation()) {
        allTestsPassed = false;
    }
    
    if (!testConfigurationValidationCompleteness()) {
        allTestsPassed = false;
    }
    
    if (!testValidConfigurationAlwaysPasses()) {
        allTestsPassed = false;
    }
    
    if (!testIllegalCharacterDetection()) {
        allTestsPassed = false;
    }
    
    std::cout << "=====================================================" << std::endl;
    if (allTestsPassed) {
        std::cout << "All configuration validator property-based tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some configuration validator property-based tests failed!" << std::endl;
        return 1;
    }
}
