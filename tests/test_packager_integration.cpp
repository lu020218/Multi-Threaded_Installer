#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include "packager/configuration_manager.h"
#include "packager/folder_scanner.h"
#include "packager/compression_module.h"
#include "packager/metadata_generator.h"
#include "common/types.h"

using namespace MultiThreadedInstaller;
namespace fs = std::filesystem;

// Test helper class
class PackagerIntegrationTestHelper {
public:
    PackagerIntegrationTestHelper() {
        // Create temporary test directory
        testDir = fs::temp_directory_path() / "packager_integration_test";
        fs::create_directories(testDir);
        
        // Create test input directory
        inputDir = testDir / "input";
        fs::create_directories(inputDir);
        
        // Create test folders
        fs::create_directories(inputDir / "app");
        fs::create_directories(inputDir / "data");
        
        // Create test files
        createTestFile(inputDir / "app" / "test.txt", "test content");
        createTestFile(inputDir / "data" / "config.ini", "config content");
    }
    
    ~PackagerIntegrationTestHelper() {
        // Clean up test directory
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
    }
    
    void createTestFile(const fs::path& path, const std::string& content) {
        std::ofstream file(path);
        file << content;
        file.close();
    }
    
    void createConfigFile(const std::string& content) {
        std::ofstream file(inputDir / "packager.json");
        file << content;
        file.close();
    }
    
    fs::path testDir;
    fs::path inputDir;
};

// Test: Configuration loading and application
void testLoadAndApplyConfiguration() {
    std::cout << "Test: Load and apply configuration..." << std::endl;
    
    PackagerIntegrationTestHelper helper;
    
    // Create a valid configuration file
    helper.createConfigFile(R"({
        "applicationName": "TestApp",
        "defaultInstallDirectory": "%ProgramFiles%",
        "compressionAlgorithm": "zstd",
        "folderTargets": [
            {
                "folder": "app",
                "targetDirectory": "installDirectory"
            },
            {
                "folder": "data",
                "targetDirectory": "%AppData%\\Roaming"
            }
        ]
    })");
    
    // Initialize configuration manager
    ConfigurationManager configManager;
    assert(configManager.initialize(helper.inputDir.string()));
    assert(configManager.hasConfigFile());
    
    const auto& config = configManager.getConfiguration();
    assert(config.applicationName == "TestApp");
    assert(config.defaultInstallDir == "%ProgramFiles%");
    assert(config.compressionAlgorithm == CompressionAlgorithm::ZSTD_FAST);
    assert(config.folderTargets.size() == 2);
    
    // Scan folders
    FolderScanner scanner;
    auto folders = scanner.scanInputDirectory(helper.inputDir.string());
    assert(folders.size() == 2);
    
    // Apply folder targets
    configManager.applyFolderTargets(folders);
    
    // Verify folder targets were applied
    bool foundApp = false;
    bool foundData = false;
    
    for (const auto& folder : folders) {
        if (folder.sourcePath.find("app") != std::string::npos) {
            assert(folder.targetPath == "installDirectory");
            foundApp = true;
        } else if (folder.sourcePath.find("data") != std::string::npos) {
            assert(folder.targetPath == "%AppData%\\Roaming");
            foundData = true;
        }
    }
    
    assert(foundApp);
    assert(foundData);
    
    std::cout << "  PASSED" << std::endl;
}

// Test: Default configuration when no config file exists
void testUseDefaultConfigurationWhenNoFile() {
    std::cout << "Test: Use default configuration when no file..." << std::endl;
    
    PackagerIntegrationTestHelper helper;
    // Don't create a config file
    
    ConfigurationManager configManager;
    assert(configManager.initialize(helper.inputDir.string()));
    assert(!configManager.hasConfigFile());
    
    const auto& config = configManager.getConfiguration();
    assert(config.applicationName == "MyApplication");
    assert(config.compressionAlgorithm == CompressionAlgorithm::ZSTD_FAST);
    
    std::cout << "  PASSED" << std::endl;
}

// Test: Configuration with LZMA compression
void testConfigurationWithLZMA() {
    std::cout << "Test: Configuration with LZMA..." << std::endl;
    
    PackagerIntegrationTestHelper helper;
    helper.createConfigFile(R"({
        "applicationName": "LZMAApp",
        "compressionAlgorithm": "lzma"
    })");
    
    ConfigurationManager configManager;
    assert(configManager.initialize(helper.inputDir.string()));
    
    const auto& config = configManager.getConfiguration();
    assert(config.compressionAlgorithm == CompressionAlgorithm::LZMA_HIGH);
    
    std::cout << "  PASSED" << std::endl;
}

// Test: Metadata generation with configuration
void testMetadataGenerationWithConfiguration() {
    std::cout << "Test: Metadata generation with configuration..." << std::endl;
    
    PackagerIntegrationTestHelper helper;
    helper.createConfigFile(R"({
        "applicationName": "MetadataTestApp",
        "defaultInstallDirectory": "%ProgramFiles%",
        "compressionAlgorithm": "zstd",
        "folderTargets": [
            {
                "folder": "app",
                "targetDirectory": "installDirectory"
            }
        ]
    })");
    
    ConfigurationManager configManager;
    assert(configManager.initialize(helper.inputDir.string()));
    
    const auto& config = configManager.getConfiguration();
    
    // Scan and compress folders
    FolderScanner scanner;
    auto folders = scanner.scanInputDirectory(helper.inputDir.string());
    configManager.applyFolderTargets(folders);
    
    CompressionModule compressor;
    compressor.setCompressionAlgorithm(config.compressionAlgorithm);
    
    std::vector<CompressionResult> compressionResults;
    for (const auto& folder : folders) {
        auto result = compressor.compressFolder(folder);
        assert(!result.compressedData.empty());
        compressionResults.push_back(result);
    }
    
    // Generate metadata (without configuration for now - will be updated in task 7)
    MetadataGenerator metadataGen;
    auto metadata = metadataGen.generateMetadata(compressionResults, folders);
    
    // Basic verification
    assert(metadata.folderMappings.size() == folders.size());
    
    std::cout << "  PASSED" << std::endl;
}

// Test: Logging configuration information
void testLoggingConfigurationInformation() {
    std::cout << "Test: Logging configuration information..." << std::endl;
    
    PackagerIntegrationTestHelper helper;
    helper.createConfigFile(R"({
        "applicationName": "LogTestApp",
        "defaultInstallDirectory": "%ProgramFiles%",
        "compressionAlgorithm": "zstd"
    })");
    
    ConfigurationManager configManager;
    assert(configManager.initialize(helper.inputDir.string()));
    
    // Verify we can retrieve configuration file path
    assert(!configManager.getConfigFilePath().empty());
    assert(configManager.hasConfigFile());
    
    const auto& config = configManager.getConfiguration();
    assert(config.applicationName == "LogTestApp");
    
    std::cout << "  PASSED" << std::endl;
}

// Test: Invalid configuration file handling
void testInvalidConfigurationHandling() {
    std::cout << "Test: Invalid configuration handling..." << std::endl;
    
    PackagerIntegrationTestHelper helper;
    // Create invalid JSON
    helper.createConfigFile("{ invalid json }");
    
    ConfigurationManager configManager;
    assert(!configManager.initialize(helper.inputDir.string()));
    
    std::cout << "  PASSED" << std::endl;
}

// Test: Missing required fields
void testMissingRequiredFields() {
    std::cout << "Test: Missing required fields..." << std::endl;
    
    PackagerIntegrationTestHelper helper;
    // Create config without applicationName
    helper.createConfigFile(R"({
        "compressionAlgorithm": "zstd"
    })");
    
    ConfigurationManager configManager;
    assert(!configManager.initialize(helper.inputDir.string()));
    
    std::cout << "  PASSED" << std::endl;
}

// Test: Folder target for non-existent folder
void testNonExistentFolderTarget() {
    std::cout << "Test: Non-existent folder target..." << std::endl;
    
    PackagerIntegrationTestHelper helper;
    helper.createConfigFile(R"({
        "applicationName": "TestApp",
        "folderTargets": [
            {
                "folder": "nonexistent",
                "targetDirectory": "installDirectory"
            }
        ]
    })");
    
    ConfigurationManager configManager;
    assert(!configManager.initialize(helper.inputDir.string()));
    
    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "Running Packager Integration Tests..." << std::endl;
    std::cout << "======================================" << std::endl;
    
    try {
        testLoadAndApplyConfiguration();
        testUseDefaultConfigurationWhenNoFile();
        testConfigurationWithLZMA();
        testMetadataGenerationWithConfiguration();
        testLoggingConfigurationInformation();
        testInvalidConfigurationHandling();
        testMissingRequiredFields();
        testNonExistentFolderTarget();
        
        std::cout << "\nAll tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\nTest failed with unknown exception" << std::endl;
        return 1;
    }
}
