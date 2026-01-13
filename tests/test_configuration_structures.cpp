#include <iostream>
#include <cassert>
#include "common/types.h"

using namespace MultiThreadedInstaller;

// 测试SpecialDirectoryType枚举
bool testSpecialDirectoryTypeEnum() {
    std::cout << "Testing SpecialDirectoryType enum..." << std::endl;
    
    try {
        // 验证枚举值可以被创建和赋值
        SpecialDirectoryType type1 = SpecialDirectoryType::INSTALL_DIRECTORY;
        SpecialDirectoryType type2 = SpecialDirectoryType::PROGRAM_FILES;
        SpecialDirectoryType type3 = SpecialDirectoryType::APPDATA_ROAMING;
        SpecialDirectoryType type4 = SpecialDirectoryType::APPDATA_LOCAL;
        SpecialDirectoryType type5 = SpecialDirectoryType::PROGRAM_DATA;
        
        // 验证枚举值可以被比较
        assert(type1 != type2);
        assert(type1 == SpecialDirectoryType::INSTALL_DIRECTORY);
        assert(type2 == SpecialDirectoryType::PROGRAM_FILES);
        assert(type3 == SpecialDirectoryType::APPDATA_ROAMING);
        assert(type4 == SpecialDirectoryType::APPDATA_LOCAL);
        assert(type5 == SpecialDirectoryType::PROGRAM_DATA);
        
        std::cout << "✓ SpecialDirectoryType enum test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ SpecialDirectoryType enum test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试FolderTargetConfig结构的默认值
bool testFolderTargetConfigDefaults() {
    std::cout << "Testing FolderTargetConfig default values..." << std::endl;
    
    try {
        // 创建默认的FolderTargetConfig
        FolderTargetConfig config;
        
        // 验证默认值
        assert(config.folderName.empty());
        assert(config.targetDirectory.empty());
        assert(config.dirType == SpecialDirectoryType::INSTALL_DIRECTORY);
        
        std::cout << "✓ FolderTargetConfig default values test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ FolderTargetConfig default values test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试FolderTargetConfig结构的初始化
bool testFolderTargetConfigInitialization() {
    std::cout << "Testing FolderTargetConfig initialization..." << std::endl;
    
    try {
        // 创建并初始化FolderTargetConfig
        FolderTargetConfig config;
        config.folderName = "app";
        config.targetDirectory = "installDirectory";
        config.dirType = SpecialDirectoryType::INSTALL_DIRECTORY;
        
        // 验证初始化的值
        assert(config.folderName == "app");
        assert(config.targetDirectory == "installDirectory");
        assert(config.dirType == SpecialDirectoryType::INSTALL_DIRECTORY);
        
        // 测试不同的目录类型
        FolderTargetConfig config2;
        config2.folderName = "plugin";
        config2.targetDirectory = "%AppData%\\Roaming";
        config2.dirType = SpecialDirectoryType::APPDATA_ROAMING;
        
        assert(config2.folderName == "plugin");
        assert(config2.targetDirectory == "%AppData%\\Roaming");
        assert(config2.dirType == SpecialDirectoryType::APPDATA_ROAMING);
        
        std::cout << "✓ FolderTargetConfig initialization test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ FolderTargetConfig initialization test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试PackagerConfiguration结构的默认值
bool testPackagerConfigurationDefaults() {
    std::cout << "Testing PackagerConfiguration default values..." << std::endl;
    
    try {
        // 创建默认的PackagerConfiguration
        PackagerConfiguration config;
        
        // 验证默认值 (Requirements: 1.6, 2.1, 3.1, 4.1)
        assert(config.applicationName == "MyApplication");
        assert(config.defaultInstallDir == "%ProgramFiles%");
        assert(config.compressionAlgorithm == CompressionAlgorithm::ZSTD_FAST);
        assert(config.folderTargets.empty());
        
        std::cout << "✓ PackagerConfiguration default values test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ PackagerConfiguration default values test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试PackagerConfiguration结构的初始化
bool testPackagerConfigurationInitialization() {
    std::cout << "Testing PackagerConfiguration initialization..." << std::endl;
    
    try {
        // 创建并初始化PackagerConfiguration
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        config.defaultInstallDir = "C:\\Program Files";
        config.compressionAlgorithm = CompressionAlgorithm::LZMA_HIGH;
        
        // 添加文件夹目标配置
        FolderTargetConfig folderTarget1;
        folderTarget1.folderName = "app";
        folderTarget1.targetDirectory = "installDirectory";
        folderTarget1.dirType = SpecialDirectoryType::INSTALL_DIRECTORY;
        config.folderTargets.push_back(folderTarget1);
        
        FolderTargetConfig folderTarget2;
        folderTarget2.folderName = "plugin";
        folderTarget2.targetDirectory = "%AppData%\\Roaming";
        folderTarget2.dirType = SpecialDirectoryType::APPDATA_ROAMING;
        config.folderTargets.push_back(folderTarget2);
        
        // 验证初始化的值
        assert(config.applicationName == "TestApp");
        assert(config.defaultInstallDir == "C:\\Program Files");
        assert(config.compressionAlgorithm == CompressionAlgorithm::LZMA_HIGH);
        assert(config.folderTargets.size() == 2);
        
        // 验证第一个文件夹目标
        assert(config.folderTargets[0].folderName == "app");
        assert(config.folderTargets[0].targetDirectory == "installDirectory");
        assert(config.folderTargets[0].dirType == SpecialDirectoryType::INSTALL_DIRECTORY);
        
        // 验证第二个文件夹目标
        assert(config.folderTargets[1].folderName == "plugin");
        assert(config.folderTargets[1].targetDirectory == "%AppData%\\Roaming");
        assert(config.folderTargets[1].dirType == SpecialDirectoryType::APPDATA_ROAMING);
        
        std::cout << "✓ PackagerConfiguration initialization test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ PackagerConfiguration initialization test failed: " << e.what() << std::endl;
        return false;
    }
}

// 测试PackagerConfiguration结构的复制
bool testPackagerConfigurationCopy() {
    std::cout << "Testing PackagerConfiguration copy..." << std::endl;
    
    try {
        // 创建原始配置
        PackagerConfiguration config1;
        config1.applicationName = "OriginalApp";
        config1.defaultInstallDir = "D:\\Apps";
        config1.compressionAlgorithm = CompressionAlgorithm::LZMA_HIGH;
        
        FolderTargetConfig folderTarget;
        folderTarget.folderName = "data";
        folderTarget.targetDirectory = "%ProgramData%";
        folderTarget.dirType = SpecialDirectoryType::PROGRAM_DATA;
        config1.folderTargets.push_back(folderTarget);
        
        // 复制配置
        PackagerConfiguration config2 = config1;
        
        // 验证复制的配置与原始配置相同
        assert(config2.applicationName == config1.applicationName);
        assert(config2.defaultInstallDir == config1.defaultInstallDir);
        assert(config2.compressionAlgorithm == config1.compressionAlgorithm);
        assert(config2.folderTargets.size() == config1.folderTargets.size());
        assert(config2.folderTargets[0].folderName == config1.folderTargets[0].folderName);
        assert(config2.folderTargets[0].targetDirectory == config1.folderTargets[0].targetDirectory);
        assert(config2.folderTargets[0].dirType == config1.folderTargets[0].dirType);
        
        // 修改复制的配置，验证不影响原始配置
        config2.applicationName = "ModifiedApp";
        assert(config1.applicationName == "OriginalApp");
        assert(config2.applicationName == "ModifiedApp");
        
        std::cout << "✓ PackagerConfiguration copy test passed" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ PackagerConfiguration copy test failed: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "Running Configuration Structures Tests" << std::endl;
    std::cout << "=======================================" << std::endl;
    
    bool allTestsPassed = true;
    
    if (!testSpecialDirectoryTypeEnum()) {
        allTestsPassed = false;
    }
    
    if (!testFolderTargetConfigDefaults()) {
        allTestsPassed = false;
    }
    
    if (!testFolderTargetConfigInitialization()) {
        allTestsPassed = false;
    }
    
    if (!testPackagerConfigurationDefaults()) {
        allTestsPassed = false;
    }
    
    if (!testPackagerConfigurationInitialization()) {
        allTestsPassed = false;
    }
    
    if (!testPackagerConfigurationCopy()) {
        allTestsPassed = false;
    }
    
    std::cout << "=======================================" << std::endl;
    if (allTestsPassed) {
        std::cout << "All configuration structures tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some configuration structures tests failed!" << std::endl;
        return 1;
    }
}
