#include "packager/compression_module.h"
#include <iostream>
#include <filesystem>
#include <fstream>

using namespace MultiThreadedInstaller;

int main() {
    std::cout << "Testing full compression module integration..." << std::endl;
    
    // Create a test folder with some files
    std::string testDir = "test_compression_folder";
    std::filesystem::create_directories(testDir);
    
    // Create test files
    std::vector<std::string> testFiles = {
        testDir + "/file1.txt",
        testDir + "/file2.txt",
        testDir + "/subdir/file3.txt"
    };
    
    std::filesystem::create_directories(testDir + "/subdir");
    
    for (size_t i = 0; i < testFiles.size(); ++i) {
        std::ofstream file(testFiles[i]);
        file << "This is test file " << (i + 1) << " with some content for compression testing. ";
        file << "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
        file << "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";
        file.close();
    }
    
    // Create FolderInfo
    FolderInfo folderInfo;
    folderInfo.sourcePath = testDir;
    folderInfo.targetPath = "target";
    folderInfo.files = testFiles;
    
    // Calculate total size
    for (const auto& file : testFiles) {
        folderInfo.totalSize += std::filesystem::file_size(file);
    }
    
    std::cout << "Created test folder with " << testFiles.size() << " files" << std::endl;
    std::cout << "Total size: " << folderInfo.totalSize << " bytes" << std::endl;
    
    // Test compression module
    CompressionModule compressor;
    
    // Test ZSTD compression
    std::cout << "\n=== Testing ZSTD Compression ===" << std::endl;
    compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
    compressor.setCompressionLevel(1);
    
    auto zstdResult = compressor.compressFolder(folderInfo);
    
    if (!zstdResult.compressedData.empty()) {
        std::cout << "✓ ZSTD compression successful!" << std::endl;
        std::cout << "  Original size: " << zstdResult.originalSize << " bytes" << std::endl;
        std::cout << "  Compressed size: " << zstdResult.compressedSize << " bytes" << std::endl;
        std::cout << "  Compression ratio: " << (double)zstdResult.compressedSize / zstdResult.originalSize << std::endl;
        std::cout << "  Checksum: " << zstdResult.checksum << std::endl;
    } else {
        std::cout << "✗ ZSTD compression failed!" << std::endl;
    }
    
    // Test LZMA compression
    std::cout << "\n=== Testing LZMA Compression ===" << std::endl;
    compressor.setCompressionAlgorithm(CompressionAlgorithm::LZMA_HIGH);
    compressor.setCompressionLevel(5);
    
    auto lzmaResult = compressor.compressFolder(folderInfo);
    
    if (!lzmaResult.compressedData.empty()) {
        std::cout << "✓ LZMA compression successful!" << std::endl;
        std::cout << "  Original size: " << lzmaResult.originalSize << " bytes" << std::endl;
        std::cout << "  Compressed size: " << lzmaResult.compressedSize << " bytes" << std::endl;
        std::cout << "  Compression ratio: " << (double)lzmaResult.compressedSize / lzmaResult.originalSize << std::endl;
        std::cout << "  Checksum: " << lzmaResult.checksum << std::endl;
    } else {
        std::cout << "✗ LZMA compression failed!" << std::endl;
    }
    
    // Compare compression ratios
    if (!zstdResult.compressedData.empty() && !lzmaResult.compressedData.empty()) {
        std::cout << "\n=== Compression Comparison ===" << std::endl;
        double zstdRatio = (double)zstdResult.compressedSize / zstdResult.originalSize;
        double lzmaRatio = (double)lzmaResult.compressedSize / lzmaResult.originalSize;
        
        std::cout << "ZSTD ratio: " << zstdRatio << std::endl;
        std::cout << "LZMA ratio: " << lzmaRatio << std::endl;
        
        if (lzmaRatio < zstdRatio) {
            std::cout << "✓ LZMA achieved better compression ratio (as expected)" << std::endl;
        } else {
            std::cout << "! ZSTD achieved better or equal compression ratio" << std::endl;
        }
    }
    
    // Cleanup
    std::filesystem::remove_all(testDir);
    
    std::cout << "\nTest completed!" << std::endl;
    return 0;
}