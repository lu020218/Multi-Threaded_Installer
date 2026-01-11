#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <sstream>
#include "installer/console_interface.h"

using namespace MultiThreadedInstaller;

void testParsePackagerArgs() {
    std::cout << "Testing parsePackagerArgs..." << std::endl;
    
    ConsoleInterface console;
    
    // Test basic arguments
    {
        const char* argv[] = {"packager", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        
        assert(args.inputPath == "./input");
        assert(args.outputPath == "./output.exe");
        assert(args.algorithm == CompressionAlgorithm::ZSTD_FAST);
        assert(args.compressionLevel == -1);
        assert(args.threadCount == -1);
        assert(args.verbose == false);
        assert(args.showHelp == false);
    }
    
    // Test with algorithm option
    {
        const char* argv[] = {"packager", "-a", "lzma", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        
        assert(args.inputPath == "./input");
        assert(args.outputPath == "./output.exe");
        assert(args.algorithm == CompressionAlgorithm::LZMA_HIGH);
    }
    
    // Test with long algorithm option
    {
        const char* argv[] = {"packager", "--algorithm", "zstd", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        
        assert(args.algorithm == CompressionAlgorithm::ZSTD_FAST);
    }
    
    // Test with compression level
    {
        const char* argv[] = {"packager", "-l", "5", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        
        assert(args.compressionLevel == 5);
    }
    
    // Test with thread count
    {
        const char* argv[] = {"packager", "-t", "8", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        
        assert(args.threadCount == 8);
    }
    
    // Test with verbose flag
    {
        const char* argv[] = {"packager", "-v", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        
        assert(args.verbose == true);
    }
    
    // Test with help flag
    {
        const char* argv[] = {"packager", "-h"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        
        assert(args.showHelp == true);
    }
    
    // Test with all options
    {
        const char* argv[] = {"packager", "-a", "lzma", "-l", "9", "-t", "4", "-v", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        
        assert(args.inputPath == "./input");
        assert(args.outputPath == "./output.exe");
        assert(args.algorithm == CompressionAlgorithm::LZMA_HIGH);
        assert(args.compressionLevel == 9);
        assert(args.threadCount == 4);
        assert(args.verbose == true);
        assert(args.showHelp == false);
    }
    
    std::cout << "✓ parsePackagerArgs tests passed" << std::endl;
}

void testParseInstallerArgs() {
    std::cout << "Testing parseInstallerArgs..." << std::endl;
    
    ConsoleInterface console;
    
    // Test basic arguments with default destination
    {
        const char* argv[] = {"installer", "-d", "C:\\Program Files\\MyApp"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.defaultDestination == "C:\\Program Files\\MyApp");
        assert(args.folderMappings.empty());
        assert(args.threadCount == -1);
        assert(args.force == false);
        assert(args.silent == false);
        assert(args.verbose == false);
        assert(args.showHelp == false);
    }
    
    // Test with folder mappings
    {
        const char* argv[] = {"installer", "folderA=C:\\App\\A", "folderB=C:\\App\\B"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.folderMappings.size() == 2);
        assert(args.folderMappings[0].first == "folderA");
        assert(args.folderMappings[0].second == "C:\\App\\A");
        assert(args.folderMappings[1].first == "folderB");
        assert(args.folderMappings[1].second == "C:\\App\\B");
    }
    
    // Test with thread count
    {
        const char* argv[] = {"installer", "-t", "6"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.threadCount == 6);
    }
    
    // Test with force flag
    {
        const char* argv[] = {"installer", "-f"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.force == true);
    }
    
    // Test with silent flag
    {
        const char* argv[] = {"installer", "-s"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.silent == true);
    }
    
    // Test with verbose flag
    {
        const char* argv[] = {"installer", "-v"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.verbose == true);
    }
    
    // Test with help flag
    {
        const char* argv[] = {"installer", "--help"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.showHelp == true);
    }
    
    // Test with all options
    {
        const char* argv[] = {"installer", "-d", "C:\\MyApp", "-t", "8", "-f", "-s", "-v", "app=C:\\App", "data=C:\\Data"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.defaultDestination == "C:\\MyApp");
        assert(args.threadCount == 8);
        assert(args.force == true);
        assert(args.silent == true);
        assert(args.verbose == true);
        assert(args.folderMappings.size() == 2);
        assert(args.folderMappings[0].first == "app");
        assert(args.folderMappings[0].second == "C:\\App");
        assert(args.folderMappings[1].first == "data");
        assert(args.folderMappings[1].second == "C:\\Data");
    }
    
    std::cout << "✓ parseInstallerArgs tests passed" << std::endl;
}

void testCompressionAlgorithmParsing() {
    std::cout << "Testing compression algorithm parsing..." << std::endl;
    
    ConsoleInterface console;
    
    // Test parseCompressionAlgorithm (private method tested through parsePackagerArgs)
    {
        const char* argv[] = {"packager", "-a", "zstd", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        assert(args.algorithm == CompressionAlgorithm::ZSTD_FAST);
    }
    
    {
        const char* argv[] = {"packager", "-a", "lzma", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        assert(args.algorithm == CompressionAlgorithm::LZMA_HIGH);
    }
    
    {
        const char* argv[] = {"packager", "-a", "7z", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        assert(args.algorithm == CompressionAlgorithm::LZMA_HIGH);
    }
    
    // Test case insensitive
    {
        const char* argv[] = {"packager", "-a", "ZSTD", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        assert(args.algorithm == CompressionAlgorithm::ZSTD_FAST);
    }
    
    // Test invalid algorithm (should default to ZSTD)
    {
        const char* argv[] = {"packager", "-a", "invalid", "./input", "./output.exe"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        assert(args.algorithm == CompressionAlgorithm::ZSTD_FAST);
    }
    
    std::cout << "✓ compression algorithm parsing tests passed" << std::endl;
}

void testProgressBarDisplay() {
    std::cout << "Testing progress bar display..." << std::endl;
    
    ConsoleInterface console;
    
    // Test progress display methods (these don't return values, so we just ensure they don't crash)
    try {
        console.showPackagingProgress("test_folder", 0.0f);
        console.showPackagingProgress("test_folder", 0.5f);
        console.showPackagingProgress("test_folder", 1.0f);
        
        console.showInstallationProgress("install_folder", 0.25f);
        console.showInstallationProgress("install_folder", 0.75f);
        console.showInstallationProgress("install_folder", 1.0f);
        
        std::cout << "✓ progress bar display tests passed" << std::endl;
    } catch (...) {
        std::cerr << "❌ progress bar display tests failed" << std::endl;
        throw;
    }
}

void testMessageDisplay() {
    std::cout << "Testing message display..." << std::endl;
    
    ConsoleInterface console;
    
    // Test message display methods (these don't return values, so we just ensure they don't crash)
    try {
        console.showError("Test error message");
        console.showWarning("Test warning message");
        console.showInfo("Test info message");
        
        std::vector<std::string> errors = {"Error 1", "Error 2", "Error 3"};
        console.showInstallationResult(true, {});
        console.showInstallationResult(false, errors);
        
        std::cout << "✓ message display tests passed" << std::endl;
    } catch (...) {
        std::cerr << "❌ message display tests failed" << std::endl;
        throw;
    }
}

void testHelpDisplay() {
    std::cout << "Testing help display..." << std::endl;
    
    ConsoleInterface console;
    
    // Test help display methods (these don't return values, so we just ensure they don't crash)
    try {
        console.showPackagerHelp();
        console.showInstallerHelp();
        
        std::cout << "✓ help display tests passed" << std::endl;
    } catch (...) {
        std::cerr << "❌ help display tests failed" << std::endl;
        throw;
    }
}

void testMenuDisplay() {
    std::cout << "Testing menu display..." << std::endl;
    
    ConsoleInterface console;
    
    // Test menu display methods (these don't return values, so we just ensure they don't crash)
    try {
        console.showPackagerMenu();
        console.showInstallerMenu();
        
        std::cout << "✓ menu display tests passed" << std::endl;
    } catch (...) {
        std::cerr << "❌ menu display tests failed" << std::endl;
        throw;
    }
}

void testEdgeCases() {
    std::cout << "Testing edge cases..." << std::endl;
    
    ConsoleInterface console;
    
    // Test empty arguments
    {
        const char* argv[] = {"packager"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parsePackagerArgs(argc, const_cast<char**>(argv));
        
        assert(args.inputPath.empty());
        assert(args.outputPath.empty());
        assert(args.algorithm == CompressionAlgorithm::ZSTD_FAST);
    }
    
    // Test installer with no arguments
    {
        const char* argv[] = {"installer"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.defaultDestination.empty());
        assert(args.folderMappings.empty());
    }
    
    // Test malformed folder mapping (missing equals sign)
    {
        const char* argv[] = {"installer", "malformed_mapping"};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        // Malformed mappings should be ignored
        assert(args.folderMappings.empty());
    }
    
    // Test folder mapping with empty parts
    {
        const char* argv[] = {"installer", "=empty_folder", "empty_path="};
        int argc = sizeof(argv) / sizeof(argv[0]);
        
        auto args = console.parseInstallerArgs(argc, const_cast<char**>(argv));
        
        assert(args.folderMappings.size() == 2);
        assert(args.folderMappings[0].first.empty());
        assert(args.folderMappings[0].second == "empty_folder");
        assert(args.folderMappings[1].first == "empty_path");
        assert(args.folderMappings[1].second.empty());
    }
    
    std::cout << "✓ edge cases tests passed" << std::endl;
}

int main() {
    std::cout << "Running ConsoleInterface unit tests..." << std::endl;
    
    try {
        testParsePackagerArgs();
        testParseInstallerArgs();
        testCompressionAlgorithmParsing();
        testProgressBarDisplay();
        testMessageDisplay();
        testHelpDisplay();
        testMenuDisplay();
        testEdgeCases();
        
        std::cout << "\n✅ All ConsoleInterface unit tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\n❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}