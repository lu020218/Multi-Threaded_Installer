#include <iostream>
#include <cassert>
#include <filesystem>
#include "packager/configuration_validator.h"

namespace fs = std::filesystem;
using namespace MultiThreadedInstaller;

// 测试辅助类
class TestHelper {
public:
    TestHelper() {
        // 创建临时测试目录
        testDir = fs::temp_directory_path() / "config_validator_test";
        fs::create_directories(testDir);
    }
    
    ~TestHelper() {
        // 清理测试目录
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
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

// 测试缺少应用程序名的配置
bool testMissingApplicationName() {
    std::cout << "Testing missing application name..." << std::endl;
    
    try {
        TestHelper helper;
        PackagerConfiguration config;
        config.applicationName = "";  // 空应用程序名
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(!result.isValid);
        assert(!result.errors.empty());
        assert(result.errors[0].find("applicationName") != std::string::npos);
        
        std::cout << "✓ Missing application name test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Missing application name test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试应用程序名包含非法字符
bool testInvalidApplicationNameCharacters() {
    std::cout << "Testing invalid application name characters..." << std::endl;
    
    try {
        TestHelper helper;
        
        // 测试各种非法字符
        std::vector<std::string> invalidNames = {
            "App<Name",
            "App>Name",
            "App:Name",
            "App\"Name",
            "App/Name",
            "App\\Name",
            "App|Name",
            "App?Name",
            "App*Name"
        };
        
        ConfigurationValidator validator;
        
        for (const auto& name : invalidNames) {
            PackagerConfiguration config;
            config.applicationName = name;
            
            auto result = validator.validate(config, helper.getTestDir());
            
            assert(!result.isValid);
            assert(!result.errors.empty());
        }
        
        std::cout << "✓ Invalid application name characters test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Invalid application name characters test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试有效的应用程序名
bool testValidApplicationName() {
    std::cout << "Testing valid application name..." << std::endl;
    
    try {
        TestHelper helper;
        PackagerConfiguration config;
        config.applicationName = "MyApplication";
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(result.isValid);
        assert(result.errors.empty());
        
        std::cout << "✓ Valid application name test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Valid application name test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试文件夹不存在的情况
bool testFolderDoesNotExist() {
    std::cout << "Testing folder does not exist..." << std::endl;
    
    try {
        TestHelper helper;
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        
        FolderTargetConfig ftc;
        ftc.folderName = "nonexistent_folder";
        ftc.targetDirectory = "installDirectory";
        config.folderTargets.push_back(ftc);
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(!result.isValid);
        assert(!result.errors.empty());
        assert(result.errors[0].find("does not exist") != std::string::npos);
        
        std::cout << "✓ Folder does not exist test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Folder does not exist test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试文件夹存在的情况
bool testFolderExists() {
    std::cout << "Testing folder exists..." << std::endl;
    
    try {
        TestHelper helper;
        helper.createFolder("app");
        
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        
        FolderTargetConfig ftc;
        ftc.folderName = "app";
        ftc.targetDirectory = "installDirectory";
        config.folderTargets.push_back(ftc);
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(result.isValid);
        assert(result.errors.empty());
        
        std::cout << "✓ Folder exists test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Folder exists test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试空文件夹名
bool testEmptyFolderName() {
    std::cout << "Testing empty folder name..." << std::endl;
    
    try {
        TestHelper helper;
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        
        FolderTargetConfig ftc;
        ftc.folderName = "";
        ftc.targetDirectory = "installDirectory";
        config.folderTargets.push_back(ftc);
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(!result.isValid);
        assert(!result.errors.empty());
        assert(result.errors[0].find("Empty folder name") != std::string::npos);
        
        std::cout << "✓ Empty folder name test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Empty folder name test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试无效的目标目录配置
bool testInvalidTargetDirectory() {
    std::cout << "Testing invalid target directory..." << std::endl;
    
    try {
        TestHelper helper;
        helper.createFolder("app");
        
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        
        FolderTargetConfig ftc;
        ftc.folderName = "app";
        ftc.targetDirectory = "";  // 空目标目录
        config.folderTargets.push_back(ftc);
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(!result.isValid);
        assert(!result.errors.empty());
        assert(result.errors[0].find("Empty target directory") != std::string::npos);
        
        std::cout << "✓ Invalid target directory test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Invalid target directory test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试有效的目标目录配置
bool testValidTargetDirectories() {
    std::cout << "Testing valid target directories..." << std::endl;
    
    try {
        TestHelper helper;
        helper.createFolder("app");
        helper.createFolder("plugin");
        helper.createFolder("config");
        
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        
        // installDirectory
        FolderTargetConfig ftc1;
        ftc1.folderName = "app";
        ftc1.targetDirectory = "installDirectory";
        config.folderTargets.push_back(ftc1);
        
        // 环境变量
        FolderTargetConfig ftc2;
        ftc2.folderName = "plugin";
        ftc2.targetDirectory = "%AppData%\\Roaming";
        config.folderTargets.push_back(ftc2);
        
        FolderTargetConfig ftc3;
        ftc3.folderName = "config";
        ftc3.targetDirectory = "%ProgramData%";
        config.folderTargets.push_back(ftc3);
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(result.isValid);
        assert(result.errors.empty());
        
        std::cout << "✓ Valid target directories test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Valid target directories test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试无效的环境变量
bool testInvalidEnvironmentVariable() {
    std::cout << "Testing invalid environment variable..." << std::endl;
    
    try {
        TestHelper helper;
        helper.createFolder("app");
        
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        
        FolderTargetConfig ftc;
        ftc.folderName = "app";
        ftc.targetDirectory = "%InvalidEnvVar%";
        config.folderTargets.push_back(ftc);
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(!result.isValid);
        assert(!result.errors.empty());
        assert(result.errors[0].find("Invalid environment variable") != std::string::npos);
        
        std::cout << "✓ Invalid environment variable test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Invalid environment variable test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试目标目录包含非法字符
bool testTargetDirectoryWithIllegalCharacters() {
    std::cout << "Testing target directory with illegal characters..." << std::endl;
    
    try {
        TestHelper helper;
        helper.createFolder("app");
        
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        
        FolderTargetConfig ftc;
        ftc.folderName = "app";
        ftc.targetDirectory = "C:\\Path<With>Illegal|Chars";
        config.folderTargets.push_back(ftc);
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(!result.isValid);
        assert(!result.errors.empty());
        
        std::cout << "✓ Target directory with illegal characters test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Target directory with illegal characters test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试有效配置通过验证
bool testValidConfigurationPassesValidation() {
    std::cout << "Testing valid configuration passes validation..." << std::endl;
    
    try {
        TestHelper helper;
        helper.createFolder("app");
        helper.createFolder("plugin");
        
        PackagerConfiguration config;
        config.applicationName = "MyApplication";
        config.defaultInstallDir = "%ProgramFiles%";
        config.compressionAlgorithm = CompressionAlgorithm::ZSTD_FAST;
        
        FolderTargetConfig ftc1;
        ftc1.folderName = "app";
        ftc1.targetDirectory = "installDirectory";
        config.folderTargets.push_back(ftc1);
        
        FolderTargetConfig ftc2;
        ftc2.folderName = "plugin";
        ftc2.targetDirectory = "%AppData%\\Roaming";
        config.folderTargets.push_back(ftc2);
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(result.isValid);
        assert(result.errors.empty());
        assert(result.warnings.empty());
        
        std::cout << "✓ Valid configuration passes validation test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Valid configuration passes validation test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试多个错误的累积
bool testMultipleErrors() {
    std::cout << "Testing multiple errors..." << std::endl;
    
    try {
        TestHelper helper;
        
        PackagerConfiguration config;
        config.applicationName = "";  // 错误1: 空应用程序名
        
        FolderTargetConfig ftc1;
        ftc1.folderName = "nonexistent";  // 错误2: 文件夹不存在
        ftc1.targetDirectory = "installDirectory";
        config.folderTargets.push_back(ftc1);
        
        FolderTargetConfig ftc2;
        ftc2.folderName = "";  // 错误3: 空文件夹名
        ftc2.targetDirectory = "";  // 错误4: 空目标目录
        config.folderTargets.push_back(ftc2);
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(!result.isValid);
        assert(result.errors.size() >= 3);  // 至少3个错误
        
        std::cout << "✓ Multiple errors test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Multiple errors test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试所有支持的环境变量
bool testAllSupportedEnvironmentVariables() {
    std::cout << "Testing all supported environment variables..." << std::endl;
    
    try {
        TestHelper helper;
        helper.createFolder("folder1");
        helper.createFolder("folder2");
        helper.createFolder("folder3");
        helper.createFolder("folder4");
        helper.createFolder("folder5");
        helper.createFolder("folder6");
        
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        
        std::vector<std::string> envVars = {
            "%ProgramFiles%",
            "%ProgramFiles(x86)%",
            "%AppData%",
            "%LocalAppData%",
            "%ProgramData%",
            "%USERPROFILE%"
        };
        
        for (size_t i = 0; i < envVars.size(); ++i) {
            FolderTargetConfig ftc;
            ftc.folderName = "folder" + std::to_string(i + 1);
            ftc.targetDirectory = envVars[i];
            config.folderTargets.push_back(ftc);
        }
        
        ConfigurationValidator validator;
        auto result = validator.validate(config, helper.getTestDir());
        
        assert(result.isValid);
        assert(result.errors.empty());
        
        std::cout << "✓ All supported environment variables test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ All supported environment variables test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试默认安装目录验证
bool testDefaultInstallDirectoryValidation() {
    std::cout << "Testing default install directory validation..." << std::endl;
    
    try {
        TestHelper helper;
        
        // 测试有效的默认安装目录
        PackagerConfiguration config1;
        config1.applicationName = "TestApp";
        config1.defaultInstallDir = "%ProgramFiles%";
        
        ConfigurationValidator validator;
        auto result1 = validator.validate(config1, helper.getTestDir());
        
        assert(result1.isValid);
        assert(result1.errors.empty());
        
        // 测试无效的默认安装目录
        PackagerConfiguration config2;
        config2.applicationName = "TestApp";
        config2.defaultInstallDir = "%InvalidVar%";
        
        auto result2 = validator.validate(config2, helper.getTestDir());
        
        assert(!result2.isValid);
        assert(!result2.errors.empty());
        
        std::cout << "✓ Default install directory validation test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Default install directory validation test failed: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Running Configuration Validator Tests" << std::endl;
    std::cout << "======================================" << std::endl;
    
    bool allTestsPassed = true;
    
    if (!testMissingApplicationName()) {
        allTestsPassed = false;
    }
    
    if (!testInvalidApplicationNameCharacters()) {
        allTestsPassed = false;
    }
    
    if (!testValidApplicationName()) {
        allTestsPassed = false;
    }
    
    if (!testFolderDoesNotExist()) {
        allTestsPassed = false;
    }
    
    if (!testFolderExists()) {
        allTestsPassed = false;
    }
    
    if (!testEmptyFolderName()) {
        allTestsPassed = false;
    }
    
    if (!testInvalidTargetDirectory()) {
        allTestsPassed = false;
    }
    
    if (!testValidTargetDirectories()) {
        allTestsPassed = false;
    }
    
    if (!testInvalidEnvironmentVariable()) {
        allTestsPassed = false;
    }
    
    if (!testTargetDirectoryWithIllegalCharacters()) {
        allTestsPassed = false;
    }
    
    if (!testValidConfigurationPassesValidation()) {
        allTestsPassed = false;
    }
    
    if (!testMultipleErrors()) {
        allTestsPassed = false;
    }
    
    if (!testAllSupportedEnvironmentVariables()) {
        allTestsPassed = false;
    }
    
    if (!testDefaultInstallDirectoryValidation()) {
        allTestsPassed = false;
    }
    
    std::cout << "======================================" << std::endl;
    if (allTestsPassed) {
        std::cout << "All configuration validator tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some configuration validator tests failed!" << std::endl;
        return 1;
    }
}
