#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>
#include <string>
#include "packager/configuration_loader.h"

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
    
    std::string getCompressionAlgorithm() {
        return getBool() ? "zstd" : "lzma";
    }
    
    std::string getDirectoryType() {
        std::vector<std::string> types = {
            "installDirectory",
            "%ProgramFiles%",
            "%AppData%\\\\Roaming",
            "%LocalAppData%",
            "%ProgramData%"
        };
        return types[dist(gen) % types.size()];
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
        testDir = fs::temp_directory_path() / ("pbt_config_test_" + std::to_string(std::random_device{}()));
        fs::create_directories(testDir);
    }
    
    ~PropertyTestHelper() {
        // 清理测试目录
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
    }
    
    void createConfigFile(const std::string& filename, const std::string& content) {
        fs::path configPath = testDir / filename;
        std::ofstream file(configPath);
        file << content;
        file.close();
    }
    
    std::string getTestDir() const {
        return testDir.string();
    }
    
private:
    fs::path testDir;
};

// 生成随机的有效配置JSON
std::string generateRandomValidConfig(RandomGenerator& rng) {
    std::string appName = rng.getString(8);
    std::string compressionAlgo = rng.getCompressionAlgorithm();
    std::string defaultInstallDir = rng.getDirectoryType();
    
    std::string json = "{\n";
    json += "  \"applicationName\": \"" + appName + "\",\n";
    json += "  \"defaultInstallDirectory\": \"" + defaultInstallDir + "\",\n";
    json += "  \"compressionAlgorithm\": \"" + compressionAlgo + "\",\n";
    json += "  \"folderTargets\": [\n";
    
    // 生成随机数量的文件夹目标（0-5个）
    int numTargets = rng.getInt() % 6;
    for (int i = 0; i < numTargets; ++i) {
        std::string folderName = "folder_" + std::to_string(i);
        std::string targetDir = rng.getDirectoryType();
        
        json += "    {\n";
        json += "      \"folder\": \"" + folderName + "\",\n";
        json += "      \"targetDirectory\": \"" + targetDir + "\"\n";
        json += "    }";
        
        if (i < numTargets - 1) {
            json += ",";
        }
        json += "\n";
    }
    
    json += "  ]\n";
    json += "}\n";
    
    return json;
}

// Feature: packager-config-file, Property 1: Configuration File Discovery and Parsing
// Validates: Requirements 1.1, 7.5, 8.1
// Property: For any valid configuration file in the input directory, the loader should
// be able to discover and successfully parse it, reading all configuration options.
bool testConfigurationFileDiscoveryAndParsing() {
    std::cout << "Testing Property 1: Configuration File Discovery and Parsing..." << std::endl;
    std::cout << "Feature: packager-config-file, Property 1" << std::endl;
    std::cout << "Validates: Requirements 1.1, 7.5, 8.1" << std::endl;
    
    const int NUM_ITERATIONS = 100;
    int successCount = 0;
    RandomGenerator rng;
    
    try {
        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
            PropertyTestHelper helper;
            
            // 生成随机的有效配置
            std::string configJson = generateRandomValidConfig(rng);
            
            // 随机选择配置文件名（packager.json 或 .packager.json）
            std::string configFileName = rng.getBool() ? "packager.json" : ".packager.json";
            helper.createConfigFile(configFileName, configJson);
            
            // 尝试加载配置
            ConfigurationLoader loader;
            auto config = loader.loadConfiguration(helper.getTestDir());
            
            // 验证配置被成功加载
            if (!config.has_value()) {
                std::cerr << "  Iteration " << iteration << " failed: Configuration not loaded" << std::endl;
                std::cerr << "  Error: " << loader.getLastError() << std::endl;
                std::cerr << "  Config JSON: " << configJson << std::endl;
                continue;
            }
            
            // 验证配置文件路径被记录
            if (loader.getLoadedConfigPath().empty()) {
                std::cerr << "  Iteration " << iteration << " failed: Config path not recorded" << std::endl;
                continue;
            }
            
            // 验证应用程序名称不为空
            if (config->applicationName.empty()) {
                std::cerr << "  Iteration " << iteration << " failed: Application name is empty" << std::endl;
                continue;
            }
            
            // 验证压缩算法是有效的
            if (config->compressionAlgorithm != CompressionAlgorithm::ZSTD_FAST &&
                config->compressionAlgorithm != CompressionAlgorithm::LZMA_HIGH) {
                std::cerr << "  Iteration " << iteration << " failed: Invalid compression algorithm" << std::endl;
                continue;
            }
            
            successCount++;
        }
        
        std::cout << "  Passed " << successCount << "/" << NUM_ITERATIONS << " iterations" << std::endl;
        
        if (successCount == NUM_ITERATIONS) {
            std::cout << "✓ Property 1: Configuration File Discovery and Parsing test passed" << std::endl;
            return true;
        } else {
            std::cerr << "✗ Property 1: Configuration File Discovery and Parsing test failed" << std::endl;
            std::cerr << "  Only " << successCount << "/" << NUM_ITERATIONS << " iterations passed" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Property 1 test failed with exception: " << e.what() << std::endl;
        return false;
    }
}

// 测试配置文件优先级属性
// Property: For any directory containing both packager.json and .packager.json,
// packager.json should always be loaded (higher priority)
bool testConfigurationFilePriorityProperty() {
    std::cout << "Testing Property: Configuration File Priority..." << std::endl;
    
    const int NUM_ITERATIONS = 50;
    int successCount = 0;
    RandomGenerator rng;
    
    try {
        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
            PropertyTestHelper helper;
            
            // 生成两个不同的配置
            std::string appName1 = "App_" + rng.getString(5);
            std::string appName2 = "App_" + rng.getString(5);
            
            std::string config1 = "{\n  \"applicationName\": \"" + appName1 + "\"\n}\n";
            std::string config2 = "{\n  \"applicationName\": \"" + appName2 + "\"\n}\n";
            
            // 创建两个配置文件
            helper.createConfigFile("packager.json", config1);
            helper.createConfigFile(".packager.json", config2);
            
            // 加载配置
            ConfigurationLoader loader;
            auto config = loader.loadConfiguration(helper.getTestDir());
            
            // 验证加载了packager.json（优先级更高）
            if (!config.has_value() || config->applicationName != appName1) {
                std::cerr << "  Iteration " << iteration << " failed: Wrong config loaded" << std::endl;
                continue;
            }
            
            successCount++;
        }
        
        std::cout << "  Passed " << successCount << "/" << NUM_ITERATIONS << " iterations" << std::endl;
        
        if (successCount == NUM_ITERATIONS) {
            std::cout << "✓ Configuration File Priority property test passed" << std::endl;
            return true;
        } else {
            std::cerr << "✗ Configuration File Priority property test failed" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Configuration File Priority property test failed with exception: " << e.what() << std::endl;
        return false;
    }
}

// 测试目录类型解析属性
// Property: For any valid directory type string, parseDirectoryType should return
// a valid SpecialDirectoryType enum value
bool testDirectoryTypeParsingProperty() {
    std::cout << "Testing Property: Directory Type Parsing..." << std::endl;
    
    const int NUM_ITERATIONS = 100;
    int successCount = 0;
    RandomGenerator rng;
    
    try {
        for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
            PropertyTestHelper helper;
            
            // 生成包含随机目录类型的配置
            std::string dirType = rng.getDirectoryType();
            std::string config = "{\n";
            config += "  \"applicationName\": \"TestApp\",\n";
            config += "  \"folderTargets\": [\n";
            config += "    {\n";
            config += "      \"folder\": \"test\",\n";
            config += "      \"targetDirectory\": \"" + dirType + "\"\n";
            config += "    }\n";
            config += "  ]\n";
            config += "}\n";
            
            helper.createConfigFile("packager.json", config);
            
            // 加载配置
            ConfigurationLoader loader;
            auto result = loader.loadConfiguration(helper.getTestDir());
            
            // 验证配置被成功加载
            if (!result.has_value()) {
                std::cerr << "  Iteration " << iteration << " failed: Configuration not loaded" << std::endl;
                continue;
            }
            
            // 验证文件夹目标被正确解析
            if (result->folderTargets.empty()) {
                std::cerr << "  Iteration " << iteration << " failed: No folder targets" << std::endl;
                continue;
            }
            
            // 验证目录类型是有效的枚举值
            SpecialDirectoryType parsedType = result->folderTargets[0].dirType;
            bool isValidType = (
                parsedType == SpecialDirectoryType::INSTALL_DIRECTORY ||
                parsedType == SpecialDirectoryType::PROGRAM_FILES ||
                parsedType == SpecialDirectoryType::APPDATA_ROAMING ||
                parsedType == SpecialDirectoryType::APPDATA_LOCAL ||
                parsedType == SpecialDirectoryType::PROGRAM_DATA
            );
            
            if (!isValidType) {
                std::cerr << "  Iteration " << iteration << " failed: Invalid directory type" << std::endl;
                continue;
            }
            
            successCount++;
        }
        
        std::cout << "  Passed " << successCount << "/" << NUM_ITERATIONS << " iterations" << std::endl;
        
        if (successCount == NUM_ITERATIONS) {
            std::cout << "✓ Directory Type Parsing property test passed" << std::endl;
            return true;
        } else {
            std::cerr << "✗ Directory Type Parsing property test failed" << std::endl;
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Directory Type Parsing property test failed with exception: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Running Configuration Loader Property-Based Tests" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    bool allTestsPassed = true;
    
    if (!testConfigurationFileDiscoveryAndParsing()) {
        allTestsPassed = false;
    }
    
    if (!testConfigurationFilePriorityProperty()) {
        allTestsPassed = false;
    }
    
    if (!testDirectoryTypeParsingProperty()) {
        allTestsPassed = false;
    }
    
    std::cout << "==================================================" << std::endl;
    if (allTestsPassed) {
        std::cout << "All configuration loader property-based tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some configuration loader property-based tests failed!" << std::endl;
        return 1;
    }
}
