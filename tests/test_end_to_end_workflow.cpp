#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include "common/types.h"
#include "packager/configuration_manager.h"
#include "packager/folder_scanner.h"
#include "packager/compression_module.h"
#include "packager/metadata_generator.h"
#include "packager/installer_generator.h"
#include "installer/metadata_parser.h"
#include "installer/path_resolver.h"

using namespace MultiThreadedInstaller;
namespace fs = std::filesystem;

// Test helper class for end-to-end workflow testing
class EndToEndTestHelper {
public:
    EndToEndTestHelper() {
        // Create temporary test directory with a simple name
        testDir = fs::path("E:/temp_e2e_test");
        if (fs::exists(testDir)) {
            fs::remove_all(testDir);
        }
        fs::create_directories(testDir);
        
        // Create test input directory
        inputDir = testDir / "input";
        fs::create_directories(inputDir);
        
        // Create test folders
        fs::create_directories(inputDir / "app");
        fs::create_directories(inputDir / "plugin");
        fs::create_directories(inputDir / "config");
        
        // Create test files
        createTestFile(inputDir / "app" / "main.exe", "main executable content");
        createTestFile(inputDir / "app" / "lib.dll", "library content");
        createTestFile(inputDir / "plugin" / "plugin1.dll", "plugin1 content");
        createTestFile(inputDir / "plugin" / "plugin2.dll", "plugin2 content");
        createTestFile(inputDir / "config" / "settings.ini", "config content");
        
        // Output installer path
        installerPath = testDir / "test_installer.exe";
        
        // Installation directory
        installDir = testDir / "install";
    }
    
    ~EndToEndTestHelper() {
        // Clean up test directory
        if (fs::exists(testDir)) {
            try {
                fs::remove_all(testDir);
            } catch (...) {
                // Ignore cleanup errors
            }
        }
    }
    
    void createTestFile(const fs::path& path, const std::string& content) {
        std::ofstream file(path, std::ios::binary);
        file << content;
        file.close();
    }
    
    void createConfigFile(const std::string& content) {
        std::ofstream file(inputDir / "packager.json");
        file << content;
        file.close();
    }
    
    bool runPackager() {
        try {
            // Initialize configuration manager
            ConfigurationManager configManager;
            if (!configManager.initialize(inputDir.string())) {
                std::cerr << "ERROR: Failed to initialize configuration manager" << std::endl;
                return false;
            }
            
            const auto& config = configManager.getConfiguration();
            std::cout << "  Configuration loaded: " << config.applicationName << std::endl;
            
            // Scan folders
            FolderScanner scanner;
            auto folders = scanner.scanInputDirectory(inputDir.string());
            std::cout << "  Found " << folders.size() << " folders" << std::endl;
            
            // Apply folder targets
            configManager.applyFolderTargets(folders);
            
            // Compress folders
            CompressionModule compressor;
            compressor.setCompressionAlgorithm(config.compressionAlgorithm);
            
            std::vector<CompressionResult> compressionResults;
            for (const auto& folder : folders) {
                auto result = compressor.compressFolder(folder);
                if (result.compressedData.empty()) {
                    std::cerr << "ERROR: Failed to compress folder: " << folder.sourcePath << std::endl;
                    return false;
                }
                compressionResults.push_back(result);
            }
            std::cout << "  Compressed " << compressionResults.size() << " folders" << std::endl;
            
            // Generate metadata
            MetadataGenerator metadataGen;
            auto extendedMetadata = metadataGen.generateExtendedMetadata(compressionResults, folders, config);
            auto serializedMetadata = metadataGen.serializeExtendedMetadata(extendedMetadata);
            std::cout << "  Generated metadata" << std::endl;
            
            // Convert compression results to vector of compressed data
            std::vector<std::vector<uint8_t>> compressedDataVec;
            for (const auto& result : compressionResults) {
                compressedDataVec.push_back(result.compressedData);
            }
            
            // Generate installer
            InstallerGenerator installerGen;
            if (!installerGen.generateInstaller(installerPath.string(), serializedMetadata, compressedDataVec)) {
                std::cerr << "ERROR: Failed to generate installer" << std::endl;
                return false;
            }
            std::cout << "  Generated installer: " << installerPath << std::endl;
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Exception during packaging: " << e.what() << std::endl;
            return false;
        }
    }
    
    bool fileExists(const fs::path& path) {
        return fs::exists(path) && fs::is_regular_file(path);
    }
    
    bool directoryExists(const fs::path& path) {
        return fs::exists(path) && fs::is_directory(path);
    }
    
    std::string readFile(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    
    fs::path testDir;
    fs::path inputDir;
    fs::path installerPath;
    fs::path installDir;
};

// Test 1: Complete packaging and installation workflow with default install directory
void testCompleteWorkflowDefaultInstallDir() {
    std::cout << "\nTest 1: Complete workflow with default install directory..." << std::endl;
    
    EndToEndTestHelper helper;
    
    // Create configuration file
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
                "folder": "plugin",
                "targetDirectory": "%AppData%\\Roaming"
            },
            {
                "folder": "config",
                "targetDirectory": "%ProgramData%"
            }
        ]
    })");
    
    // Run packager
    std::cout << "  Running packager..." << std::endl;
    assert(helper.runPackager());
    
    // Verify installer was created
    std::cout << "  Verifying installer was created..." << std::endl;
    assert(helper.fileExists(helper.installerPath));
    
    // Verify installer size is reasonable (should contain compressed data)
    auto installerSize = fs::file_size(helper.installerPath);
    std::cout << "  Installer size: " << installerSize << " bytes" << std::endl;
    assert(installerSize > 1000); // Should be at least 1KB
    
    std::cout << "  PASSED" << std::endl;
}

// Test 2: User modifies install directory to path without app name
void testUserModifiesPathWithoutAppName() {
    std::cout << "\nTest 2: User modifies install directory (without app name)..." << std::endl;
    
    EndToEndTestHelper helper;
    
    // Create configuration file
    helper.createConfigFile(R"({
        "applicationName": "MyApplication",
        "defaultInstallDirectory": "%ProgramFiles%",
        "compressionAlgorithm": "zstd",
        "folderTargets": [
            {
                "folder": "app",
                "targetDirectory": "installDirectory"
            }
        ]
    })");
    
    // Run packager
    std::cout << "  Running packager..." << std::endl;
    assert(helper.runPackager());
    
    // Verify installer was created
    assert(helper.fileExists(helper.installerPath));
    
    std::cout << "  PASSED" << std::endl;
}

// Test 3: User modifies install directory to path with app name already included
void testUserModifiesPathWithAppName() {
    std::cout << "\nTest 3: User modifies install directory (with app name)..." << std::endl;
    
    EndToEndTestHelper helper;
    
    // Create configuration file
    helper.createConfigFile(R"({
        "applicationName": "MyApplication",
        "defaultInstallDirectory": "%ProgramFiles%",
        "compressionAlgorithm": "zstd",
        "folderTargets": [
            {
                "folder": "app",
                "targetDirectory": "installDirectory"
            }
        ]
    })");
    
    // Run packager
    std::cout << "  Running packager..." << std::endl;
    assert(helper.runPackager());
    
    // Verify installer was created
    assert(helper.fileExists(helper.installerPath));
    
    std::cout << "  PASSED" << std::endl;
}

// Test 4: Multiple folder targets with different directory types
void testMultipleFolderTargets() {
    std::cout << "\nTest 4: Multiple folder targets with different directory types..." << std::endl;
    
    EndToEndTestHelper helper;
    
    // Create configuration file with multiple folder targets
    helper.createConfigFile(R"({
        "applicationName": "MultiTargetApp",
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
            },
            {
                "folder": "config",
                "targetDirectory": "%ProgramData%"
            }
        ]
    })");
    
    // Run packager
    std::cout << "  Running packager..." << std::endl;
    assert(helper.runPackager());
    
    // Verify installer was created
    assert(helper.fileExists(helper.installerPath));
    
    // Verify installer size is reasonable
    auto installerSize = fs::file_size(helper.installerPath);
    std::cout << "  Installer size: " << installerSize << " bytes" << std::endl;
    assert(installerSize > 1000);
    
    std::cout << "  PASSED" << std::endl;
}

// Test 5: Application name appending logic verification
void testApplicationNameAppendingLogic() {
    std::cout << "\nTest 5: Application name appending logic..." << std::endl;
    
    EndToEndTestHelper helper;
    
    // Create configuration file
    helper.createConfigFile(R"({
        "applicationName": "AppNameTest",
        "defaultInstallDirectory": "%ProgramFiles%",
        "compressionAlgorithm": "zstd",
        "folderTargets": [
            {
                "folder": "app",
                "targetDirectory": "installDirectory"
            }
        ]
    })");
    
    // Run packager
    std::cout << "  Running packager..." << std::endl;
    assert(helper.runPackager());
    
    // Verify installer was created
    assert(helper.fileExists(helper.installerPath));
    
    std::cout << "  PASSED" << std::endl;
}

// Test 6: LZMA compression algorithm
void testLZMACompression() {
    std::cout << "\nTest 6: LZMA compression algorithm..." << std::endl;
    
    EndToEndTestHelper helper;
    
    // Create configuration file with LZMA
    helper.createConfigFile(R"({
        "applicationName": "LZMATestApp",
        "defaultInstallDirectory": "%ProgramFiles%",
        "compressionAlgorithm": "lzma",
        "folderTargets": [
            {
                "folder": "app",
                "targetDirectory": "installDirectory"
            }
        ]
    })");
    
    // Run packager
    std::cout << "  Running packager..." << std::endl;
    assert(helper.runPackager());
    
    // Verify installer was created
    assert(helper.fileExists(helper.installerPath));
    
    std::cout << "  PASSED" << std::endl;
}

// Test 7: Environment variable paths
void testEnvironmentVariablePaths() {
    std::cout << "\nTest 7: Environment variable paths..." << std::endl;
    
    EndToEndTestHelper helper;
    
    // Create configuration file with various environment variables
    helper.createConfigFile(R"({
        "applicationName": "EnvVarApp",
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
            },
            {
                "folder": "config",
                "targetDirectory": "%ProgramData%"
            }
        ]
    })");
    
    // Run packager
    std::cout << "  Running packager..." << std::endl;
    assert(helper.runPackager());
    
    // Verify installer was created
    assert(helper.fileExists(helper.installerPath));
    
    std::cout << "  PASSED" << std::endl;
}

// Test 8: Default configuration (no config file)
void testDefaultConfiguration() {
    std::cout << "\nTest 8: Default configuration (no config file)..." << std::endl;
    
    EndToEndTestHelper helper;
    // Don't create a config file
    
    // Run packager
    std::cout << "  Running packager..." << std::endl;
    assert(helper.runPackager());
    
    // Verify installer was created
    assert(helper.fileExists(helper.installerPath));
    
    std::cout << "  PASSED" << std::endl;
}

// Test 9: Path resolution - user does not modify install directory
void testPathResolutionDefaultDirectory() {
    std::cout << "\nTest 9: Path resolution - user does not modify install directory..." << std::endl;
    
    InstallerPathResolver resolver;
    std::string appName = "TestApp";
    std::string defaultPath = "C:\\Program Files (x86)";
    
    // Scenario: User accepts default path (without app name)
    // Expected: Path should be appended with app name
    std::string resolvedPath = resolver.resolveFinalPath(
        defaultPath,
        SpecialDirectoryType::INSTALL_DIRECTORY,
        appName
    );
    
    std::cout << "  Input: " << defaultPath << std::endl;
    std::cout << "  Output: " << resolvedPath << std::endl;
    
    // Verify app name was appended
    assert(resolvedPath.find(appName) != std::string::npos);
    assert(resolvedPath == "C:\\Program Files (x86)\\TestApp");
    
    std::cout << "  PASSED" << std::endl;
}

// Test 10: Path resolution - user modifies to path without app name
void testPathResolutionModifiedWithoutAppName() {
    std::cout << "\nTest 10: Path resolution - user modifies to path without app name..." << std::endl;
    
    InstallerPathResolver resolver;
    std::string appName = "MyApplication";
    std::string userPath = "D:\\CustomPath";
    
    // Scenario: User modifies path to custom location without app name
    // Expected: Path should be appended with app name
    std::string resolvedPath = resolver.resolveFinalPath(
        userPath,
        SpecialDirectoryType::INSTALL_DIRECTORY,
        appName
    );
    
    std::cout << "  Input: " << userPath << std::endl;
    std::cout << "  Output: " << resolvedPath << std::endl;
    
    // Verify app name was appended
    assert(resolvedPath.find(appName) != std::string::npos);
    assert(resolvedPath == "D:\\CustomPath\\MyApplication");
    
    std::cout << "  PASSED" << std::endl;
}

// Test 11: Path resolution - user modifies to path with app name
void testPathResolutionModifiedWithAppName() {
    std::cout << "\nTest 11: Path resolution - user modifies to path with app name..." << std::endl;
    
    InstallerPathResolver resolver;
    std::string appName = "MyApplication";
    std::string userPath = "D:\\CustomPath\\MyApplication";
    
    // Scenario: User modifies path to include app name
    // Expected: Path should NOT be appended again
    std::string resolvedPath = resolver.resolveFinalPath(
        userPath,
        SpecialDirectoryType::INSTALL_DIRECTORY,
        appName
    );
    
    std::cout << "  Input: " << userPath << std::endl;
    std::cout << "  Output: " << resolvedPath << std::endl;
    
    // Verify app name was NOT duplicated
    assert(resolvedPath == "D:\\CustomPath\\MyApplication");
    
    // Count occurrences of app name (should be exactly 1)
    size_t pos = 0;
    int count = 0;
    while ((pos = resolvedPath.find(appName, pos)) != std::string::npos) {
        count++;
        pos += appName.length();
    }
    assert(count == 1);
    
    std::cout << "  PASSED" << std::endl;
}

// Test 12: Path resolution - environment variable paths
void testPathResolutionEnvironmentVariables() {
    std::cout << "\nTest 12: Path resolution - environment variable paths..." << std::endl;
    
    InstallerPathResolver resolver;
    std::string appName = "TestApp";
    
    // Test AppData Roaming
    std::string roamingPath = resolver.resolveFinalPath(
        "",  // Not used for environment variables
        SpecialDirectoryType::APPDATA_ROAMING,
        appName
    );
    std::cout << "  AppData Roaming: " << roamingPath << std::endl;
    assert(roamingPath.find("AppData\\Roaming") != std::string::npos);
    assert(roamingPath.find(appName) != std::string::npos);
    
    // Test AppData Local
    std::string localPath = resolver.resolveFinalPath(
        "",  // Not used for environment variables
        SpecialDirectoryType::APPDATA_LOCAL,
        appName
    );
    std::cout << "  AppData Local: " << localPath << std::endl;
    assert(localPath.find("AppData\\Local") != std::string::npos);
    assert(localPath.find(appName) != std::string::npos);
    
    // Test ProgramData
    std::string programDataPath = resolver.resolveFinalPath(
        "",  // Not used for environment variables
        SpecialDirectoryType::PROGRAM_DATA,
        appName
    );
    std::cout << "  ProgramData: " << programDataPath << std::endl;
    assert(programDataPath.find("ProgramData") != std::string::npos);
    assert(programDataPath.find(appName) != std::string::npos);
    
    std::cout << "  PASSED" << std::endl;
}

// Test 13: Path resolution - case sensitivity
void testPathResolutionCaseSensitivity() {
    std::cout << "\nTest 13: Path resolution - case sensitivity..." << std::endl;
    
    InstallerPathResolver resolver;
    std::string appName = "MyApp";
    
    // Test with different case variations
    std::string path1 = "D:\\CustomPath\\myapp";  // lowercase
    std::string resolved1 = resolver.resolveFinalPath(path1, SpecialDirectoryType::INSTALL_DIRECTORY, appName);
    std::cout << "  Input (lowercase): " << path1 << " -> " << resolved1 << std::endl;
    
    std::string path2 = "D:\\CustomPath\\MYAPP";  // uppercase
    std::string resolved2 = resolver.resolveFinalPath(path2, SpecialDirectoryType::INSTALL_DIRECTORY, appName);
    std::cout << "  Input (uppercase): " << path2 << " -> " << resolved2 << std::endl;
    
    std::string path3 = "D:\\CustomPath\\MyApp";  // exact match
    std::string resolved3 = resolver.resolveFinalPath(path3, SpecialDirectoryType::INSTALL_DIRECTORY, appName);
    std::cout << "  Input (exact): " << path3 << " -> " << resolved3 << std::endl;
    
    // On Windows, path comparison should be case-insensitive
    // So all three should NOT append the app name again
    assert(resolved1 == path1);
    assert(resolved2 == path2);
    assert(resolved3 == path3);
    
    std::cout << "  PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "End-to-End Integration Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // Packaging workflow tests
        testCompleteWorkflowDefaultInstallDir();
        testUserModifiesPathWithoutAppName();
        testUserModifiesPathWithAppName();
        testMultipleFolderTargets();
        testApplicationNameAppendingLogic();
        testLZMACompression();
        testEnvironmentVariablePaths();
        testDefaultConfiguration();
        
        // Path resolution scenario tests (Requirements 4.6)
        testPathResolutionDefaultDirectory();
        testPathResolutionModifiedWithoutAppName();
        testPathResolutionModifiedWithAppName();
        testPathResolutionEnvironmentVariables();
        testPathResolutionCaseSensitivity();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "All end-to-end tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\nTest failed with unknown exception" << std::endl;
        return 1;
    }
}
