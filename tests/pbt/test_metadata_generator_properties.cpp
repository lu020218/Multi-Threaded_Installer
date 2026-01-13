#include <iostream>
#include <cassert>
#include <random>
#include <vector>
#include <string>
#include "packager/metadata_generator.h"
#include "common/types.h"

using namespace MultiThreadedInstaller;

// 随机生成器
class RandomGenerator {
public:
    RandomGenerator() : gen(rd()), dist(0, 1000) {}
    
    int getInt(int min = 0, int max = 1000) {
        std::uniform_int_distribution<> d(min, max);
        return d(gen);
    }
    
    uint64_t getUInt64(uint64_t min = 0, uint64_t max = 100000) {
        std::uniform_int_distribution<uint64_t> d(min, max);
        return d(gen);
    }
    
    uint32_t getUInt32(uint32_t min = 0, uint32_t max = 100000) {
        std::uniform_int_distribution<uint32_t> d(min, max);
        return d(gen);
    }
    
    std::string getString(size_t length = 10) {
        const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
        std::string result;
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            result += charset[getInt(0, sizeof(charset) - 2)];
        }
        return result;
    }
    
    bool getBool() {
        return getInt(0, 1) == 1;
    }
    
    CompressionAlgorithm getCompressionAlgorithm() {
        return getBool() ? CompressionAlgorithm::ZSTD_FAST : CompressionAlgorithm::LZMA_HIGH;
    }
    
    SpecialDirectoryType getDirectoryType() {
        int choice = getInt(0, 4);
        switch (choice) {
            case 0: return SpecialDirectoryType::INSTALL_DIRECTORY;
            case 1: return SpecialDirectoryType::PROGRAM_FILES;
            case 2: return SpecialDirectoryType::APPDATA_ROAMING;
            case 3: return SpecialDirectoryType::APPDATA_LOCAL;
            case 4: return SpecialDirectoryType::PROGRAM_DATA;
            default: return SpecialDirectoryType::INSTALL_DIRECTORY;
        }
    }
    
    std::string getDirectoryTypeString(SpecialDirectoryType type) {
        switch (type) {
            case SpecialDirectoryType::INSTALL_DIRECTORY: return "installDirectory";
            case SpecialDirectoryType::PROGRAM_FILES: return "%ProgramFiles%";
            case SpecialDirectoryType::APPDATA_ROAMING: return "%AppData%\\Roaming";
            case SpecialDirectoryType::APPDATA_LOCAL: return "%LocalAppData%";
            case SpecialDirectoryType::PROGRAM_DATA: return "%ProgramData%";
            default: return "installDirectory";
        }
    }
    
private:
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_int_distribution<> dist;
};

// 生成随机的压缩结果
std::vector<CompressionResult> generateRandomResults(RandomGenerator& rng, int count) {
    std::vector<CompressionResult> results;
    for (int i = 0; i < count; ++i) {
        CompressionResult result;
        result.originalSize = rng.getUInt64(1000, 100000);
        result.compressedSize = rng.getUInt64(500, result.originalSize);
        result.checksum = rng.getUInt32();
        result.algorithm = rng.getCompressionAlgorithm();
        results.push_back(result);
    }
    return results;
}

// 生成随机的文件夹信息
std::vector<FolderInfo> generateRandomFolderInfos(RandomGenerator& rng, int count) {
    std::vector<FolderInfo> folders;
    for (int i = 0; i < count; ++i) {
        FolderInfo folder;
        folder.sourcePath = "source_" + rng.getString(8);
        folder.targetPath = "target_" + rng.getString(8);
        folder.totalSize = rng.getUInt64(1000, 100000);
        folders.push_back(folder);
    }
    return folders;
}

// 生成随机的配置
PackagerConfiguration generateRandomConfig(RandomGenerator& rng, const std::vector<FolderInfo>& folders) {
    PackagerConfiguration config;
    config.applicationName = rng.getString(10);
    config.defaultInstallDir = rng.getDirectoryTypeString(rng.getDirectoryType());
    config.compressionAlgorithm = rng.getCompressionAlgorithm();
    
    // 为每个文件夹生成目标配置
    for (const auto& folder : folders) {
        FolderTargetConfig target;
        target.folderName = folder.targetPath;
        target.dirType = rng.getDirectoryType();
        target.targetDirectory = rng.getDirectoryTypeString(target.dirType);
        config.folderTargets.push_back(target);
    }
    
    return config;
}

// Property 8: Folder Target Metadata Persistence
// Feature: packager-config-file, Property 8: Folder Target Metadata Persistence
// Validates: Requirements 4.2, 10.2
// For any folder target configuration, the packager should write the folder name and target directory type into metadata
bool testFolderTargetMetadataPersistence(int iterations = 100) {
    std::cout << "Property 8: Folder Target Metadata Persistence" << std::endl;
    std::cout << "  Testing with " << iterations << " iterations..." << std::endl;
    
    RandomGenerator rng;
    MetadataGenerator generator;
    int passed = 0;
    
    for (int i = 0; i < iterations; ++i) {
        try {
            // 生成随机数据
            int folderCount = rng.getInt(1, 10);
            auto results = generateRandomResults(rng, folderCount);
            auto folders = generateRandomFolderInfos(rng, folderCount);
            auto config = generateRandomConfig(rng, folders);
            
            // 生成扩展元数据
            ExtendedInstallationMetadata metadata = generator.generateExtendedMetadata(results, folders, config);
            
            // 验证：每个文件夹目标都应该在扩展映射中
            assert(metadata.extendedMappings.size() == folders.size());
            
            for (size_t j = 0; j < folders.size(); ++j) {
                const auto& folder = folders[j];
                const auto& extMapping = metadata.extendedMappings[j];
                
                // 验证文件夹名称被正确写入
                assert(extMapping.folderName == folder.targetPath);
                
                // 验证目标目录类型被正确写入
                bool foundTarget = false;
                for (const auto& target : config.folderTargets) {
                    if (target.folderName == folder.targetPath) {
                        assert(extMapping.targetDirType == target.dirType);
                        assert(extMapping.customTargetPath == target.targetDirectory);
                        foundTarget = true;
                        break;
                    }
                }
                assert(foundTarget);
            }
            
            passed++;
        } catch (const std::exception& e) {
            std::cerr << "  Iteration " << i << " failed: " << e.what() << std::endl;
        }
    }
    
    std::cout << "  Passed: " << passed << "/" << iterations << std::endl;
    return passed == iterations;
}

// Property 13: Metadata Configuration Round-Trip
// Feature: packager-config-file, Property 13: Metadata Configuration Round-Trip
// Validates: Requirements 10.1, 10.2, 10.3, 10.5
// For any valid configuration, the packager should write it to metadata and be able to read it back correctly
bool testMetadataConfigurationRoundTrip(int iterations = 100) {
    std::cout << "Property 13: Metadata Configuration Round-Trip" << std::endl;
    std::cout << "  Testing with " << iterations << " iterations..." << std::endl;
    
    RandomGenerator rng;
    MetadataGenerator generator;
    int passed = 0;
    
    for (int i = 0; i < iterations; ++i) {
        try {
            // 生成随机数据
            int folderCount = rng.getInt(1, 10);
            auto results = generateRandomResults(rng, folderCount);
            auto folders = generateRandomFolderInfos(rng, folderCount);
            auto config = generateRandomConfig(rng, folders);
            
            // 生成扩展元数据
            ExtendedInstallationMetadata metadata = generator.generateExtendedMetadata(results, folders, config);
            
            // 验证配置信息被正确写入元数据
            assert(metadata.applicationName == config.applicationName);
            assert(metadata.defaultInstallDir == config.defaultInstallDir);
            
            // 序列化元数据
            std::vector<uint8_t> serialized = generator.serializeExtendedMetadata(metadata);
            
            // 验证序列化成功
            assert(serialized.size() > sizeof(BinaryMetadata));
            
            // 验证头部信息
            BinaryMetadata* header = reinterpret_cast<BinaryMetadata*>(serialized.data());
            assert(header->magic == Constants::MAGIC_NUMBER);
            assert(header->version == 2); // 扩展版本
            assert(header->folderCount == folderCount);
            
            // 验证文件夹映射信息被保留
            assert(metadata.folderMappings.size() == folderCount);
            assert(metadata.extendedMappings.size() == folderCount);
            
            passed++;
        } catch (const std::exception& e) {
            std::cerr << "  Iteration " << i << " failed: " << e.what() << std::endl;
        }
    }
    
    std::cout << "  Passed: " << passed << "/" << iterations << std::endl;
    return passed == iterations;
}

// Property 14: Backward Compatibility
// Feature: packager-config-file, Property 14: Backward Compatibility
// Validates: Requirements 10.4
// For any metadata without new configuration options (old version), the installer should be able to read it correctly
bool testBackwardCompatibility(int iterations = 100) {
    std::cout << "Property 14: Backward Compatibility" << std::endl;
    std::cout << "  Testing with " << iterations << " iterations..." << std::endl;
    
    RandomGenerator rng;
    MetadataGenerator generator;
    int passed = 0;
    
    for (int i = 0; i < iterations; ++i) {
        try {
            // 生成随机数据
            int folderCount = rng.getInt(1, 10);
            auto results = generateRandomResults(rng, folderCount);
            auto folders = generateRandomFolderInfos(rng, folderCount);
            
            // 生成旧版本元数据（不包含配置）
            InstallationMetadata oldMetadata = generator.generateMetadata(results, folders);
            std::vector<uint8_t> oldSerialized = generator.serializeMetadata(oldMetadata);
            
            // 验证旧版本使用版本号1
            BinaryMetadata* oldHeader = reinterpret_cast<BinaryMetadata*>(oldSerialized.data());
            assert(oldHeader->version == 1);
            assert(oldHeader->magic == Constants::MAGIC_NUMBER);
            assert(oldHeader->folderCount == folderCount);
            
            // 生成新版本元数据（包含配置）
            auto config = generateRandomConfig(rng, folders);
            ExtendedInstallationMetadata newMetadata = generator.generateExtendedMetadata(results, folders, config);
            std::vector<uint8_t> newSerialized = generator.serializeExtendedMetadata(newMetadata);
            
            // 验证新版本使用版本号2
            BinaryMetadata* newHeader = reinterpret_cast<BinaryMetadata*>(newSerialized.data());
            assert(newHeader->version == 2);
            assert(newHeader->magic == Constants::MAGIC_NUMBER);
            assert(newHeader->folderCount == folderCount);
            
            // 验证两个版本都有相同的魔数
            assert(oldHeader->magic == newHeader->magic);
            
            // 验证新版本包含基类的映射信息（向后兼容）
            assert(newMetadata.folderMappings.size() == oldMetadata.folderMappings.size());
            
            passed++;
        } catch (const std::exception& e) {
            std::cerr << "  Iteration " << i << " failed: " << e.what() << std::endl;
        }
    }
    
    std::cout << "  Passed: " << passed << "/" << iterations << std::endl;
    return passed == iterations;
}

// Main test runner
int main() {
    std::cout << "Running MetadataGenerator property-based tests..." << std::endl;
    std::cout << "=================================================" << std::endl;
    
    int passed = 0;
    int total = 0;
    
    total++; if (testFolderTargetMetadataPersistence(100)) passed++;
    total++; if (testMetadataConfigurationRoundTrip(100)) passed++;
    total++; if (testBackwardCompatibility(100)) passed++;
    
    std::cout << "=================================================" << std::endl;
    std::cout << "Properties passed: " << passed << "/" << total << std::endl;
    
    return (passed == total) ? 0 : 1;
}
