#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include "packager/configuration_manager.h"

namespace fs = std::filesystem;
using namespace MultiThreadedInstaller;

// 测试辅助类
class TestHelper {
public:
    TestHelper() {
        // 创建临时测试目录
        testDir = fs::temp_directory_path() / "config_manager_test";
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
    
    // 创建测试文件夹
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

// 测试初始化成功（有配置文件）
bool testInitializeWithConfigFile() {
    std::cout << "Testing initialize with config file..." << std::endl;
    
    try {
        TestHelper helper;
        
        // 创建测试文件夹
        helper.createFolder("app");
        helper.createFolder("plugin");
        
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
        
        ConfigurationManager manager;
        bool result = manager.initialize(helper.getTestDir());
        
        assert(result == true);
        assert(manager.hasConfigFile() == true);
        assert(!manager.getConfigFilePath().empty());
        assert(manager.getConfiguration().applicationName == "TestApp");
        assert(manager.getConfiguration().folderTargets.size() == 2);
        assert(manager.getLastError().empty());
        
        std::cout << "✓ Initialize with config file test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Initialize with config file test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试初始化成功（无配置文件，使用默认值）
bool testInitializeWithoutConfigFile() {
    std::cout << "Testing initialize without config file..." << std::endl;
    
    try {
        TestHelper helper;
        
        ConfigurationManager manager;
        bool result = manager.initialize(helper.getTestDir());
        
        assert(result == true);
        assert(manager.hasConfigFile() == false);
        assert(manager.getConfigFilePath().empty());
        assert(manager.getConfiguration().applicationName == "MyApplication");
        assert(manager.getConfiguration().defaultInstallDir == "%ProgramFiles%");
        assert(manager.getConfiguration().compressionAlgorithm == CompressionAlgorithm::ZSTD_FAST);
        assert(manager.getLastError().empty());
        
        std::cout << "✓ Initialize without config file test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Initialize without config file test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试初始化失败（配置验证失败）
bool testInitializeWithInvalidConfig() {
    std::cout << "Testing initialize with invalid config..." << std::endl;
    
    try {
        TestHelper helper;
        
        // 创建无效配置（缺少applicationName）
        std::string invalidConfig = R"({
            "defaultInstallDirectory": "%ProgramFiles%",
            "compressionAlgorithm": "zstd"
        })";
        
        helper.createConfigFile("packager.json", invalidConfig);
        
        ConfigurationManager manager;
        bool result = manager.initialize(helper.getTestDir());
        
        assert(result == false);
        assert(!manager.getLastError().empty());
        std::cout << "Error message: " << manager.getLastError() << std::endl;
        // The error should be about missing applicationName from the loader
        assert(manager.getLastError().find("applicationName") != std::string::npos);
        
        std::cout << "✓ Initialize with invalid config test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Initialize with invalid config test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试初始化失败（JSON解析错误）
bool testInitializeWithMalformedJson() {
    std::cout << "Testing initialize with malformed JSON..." << std::endl;
    
    try {
        TestHelper helper;
        
        // 创建格式错误的JSON
        std::string malformedJson = R"({
            "applicationName": "TestApp",
            "compressionAlgorithm": "zstd"
        )";  // 缺少闭合括号
        
        helper.createConfigFile("packager.json", malformedJson);
        
        ConfigurationManager manager;
        bool result = manager.initialize(helper.getTestDir());
        
        assert(result == false);
        assert(!manager.getLastError().empty());
        
        std::cout << "✓ Initialize with malformed JSON test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Initialize with malformed JSON test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试应用文件夹目标配置
bool testApplyFolderTargets() {
    std::cout << "Testing apply folder targets..." << std::endl;
    
    try {
        TestHelper helper;
        
        // 创建测试文件夹
        helper.createFolder("app");
        helper.createFolder("plugin");
        helper.createFolder("data");
        
        std::string config = R"({
            "applicationName": "TestApp",
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
        
        helper.createConfigFile("packager.json", config);
        
        ConfigurationManager manager;
        bool result = manager.initialize(helper.getTestDir());
        assert(result == true);
        
        // 创建文件夹信息列表
        std::vector<FolderInfo> folders;
        folders.push_back(FolderInfo(helper.getTestDir() + "/app", ""));
        folders.push_back(FolderInfo(helper.getTestDir() + "/plugin", ""));
        folders.push_back(FolderInfo(helper.getTestDir() + "/data", ""));
        
        // 应用文件夹目标配置
        manager.applyFolderTargets(folders);
        
        // 验证结果
        assert(folders[0].targetPath == "installDirectory");
        assert(folders[1].targetPath == "%AppData%\\Roaming");
        assert(folders[2].targetPath == "");  // 没有配置，保持原值
        
        std::cout << "✓ Apply folder targets test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Apply folder targets test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试应用空的文件夹目标配置
bool testApplyEmptyFolderTargets() {
    std::cout << "Testing apply empty folder targets..." << std::endl;
    
    try {
        TestHelper helper;
        
        std::string config = R"({
            "applicationName": "TestApp",
            "folderTargets": []
        })";
        
        helper.createConfigFile("packager.json", config);
        
        ConfigurationManager manager;
        bool result = manager.initialize(helper.getTestDir());
        assert(result == true);
        
        // 创建文件夹信息列表
        std::vector<FolderInfo> folders;
        folders.push_back(FolderInfo("test/app", "original_path"));
        
        // 应用文件夹目标配置
        manager.applyFolderTargets(folders);
        
        // 验证结果（应该保持原值）
        assert(folders[0].targetPath == "original_path");
        
        std::cout << "✓ Apply empty folder targets test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Apply empty folder targets test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试无配置文件时应用文件夹目标
bool testApplyFolderTargetsWithoutConfig() {
    std::cout << "Testing apply folder targets without config..." << std::endl;
    
    try {
        TestHelper helper;
        
        ConfigurationManager manager;
        bool result = manager.initialize(helper.getTestDir());
        assert(result == true);
        assert(manager.hasConfigFile() == false);
        
        // 创建文件夹信息列表
        std::vector<FolderInfo> folders;
        folders.push_back(FolderInfo("test/app", "original_path"));
        
        // 应用文件夹目标配置
        manager.applyFolderTargets(folders);
        
        // 验证结果（应该保持原值）
        assert(folders[0].targetPath == "original_path");
        
        std::cout << "✓ Apply folder targets without config test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Apply folder targets without config test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试配置文件路径获取
bool testGetConfigFilePath() {
    std::cout << "Testing get config file path..." << std::endl;
    
    try {
        TestHelper helper;
        
        std::string config = R"({
            "applicationName": "TestApp"
        })";
        
        helper.createConfigFile("packager.json", config);
        
        ConfigurationManager manager;
        bool result = manager.initialize(helper.getTestDir());
        assert(result == true);
        
        std::string configPath = manager.getConfigFilePath();
        assert(!configPath.empty());
        assert(configPath.find("packager.json") != std::string::npos);
        
        std::cout << "✓ Get config file path test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Get config file path test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试获取配置对象
bool testGetConfiguration() {
    std::cout << "Testing get configuration..." << std::endl;
    
    try {
        TestHelper helper;
        
        helper.createFolder("app");
        
        std::string config = R"({
            "applicationName": "MyTestApp",
            "defaultInstallDirectory": "%LocalAppData%",
            "compressionAlgorithm": "lzma",
            "folderTargets": [
                {
                    "folder": "app",
                    "targetDirectory": "installDirectory"
                }
            ]
        })";
        
        helper.createConfigFile("packager.json", config);
        
        ConfigurationManager manager;
        bool result = manager.initialize(helper.getTestDir());
        assert(result == true);
        
        const PackagerConfiguration& cfg = manager.getConfiguration();
        assert(cfg.applicationName == "MyTestApp");
        assert(cfg.defaultInstallDir == "%LocalAppData%");
        assert(cfg.compressionAlgorithm == CompressionAlgorithm::LZMA_HIGH);
        assert(cfg.folderTargets.size() == 1);
        assert(cfg.folderTargets[0].folderName == "app");
        
        std::cout << "✓ Get configuration test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Get configuration test failed: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Running Configuration Manager Tests" << std::endl;
    std::cout << "====================================" << std::endl;
    
    bool allTestsPassed = true;
    
    if (!testInitializeWithConfigFile()) {
        allTestsPassed = false;
    }
    
    if (!testInitializeWithoutConfigFile()) {
        allTestsPassed = false;
    }
    
    if (!testInitializeWithInvalidConfig()) {
        allTestsPassed = false;
    }
    
    if (!testInitializeWithMalformedJson()) {
        allTestsPassed = false;
    }
    
    if (!testApplyFolderTargets()) {
        allTestsPassed = false;
    }
    
    if (!testApplyEmptyFolderTargets()) {
        allTestsPassed = false;
    }
    
    if (!testApplyFolderTargetsWithoutConfig()) {
        allTestsPassed = false;
    }
    
    if (!testGetConfigFilePath()) {
        allTestsPassed = false;
    }
    
    if (!testGetConfiguration()) {
        allTestsPassed = false;
    }
    
    std::cout << "====================================" << std::endl;
    if (allTestsPassed) {
        std::cout << "All configuration manager tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some configuration manager tests failed!" << std::endl;
        return 1;
    }
}
