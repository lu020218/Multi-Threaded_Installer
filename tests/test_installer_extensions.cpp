#include "installer/metadata_parser.h"
#include "installer/path_resolver.h"
#include "common/types.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>

using namespace MultiThreadedInstaller;

// 辅助函数：创建扩展元数据的二进制表示
std::vector<uint8_t> createExtendedMetadataBinary(
    const std::string& appName,
    const std::string& defaultInstallDir,
    const std::vector<ExtendedFolderMapping>& mappings) {
    
    std::vector<uint8_t> data;
    
    // 写入头部
    BinaryMetadata header;
    header.magic = Constants::MAGIC_NUMBER;
    header.version = Constants::VERSION;
    header.folderCount = static_cast<uint32_t>(mappings.size());
    header.metadataSize = 0; // 稍后计算
    header.dataOffset = 0;
    
    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), headerBytes, headerBytes + sizeof(BinaryMetadata));
    
    // 写入扩展字段标记
    uint32_t extendedMarker = 0x45585444; // "EXTD"
    const uint8_t* markerBytes = reinterpret_cast<const uint8_t*>(&extendedMarker);
    data.insert(data.end(), markerBytes, markerBytes + sizeof(uint32_t));
    
    // 写入 applicationName
    uint32_t appNameLen = static_cast<uint32_t>(appName.size());
    const uint8_t* appNameLenBytes = reinterpret_cast<const uint8_t*>(&appNameLen);
    data.insert(data.end(), appNameLenBytes, appNameLenBytes + sizeof(uint32_t));
    data.insert(data.end(), appName.begin(), appName.end());
    
    // 写入 defaultInstallDir
    uint32_t installDirLen = static_cast<uint32_t>(defaultInstallDir.size());
    const uint8_t* installDirLenBytes = reinterpret_cast<const uint8_t*>(&installDirLen);
    data.insert(data.end(), installDirLenBytes, installDirLenBytes + sizeof(uint32_t));
    data.insert(data.end(), defaultInstallDir.begin(), defaultInstallDir.end());
    
    // 写入文件夹映射
    for (const auto& mapping : mappings) {
        // 写入数值字段
        const uint8_t* offsetBytes = reinterpret_cast<const uint8_t*>(&mapping.offset);
        data.insert(data.end(), offsetBytes, offsetBytes + sizeof(uint64_t));
        
        const uint8_t* compSizeBytes = reinterpret_cast<const uint8_t*>(&mapping.compressedSize);
        data.insert(data.end(), compSizeBytes, compSizeBytes + sizeof(uint64_t));
        
        const uint8_t* origSizeBytes = reinterpret_cast<const uint8_t*>(&mapping.originalSize);
        data.insert(data.end(), origSizeBytes, origSizeBytes + sizeof(uint64_t));
        
        const uint8_t* checksumBytes = reinterpret_cast<const uint8_t*>(&mapping.checksum);
        data.insert(data.end(), checksumBytes, checksumBytes + sizeof(uint32_t));
        
        const uint8_t* algoBytes = reinterpret_cast<const uint8_t*>(&mapping.algorithm);
        data.insert(data.end(), algoBytes, algoBytes + sizeof(CompressionAlgorithm));
        
        // 写入文件夹名称
        uint32_t folderNameLen = static_cast<uint32_t>(mapping.folderName.size());
        const uint8_t* folderNameLenBytes = reinterpret_cast<const uint8_t*>(&folderNameLen);
        data.insert(data.end(), folderNameLenBytes, folderNameLenBytes + sizeof(uint32_t));
        data.insert(data.end(), mapping.folderName.begin(), mapping.folderName.end());
        
        // 写入目标路径
        uint32_t targetPathLen = static_cast<uint32_t>(mapping.targetPath.size());
        const uint8_t* targetPathLenBytes = reinterpret_cast<const uint8_t*>(&targetPathLen);
        data.insert(data.end(), targetPathLenBytes, targetPathLenBytes + sizeof(uint32_t));
        data.insert(data.end(), mapping.targetPath.begin(), mapping.targetPath.end());
        
        // 写入扩展字段
        const uint8_t* dirTypeBytes = reinterpret_cast<const uint8_t*>(&mapping.targetDirType);
        data.insert(data.end(), dirTypeBytes, dirTypeBytes + sizeof(SpecialDirectoryType));
        
        uint32_t customPathLen = static_cast<uint32_t>(mapping.customTargetPath.size());
        const uint8_t* customPathLenBytes = reinterpret_cast<const uint8_t*>(&customPathLen);
        data.insert(data.end(), customPathLenBytes, customPathLenBytes + sizeof(uint32_t));
        data.insert(data.end(), mapping.customTargetPath.begin(), mapping.customTargetPath.end());
    }
    
    return data;
}

// 测试扩展元数据解析
void testExtendedMetadataParsing() {
    std::cout << "Testing extended metadata parsing..." << std::endl;
    
    // 创建测试数据
    std::string appName = "TestApplication";
    std::string defaultInstallDir = "%ProgramFiles%";
    
    std::vector<ExtendedFolderMapping> mappings;
    
    ExtendedFolderMapping mapping1;
    mapping1.folderName = "app";
    mapping1.targetPath = "";
    mapping1.offset = 0;
    mapping1.compressedSize = 1000;
    mapping1.originalSize = 2000;
    mapping1.checksum = 12345;
    mapping1.algorithm = CompressionAlgorithm::ZSTD_FAST;
    mapping1.targetDirType = SpecialDirectoryType::INSTALL_DIRECTORY;
    mapping1.customTargetPath = "";
    mappings.push_back(mapping1);
    
    ExtendedFolderMapping mapping2;
    mapping2.folderName = "plugin";
    mapping2.targetPath = "%AppData%\\Roaming";
    mapping2.offset = 1000;
    mapping2.compressedSize = 500;
    mapping2.originalSize = 1000;
    mapping2.checksum = 67890;
    mapping2.algorithm = CompressionAlgorithm::LZMA_HIGH;
    mapping2.targetDirType = SpecialDirectoryType::APPDATA_ROAMING;
    mapping2.customTargetPath = "%AppData%\\Roaming";
    mappings.push_back(mapping2);
    
    // 创建二进制数据
    std::vector<uint8_t> binaryData = createExtendedMetadataBinary(appName, defaultInstallDir, mappings);
    
    // 解析元数据
    MetadataParser parser;
    ExtendedInstallationMetadata metadata = parser.deserializeExtendedMetadata(binaryData);
    
    // 验证结果
    assert(metadata.applicationName == appName);
    assert(metadata.defaultInstallDir == defaultInstallDir);
    assert(metadata.folderCount == 2);
    assert(metadata.extendedMappings.size() == 2);
    
    // 验证第一个映射
    assert(metadata.extendedMappings[0].folderName == "app");
    assert(metadata.extendedMappings[0].targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY);
    assert(metadata.extendedMappings[0].compressedSize == 1000);
    assert(metadata.extendedMappings[0].originalSize == 2000);
    
    // 验证第二个映射
    assert(metadata.extendedMappings[1].folderName == "plugin");
    assert(metadata.extendedMappings[1].targetDirType == SpecialDirectoryType::APPDATA_ROAMING);
    assert(metadata.extendedMappings[1].customTargetPath == "%AppData%\\Roaming");
    
    std::cout << "✓ Extended metadata parsing test passed" << std::endl;
}

// 测试向后兼容性
void testBackwardCompatibility() {
    std::cout << "Testing backward compatibility..." << std::endl;
    
    // 创建不包含扩展字段的元数据
    std::vector<uint8_t> data;
    
    // 写入头部
    BinaryMetadata header;
    header.magic = Constants::MAGIC_NUMBER;
    header.version = Constants::VERSION;
    header.folderCount = 1;
    header.metadataSize = 0;
    header.dataOffset = 0;
    
    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), headerBytes, headerBytes + sizeof(BinaryMetadata));
    
    // 写入一个基本的文件夹映射（不包含扩展字段标记）
    FolderMapping mapping;
    mapping.folderName = "test";
    mapping.targetPath = "C:\\Test";
    mapping.offset = 0;
    mapping.compressedSize = 100;
    mapping.originalSize = 200;
    mapping.checksum = 123;
    mapping.algorithm = CompressionAlgorithm::ZSTD_FAST;
    
    // 写入数值字段
    const uint8_t* offsetBytes = reinterpret_cast<const uint8_t*>(&mapping.offset);
    data.insert(data.end(), offsetBytes, offsetBytes + sizeof(uint64_t));
    
    const uint8_t* compSizeBytes = reinterpret_cast<const uint8_t*>(&mapping.compressedSize);
    data.insert(data.end(), compSizeBytes, compSizeBytes + sizeof(uint64_t));
    
    const uint8_t* origSizeBytes = reinterpret_cast<const uint8_t*>(&mapping.originalSize);
    data.insert(data.end(), origSizeBytes, origSizeBytes + sizeof(uint64_t));
    
    const uint8_t* checksumBytes = reinterpret_cast<const uint8_t*>(&mapping.checksum);
    data.insert(data.end(), checksumBytes, checksumBytes + sizeof(uint32_t));
    
    const uint8_t* algoBytes = reinterpret_cast<const uint8_t*>(&mapping.algorithm);
    data.insert(data.end(), algoBytes, algoBytes + sizeof(CompressionAlgorithm));
    
    // 写入文件夹名称
    uint32_t folderNameLen = static_cast<uint32_t>(mapping.folderName.size());
    const uint8_t* folderNameLenBytes = reinterpret_cast<const uint8_t*>(&folderNameLen);
    data.insert(data.end(), folderNameLenBytes, folderNameLenBytes + sizeof(uint32_t));
    data.insert(data.end(), mapping.folderName.begin(), mapping.folderName.end());
    
    // 写入目标路径
    uint32_t targetPathLen = static_cast<uint32_t>(mapping.targetPath.size());
    const uint8_t* targetPathLenBytes = reinterpret_cast<const uint8_t*>(&targetPathLen);
    data.insert(data.end(), targetPathLenBytes, targetPathLenBytes + sizeof(uint32_t));
    data.insert(data.end(), mapping.targetPath.begin(), mapping.targetPath.end());
    
    // 解析元数据
    MetadataParser parser;
    ExtendedInstallationMetadata metadata = parser.deserializeExtendedMetadata(data);
    
    // 验证结果 - 应该使用默认值
    assert(metadata.applicationName == "MyApplication"); // 默认值
    assert(metadata.defaultInstallDir == "%ProgramFiles%"); // 默认值
    assert(metadata.folderCount == 1);
    assert(metadata.folderMappings.size() == 1);
    assert(metadata.folderMappings[0].folderName == "test");
    
    std::cout << "✓ Backward compatibility test passed" << std::endl;
}

// 测试路径解析和文件安装
void testPathResolutionAndInstallation() {
    std::cout << "Testing path resolution and installation..." << std::endl;
    
    InstallerPathResolver resolver;
    std::string appName = "MyApp";
    
    // 测试场景1：用户路径不包含应用程序名
    {
        std::string userPath = "C:\\Program Files";
        std::string resolved = resolver.resolveFinalPath(
            userPath,
            SpecialDirectoryType::INSTALL_DIRECTORY,
            appName
        );
        assert(resolved == "C:\\Program Files\\MyApp");
        std::cout << "  ✓ Scenario 1: Path without app name - " << resolved << std::endl;
    }
    
    // 测试场景2：用户路径已包含应用程序名
    {
        std::string userPath = "C:\\Program Files\\MyApp";
        std::string resolved = resolver.resolveFinalPath(
            userPath,
            SpecialDirectoryType::INSTALL_DIRECTORY,
            appName
        );
        assert(resolved == "C:\\Program Files\\MyApp");
        std::cout << "  ✓ Scenario 2: Path with app name - " << resolved << std::endl;
    }
    
    // 测试场景3：环境变量路径
    {
        std::string envPath = "%AppData%\\Roaming";
        std::string resolved = resolver.resolveFinalPath(
            envPath,
            SpecialDirectoryType::APPDATA_ROAMING,
            appName
        );
        // 应该展开环境变量并添加应用程序名
        assert(resolved.find("AppData") != std::string::npos);
        assert(resolved.find("MyApp") != std::string::npos);
        std::cout << "  ✓ Scenario 3: Environment variable path - " << resolved << std::endl;
    }
    
    std::cout << "✓ Path resolution and installation test passed" << std::endl;
}

// 测试用户输入处理
void testUserInputHandling() {
    std::cout << "Testing user input handling..." << std::endl;
    
    InstallerPathResolver resolver;
    
    // 测试环境变量展开
    {
        std::string path = "%ProgramFiles%";
        std::string expanded = resolver.expandEnvironmentVariables(path);
        assert(!expanded.empty());
        assert(expanded != path); // 应该被展开
        std::cout << "  ✓ Environment variable expansion: " << path << " -> " << expanded << std::endl;
    }
    
    // 测试应用程序名检测
    {
        std::string path1 = "C:\\Program Files\\MyApp";
        std::string appName = "MyApp";
        std::string result1 = resolver.appendAppNameIfNeeded(path1, appName);
        assert(result1 == path1); // 不应该重复添加
        
        std::string path2 = "C:\\Program Files";
        std::string result2 = resolver.appendAppNameIfNeeded(path2, appName);
        assert(result2 == "C:\\Program Files\\MyApp"); // 应该添加
        
        std::cout << "  ✓ Application name detection and appending" << std::endl;
    }
    
    std::cout << "✓ User input handling test passed" << std::endl;
}

int main() {
    std::cout << "Running Installer Extensions Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        testExtendedMetadataParsing();
        testBackwardCompatibility();
        testPathResolutionAndInstallation();
        testUserInputHandling();
        
        std::cout << std::endl;
        std::cout << "All installer extensions tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}
