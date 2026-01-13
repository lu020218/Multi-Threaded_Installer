// Unit tests for MetadataGenerator
#include <iostream>
#include <cassert>
#include "packager/metadata_generator.h"
#include "common/types.h"

using namespace MultiThreadedInstaller;

// Helper function to create test compression results
std::vector<CompressionResult> createTestResults() {
    std::vector<CompressionResult> results;
    
    CompressionResult result1;
    result1.compressedSize = 1000;
    result1.originalSize = 2000;
    result1.checksum = 0x12345678;
    result1.algorithm = CompressionAlgorithm::ZSTD_FAST;
    results.push_back(result1);
    
    CompressionResult result2;
    result2.compressedSize = 1500;
    result2.originalSize = 3000;
    result2.checksum = 0x87654321;
    result2.algorithm = CompressionAlgorithm::LZMA_HIGH;
    results.push_back(result2);
    
    return results;
}

// Helper function to create test folder infos
std::vector<FolderInfo> createTestFolderInfos() {
    std::vector<FolderInfo> folders;
    
    FolderInfo folder1;
    folder1.sourcePath = "source1";
    folder1.targetPath = "target1";
    folder1.totalSize = 2000;
    folders.push_back(folder1);
    
    FolderInfo folder2;
    folder2.sourcePath = "source2";
    folder2.targetPath = "target2";
    folder2.totalSize = 3000;
    folders.push_back(folder2);
    
    return folders;
}

// Helper function to create test configuration
PackagerConfiguration createTestConfig() {
    PackagerConfiguration config;
    config.applicationName = "TestApp";
    config.defaultInstallDir = "%ProgramFiles%";
    config.compressionAlgorithm = CompressionAlgorithm::ZSTD_FAST;
    
    FolderTargetConfig target1;
    target1.folderName = "target1";
    target1.targetDirectory = "installDirectory";
    target1.dirType = SpecialDirectoryType::INSTALL_DIRECTORY;
    config.folderTargets.push_back(target1);
    
    FolderTargetConfig target2;
    target2.folderName = "target2";
    target2.targetDirectory = "%AppData%\\Roaming";
    target2.dirType = SpecialDirectoryType::APPDATA_ROAMING;
    config.folderTargets.push_back(target2);
    
    return config;
}

// Test basic metadata generation
bool testGenerateBasicMetadata() {
    std::cout << "Testing basic metadata generation..." << std::endl;
    
    try {
        MetadataGenerator generator;
        auto results = createTestResults();
        auto folders = createTestFolderInfos();
        
        InstallationMetadata metadata = generator.generateMetadata(results, folders);
        
        assert(metadata.version == Constants::VERSION);
        assert(metadata.folderCount == 2);
        assert(metadata.folderMappings.size() == 2);
        assert(metadata.totalCompressedSize == 2500); // 1000 + 1500
        
        // Check first mapping
        assert(metadata.folderMappings[0].folderName == "target1");
        assert(metadata.folderMappings[0].offset == 0);
        assert(metadata.folderMappings[0].compressedSize == 1000);
        assert(metadata.folderMappings[0].originalSize == 2000);
        assert(metadata.folderMappings[0].checksum == 0x12345678);
        
        // Check second mapping
        assert(metadata.folderMappings[1].folderName == "target2");
        assert(metadata.folderMappings[1].offset == 1000);
        assert(metadata.folderMappings[1].compressedSize == 1500);
        assert(metadata.folderMappings[1].originalSize == 3000);
        assert(metadata.folderMappings[1].checksum == 0x87654321);
        
        std::cout << "  PASSED" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  FAILED: " << e.what() << std::endl;
        return false;
    }
}

// Test extended metadata generation
bool testGenerateExtendedMetadata() {
    std::cout << "Testing extended metadata generation..." << std::endl;
    
    try {
        MetadataGenerator generator;
        auto results = createTestResults();
        auto folders = createTestFolderInfos();
        auto config = createTestConfig();
        
        ExtendedInstallationMetadata metadata = generator.generateExtendedMetadata(results, folders, config);
        
        // Check basic fields
        assert(metadata.version == Constants::VERSION);
        assert(metadata.folderCount == 2);
        assert(metadata.totalCompressedSize == 2500);
        
        // Check configuration fields
        assert(metadata.applicationName == "TestApp");
        assert(metadata.defaultInstallDir == "%ProgramFiles%");
        
        // Check extended mappings
        assert(metadata.extendedMappings.size() == 2);
        
        // Check first extended mapping
        assert(metadata.extendedMappings[0].folderName == "target1");
        assert(metadata.extendedMappings[0].targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY);
        assert(metadata.extendedMappings[0].customTargetPath == "installDirectory");
        
        // Check second extended mapping
        assert(metadata.extendedMappings[1].folderName == "target2");
        assert(metadata.extendedMappings[1].targetDirType == SpecialDirectoryType::APPDATA_ROAMING);
        assert(metadata.extendedMappings[1].customTargetPath == "%AppData%\\Roaming");
        
        // Check backward compatibility - base class mappings should also be filled
        assert(metadata.folderMappings.size() == 2);
        
        std::cout << "  PASSED" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  FAILED: " << e.what() << std::endl;
        return false;
    }
}

// Test basic metadata serialization
bool testSerializeBasicMetadata() {
    std::cout << "Testing basic metadata serialization..." << std::endl;
    
    try {
        MetadataGenerator generator;
        auto results = createTestResults();
        auto folders = createTestFolderInfos();
        
        InstallationMetadata metadata = generator.generateMetadata(results, folders);
        std::vector<uint8_t> serialized = generator.serializeMetadata(metadata);
        
        // Check that serialization produces non-empty data
        assert(serialized.size() > sizeof(BinaryMetadata));
        
        // Check magic number
        BinaryMetadata* header = reinterpret_cast<BinaryMetadata*>(serialized.data());
        assert(header->magic == Constants::MAGIC_NUMBER);
        assert(header->version == Constants::VERSION);
        assert(header->folderCount == 2);
        
        std::cout << "  PASSED" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  FAILED: " << e.what() << std::endl;
        return false;
    }
}

// Test extended metadata serialization
bool testSerializeExtendedMetadata() {
    std::cout << "Testing extended metadata serialization..." << std::endl;
    
    try {
        MetadataGenerator generator;
        auto results = createTestResults();
        auto folders = createTestFolderInfos();
        auto config = createTestConfig();
        
        ExtendedInstallationMetadata metadata = generator.generateExtendedMetadata(results, folders, config);
        std::vector<uint8_t> serialized = generator.serializeExtendedMetadata(metadata);
        
        // Check that serialization produces non-empty data
        assert(serialized.size() > sizeof(BinaryMetadata));
        
        // Check magic number and version
        BinaryMetadata* header = reinterpret_cast<BinaryMetadata*>(serialized.data());
        assert(header->magic == Constants::MAGIC_NUMBER);
        assert(header->version == 2); // Extended version
        assert(header->folderCount == 2);
        
        std::cout << "  PASSED" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  FAILED: " << e.what() << std::endl;
        return false;
    }
}

// Test backward compatibility - old format should still work
bool testBackwardCompatibility() {
    std::cout << "Testing backward compatibility..." << std::endl;
    
    try {
        MetadataGenerator generator;
        auto results = createTestResults();
        auto folders = createTestFolderInfos();
        
        // Generate and serialize old format
        InstallationMetadata oldMetadata = generator.generateMetadata(results, folders);
        std::vector<uint8_t> oldSerialized = generator.serializeMetadata(oldMetadata);
        
        // Check that old format uses version 1
        BinaryMetadata* oldHeader = reinterpret_cast<BinaryMetadata*>(oldSerialized.data());
        assert(oldHeader->version == 1);
        
        // Generate and serialize new format
        auto config = createTestConfig();
        ExtendedInstallationMetadata newMetadata = generator.generateExtendedMetadata(results, folders, config);
        std::vector<uint8_t> newSerialized = generator.serializeExtendedMetadata(newMetadata);
        
        // Check that new format uses version 2
        BinaryMetadata* newHeader = reinterpret_cast<BinaryMetadata*>(newSerialized.data());
        assert(newHeader->version == 2);
        
        // Both should have the same magic number
        assert(oldHeader->magic == newHeader->magic);
        
        std::cout << "  PASSED" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  FAILED: " << e.what() << std::endl;
        return false;
    }
}

// Test empty results
bool testEmptyResults() {
    std::cout << "Testing empty results..." << std::endl;
    
    try {
        MetadataGenerator generator;
        std::vector<CompressionResult> results;
        std::vector<FolderInfo> folders;
        
        InstallationMetadata metadata = generator.generateMetadata(results, folders);
        
        assert(metadata.folderCount == 0);
        assert(metadata.folderMappings.size() == 0);
        assert(metadata.totalCompressedSize == 0);
        
        std::cout << "  PASSED" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  FAILED: " << e.what() << std::endl;
        return false;
    }
}

// Test extended metadata with default folder targets
bool testExtendedMetadataWithDefaultTargets() {
    std::cout << "Testing extended metadata with default targets..." << std::endl;
    
    try {
        MetadataGenerator generator;
        auto results = createTestResults();
        auto folders = createTestFolderInfos();
        
        // Create config without folder targets
        PackagerConfiguration config;
        config.applicationName = "TestApp";
        config.defaultInstallDir = "%ProgramFiles%";
        
        ExtendedInstallationMetadata metadata = generator.generateExtendedMetadata(results, folders, config);
        
        // All folders should default to INSTALL_DIRECTORY
        assert(metadata.extendedMappings.size() == 2);
        assert(metadata.extendedMappings[0].targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY);
        assert(metadata.extendedMappings[1].targetDirType == SpecialDirectoryType::INSTALL_DIRECTORY);
        
        std::cout << "  PASSED" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "  FAILED: " << e.what() << std::endl;
        return false;
    }
}

// Main test runner
int main() {
    std::cout << "Running MetadataGenerator unit tests..." << std::endl;
    std::cout << "========================================" << std::endl;
    
    int passed = 0;
    int total = 0;
    
    total++; if (testGenerateBasicMetadata()) passed++;
    total++; if (testGenerateExtendedMetadata()) passed++;
    total++; if (testSerializeBasicMetadata()) passed++;
    total++; if (testSerializeExtendedMetadata()) passed++;
    total++; if (testBackwardCompatibility()) passed++;
    total++; if (testEmptyResults()) passed++;
    total++; if (testExtendedMetadataWithDefaultTargets()) passed++;
    
    std::cout << "========================================" << std::endl;
    std::cout << "Tests passed: " << passed << "/" << total << std::endl;
    
    return (passed == total) ? 0 : 1;
}