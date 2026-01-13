#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include "packager/configuration_loader.h"

namespace fs = std::filesystem;
using namespace MultiThreadedInstaller;

// 测试辅助类
class TestHelper {
public:
    TestHelper() {
        // 创建临时测试目录
        testDir = fs::temp_directory_path() / "config_loader_test";
        fs::create_directories(testDir);
    }
    
    ~TestHelper() {
        // 清理测试目录
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
    }
    
    // 创建测试配置文件
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

// 测试成功加载有效配置文件
bool testLoadValidConfiguration() {
    std::cout << "Testing load valid configuration..." << std::endl;
    
    try {
        TestHelper helper;
        std::string validConfig = R"({
            "applicationName": "TestApp",
            "defaultInstallDirectory": "%ProgramFiles%",
            "compressionAlgorithm": "zstd",
            "folderTargets": [
                {
                    "folder": "app",
                    "targetDirectory": "installDirectory"
                },
                {
                    "folder": "plugin",
                    "targetDirectory": "%AppData%\\Roaming"
                }
            ]
        })";
        
        helper.createConfigFile("packager.json", validConfig);
        
        ConfigurationLoader loader;
        auto config = loader.loadConfiguration(helper.getTestDir());
        
        assert(config.has_value());
        assert(config->applicationName == "TestApp");
        assert(config->defaultInstallDir == "%ProgramFiles%");
        assert(config->compressionAlgorithm == CompressionAlgorithm::ZSTD_FAST);
        assert(config->folderTargets.size() == 2);
        assert(config->folderTargets[0].folderName == "app");
        assert(config->folderTargets[0].dirType == SpecialDirectoryType::INSTALL_DIRECTORY);
        assert(config->folderTargets[1].folderName == "plugin");
        assert(config->folderTargets[1].dirType == SpecialDirectoryType::APPDATA_ROAMING);
        
        std::cout << "✓ Load valid configuration test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Load valid configuration test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试配置文件不存在的情况
bool testNoConfigurationFile() {
    std::cout << "Testing no configuration file..." << std::endl;
    
    try {
        TestHelper helper;
        ConfigurationLoader loader;
        auto config = loader.loadConfiguration(helper.getTestDir());
        
        // 配置文件不存在应该返回nullopt，不是错误
        assert(!config.has_value());
        assert(loader.getLastError().empty());
        
        std::cout << "✓ No configuration file test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ No configuration file test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试无效JSON格式
bool testInvalidJsonFormat() {
    std::cout << "Testing invalid JSON format..." << std::endl;
    
    try {
        TestHelper helper;
        std::string invalidJson = R"({
            "applicationName": "TestApp",
            "compressionAlgorithm": "zstd"
        )";  // 缺少闭合括号
        
        helper.createConfigFile("packager.json", invalidJson);
        
        ConfigurationLoader loader;
        auto config = loader.loadConfiguration(helper.getTestDir());
        
        assert(!config.has_value());
        assert(!loader.getLastError().empty());
        assert(loader.getLastError().find("parse error") != std::string::npos);
        
        std::cout << "✓ Invalid JSON format test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Invalid JSON format test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试配置文件优先级
bool testConfigurationFilePriority() {
    std::cout << "Testing configuration file priority..." << std::endl;
    
    try {
        TestHelper helper;
        std::string config1 = R"({
            "applicationName": "App1"
        })";
        
        std::string config2 = R"({
            "applicationName": "App2"
        })";
        
        helper.createConfigFile("packager.json", config1);
        helper.createConfigFile(".packager.json", config2);
        
        ConfigurationLoader loader;
        auto config = loader.loadConfiguration(helper.getTestDir());
        
        assert(config.has_value());
        // 应该加载packager.json（优先级更高）
        assert(config->applicationName == "App1");
        
        std::cout << "✓ Configuration file priority test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Configuration file priority test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试只有.packager.json的情况
bool testLoadDotPackagerJson() {
    std::cout << "Testing load .packager.json..." << std::endl;
    
    try {
        TestHelper helper;
        std::string validConfig = R"({
            "applicationName": "DotApp"
        })";
        
        helper.createConfigFile(".packager.json", validConfig);
        
        ConfigurationLoader loader;
        auto config = loader.loadConfiguration(helper.getTestDir());
        
        assert(config.has_value());
        assert(config->applicationName == "DotApp");
        
        std::cout << "✓ Load .packager.json test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Load .packager.json test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试无效的压缩算法
bool testInvalidCompressionAlgorithm() {
    std::cout << "Testing invalid compression algorithm..." << std::endl;
    
    try {
        TestHelper helper;
        std::string invalidConfig = R"({
            "applicationName": "TestApp",
            "compressionAlgorithm": "invalid"
        })";
        
        helper.createConfigFile("packager.json", invalidConfig);
        
        ConfigurationLoader loader;
        auto config = loader.loadConfiguration(helper.getTestDir());
        
        assert(!config.has_value());
        assert(!loader.getLastError().empty());
        assert(loader.getLastError().find("Invalid compression algorithm") != std::string::npos);
        
        std::cout << "✓ Invalid compression algorithm test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Invalid compression algorithm test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试LZMA压缩算法
bool testLzmaCompressionAlgorithm() {
    std::cout << "Testing LZMA compression algorithm..." << std::endl;
    
    try {
        TestHelper helper;
        std::string config = R"({
            "applicationName": "TestApp",
            "compressionAlgorithm": "lzma"
        })";
        
        helper.createConfigFile("packager.json", config);
        
        ConfigurationLoader loader;
        auto result = loader.loadConfiguration(helper.getTestDir());
        
        assert(result.has_value());
        assert(result->compressionAlgorithm == CompressionAlgorithm::LZMA_HIGH);
        
        std::cout << "✓ LZMA compression algorithm test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ LZMA compression algorithm test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试默认值
bool testDefaultValues() {
    std::cout << "Testing default values..." << std::endl;
    
    try {
        TestHelper helper;
        std::string minimalConfig = R"({
            "applicationName": "TestApp"
        })";
        
        helper.createConfigFile("packager.json", minimalConfig);
        
        ConfigurationLoader loader;
        auto config = loader.loadConfiguration(helper.getTestDir());
        
        assert(config.has_value());
        assert(config->applicationName == "TestApp");
        // 应该使用默认值
        assert(config->defaultInstallDir == "%ProgramFiles%");
        assert(config->compressionAlgorithm == CompressionAlgorithm::ZSTD_FAST);
        assert(config->folderTargets.empty());
        
        std::cout << "✓ Default values test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Default values test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试parseDirectoryType方法
bool testParseDirectoryTypes() {
    std::cout << "Testing parse directory types..." << std::endl;
    
    try {
        TestHelper helper;
        std::string config = R"({
            "applicationName": "TestApp",
            "folderTargets": [
                {
                    "folder": "app",
                    "targetDirectory": "installDirectory"
                },
                {
                    "folder": "data1",
                    "targetDirectory": "%ProgramFiles%"
                },
                {
                    "folder": "data2",
                    "targetDirectory": "%AppData%\\Roaming"
                },
                {
                    "folder": "data3",
                    "targetDirectory": "%LocalAppData%"
                },
                {
                    "folder": "data4",
                    "targetDirectory": "%ProgramData%"
                }
            ]
        })";
        
        helper.createConfigFile("packager.json", config);
        
        ConfigurationLoader loader;
        auto result = loader.loadConfiguration(helper.getTestDir());
        
        assert(result.has_value());
        assert(result->folderTargets.size() == 5);
        
        assert(result->folderTargets[0].dirType == SpecialDirectoryType::INSTALL_DIRECTORY);
        assert(result->folderTargets[1].dirType == SpecialDirectoryType::PROGRAM_FILES);
        assert(result->folderTargets[2].dirType == SpecialDirectoryType::APPDATA_ROAMING);
        assert(result->folderTargets[3].dirType == SpecialDirectoryType::APPDATA_LOCAL);
        assert(result->folderTargets[4].dirType == SpecialDirectoryType::PROGRAM_DATA);
        
        std::cout << "✓ Parse directory types test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Parse directory types test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试从指定路径加载配置
bool testLoadFromSpecificPath() {
    std::cout << "Testing load from specific path..." << std::endl;
    
    try {
        TestHelper helper;
        std::string config = R"({
            "applicationName": "SpecificApp"
        })";
        
        fs::path configPath = fs::path(helper.getTestDir()) / "custom_config.json";
        std::ofstream file(configPath);
        file << config;
        file.close();
        
        ConfigurationLoader loader;
        auto result = loader.loadConfigurationFromPath(configPath.string());
        
        assert(result.has_value());
        assert(result->applicationName == "SpecificApp");
        assert(loader.getLoadedConfigPath() == configPath.string());
        
        std::cout << "✓ Load from specific path test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Load from specific path test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试空的folderTargets数组
bool testEmptyFolderTargets() {
    std::cout << "Testing empty folder targets..." << std::endl;
    
    try {
        TestHelper helper;
        std::string config = R"({
            "applicationName": "TestApp",
            "folderTargets": []
        })";
        
        helper.createConfigFile("packager.json", config);
        
        ConfigurationLoader loader;
        auto result = loader.loadConfiguration(helper.getTestDir());
        
        assert(result.has_value());
        assert(result->folderTargets.empty());
        
        std::cout << "✓ Empty folder targets test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Empty folder targets test failed: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Running Configuration Loader Tests" << std::endl;
    std::cout << "===================================" << std::endl;
    
    bool allTestsPassed = true;
    
    if (!testLoadValidConfiguration()) {
        allTestsPassed = false;
    }
    
    if (!testNoConfigurationFile()) {
        allTestsPassed = false;
    }
    
    if (!testInvalidJsonFormat()) {
        allTestsPassed = false;
    }
    
    if (!testConfigurationFilePriority()) {
        allTestsPassed = false;
    }
    
    if (!testLoadDotPackagerJson()) {
        allTestsPassed = false;
    }
    
    if (!testInvalidCompressionAlgorithm()) {
        allTestsPassed = false;
    }
    
    if (!testLzmaCompressionAlgorithm()) {
        allTestsPassed = false;
    }
    
    if (!testDefaultValues()) {
        allTestsPassed = false;
    }
    
    if (!testParseDirectoryTypes()) {
        allTestsPassed = false;
    }
    
    if (!testLoadFromSpecificPath()) {
        allTestsPassed = false;
    }
    
    if (!testEmptyFolderTargets()) {
        allTestsPassed = false;
    }
    
    std::cout << "===================================" << std::endl;
    if (allTestsPassed) {
        std::cout << "All configuration loader tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some configuration loader tests failed!" << std::endl;
        return 1;
    }
}
