#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include "installer/file_system_operator.h"

using namespace MultiThreadedInstaller;

void testCreateDirectoryRecursive() {
    std::cout << "Testing createDirectoryRecursive..." << std::endl;
    
    FileSystemOperator fsOp;
    
    // Test creating nested directories
    std::string testPath = "test_output/nested/deep/directory";
    
    // Clean up first
    if (std::filesystem::exists("test_output")) {
        std::filesystem::remove_all("test_output");
    }
    
    bool result = fsOp.createDirectoryRecursive(testPath);
    assert(result == true);
    assert(fsOp.directoryExists(testPath) == true);
    
    // Test creating existing directory (should succeed)
    result = fsOp.createDirectoryRecursive(testPath);
    assert(result == true);
    
    // Clean up
    std::filesystem::remove_all("test_output");
    
    std::cout << "✓ createDirectoryRecursive tests passed" << std::endl;
}

void testWriteFile() {
    std::cout << "Testing writeFile..." << std::endl;
    
    FileSystemOperator fsOp;
    
    // Create test directory
    std::string testDir = "test_output";
    fsOp.createDirectoryRecursive(testDir);
    
    // Test writing file
    std::string filePath = testDir + "/test_file.txt";
    std::vector<uint8_t> testData = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};
    
    bool result = fsOp.writeFile(filePath, testData);
    assert(result == true);
    assert(fsOp.fileExists(filePath) == true);
    assert(fsOp.getFileSize(filePath) == testData.size());
    
    // Test overwriting file (should succeed due to conflict handling)
    std::vector<uint8_t> newData = {'N', 'e', 'w', ' ', 'D', 'a', 't', 'a'};
    result = fsOp.writeFile(filePath, newData);
    assert(result == true);
    assert(fsOp.getFileSize(filePath) == newData.size());
    
    // Clean up
    std::filesystem::remove_all("test_output");
    
    std::cout << "✓ writeFile tests passed" << std::endl;
}

void testVerifyFileIntegrity() {
    std::cout << "Testing verifyFileIntegrity..." << std::endl;
    
    FileSystemOperator fsOp;
    
    // Create test directory
    std::string testDir = "test_output";
    fsOp.createDirectoryRecursive(testDir);
    
    // Create test file
    std::string filePath = testDir + "/integrity_test.txt";
    std::vector<uint8_t> testData = {'T', 'e', 's', 't', ' ', 'D', 'a', 't', 'a'};
    
    bool result = fsOp.writeFile(filePath, testData);
    assert(result == true);
    
    // Get the actual checksum
    uint32_t actualChecksum = fsOp.getFileChecksum(filePath);
    
    // Test with correct checksum
    result = fsOp.verifyFileIntegrity(filePath, actualChecksum);
    assert(result == true);
    
    // Test with incorrect checksum
    result = fsOp.verifyFileIntegrity(filePath, actualChecksum + 1);
    assert(result == false);
    
    // Test with non-existent file
    result = fsOp.verifyFileIntegrity("non_existent.txt", actualChecksum);
    assert(result == false);
    
    // Clean up
    std::filesystem::remove_all("test_output");
    
    std::cout << "✓ verifyFileIntegrity tests passed" << std::endl;
}

void testHandleFileConflict() {
    std::cout << "Testing handleFileConflict..." << std::endl;
    
    FileSystemOperator fsOp;
    
    // Create test directory
    std::string testDir = "test_output";
    fsOp.createDirectoryRecursive(testDir);
    
    // Create test file
    std::string filePath = testDir + "/conflict_test.txt";
    std::vector<uint8_t> testData = {'O', 'r', 'i', 'g', 'i', 'n', 'a', 'l'};
    
    bool result = fsOp.writeFile(filePath, testData);
    assert(result == true);
    assert(fsOp.fileExists(filePath) == true);
    
    // Handle conflict (should remove the file)
    result = fsOp.handleFileConflict(filePath);
    assert(result == true);
    assert(fsOp.fileExists(filePath) == false);
    
    // Test handling conflict on non-existent file (should succeed)
    result = fsOp.handleFileConflict("non_existent.txt");
    assert(result == true);
    
    // Clean up
    std::filesystem::remove_all("test_output");
    
    std::cout << "✓ handleFileConflict tests passed" << std::endl;
}

void testFileExists() {
    std::cout << "Testing fileExists..." << std::endl;
    
    FileSystemOperator fsOp;
    
    // Create test directory
    std::string testDir = "test_output";
    fsOp.createDirectoryRecursive(testDir);
    
    // Test non-existent file
    std::string filePath = testDir + "/exists_test.txt";
    assert(fsOp.fileExists(filePath) == false);
    
    // Create file and test
    std::vector<uint8_t> testData = {'T', 'e', 's', 't'};
    fsOp.writeFile(filePath, testData);
    assert(fsOp.fileExists(filePath) == true);
    
    // Test directory (should return false for directories)
    assert(fsOp.fileExists(testDir) == false);
    
    // Clean up
    std::filesystem::remove_all("test_output");
    
    std::cout << "✓ fileExists tests passed" << std::endl;
}

void testGetFileSize() {
    std::cout << "Testing getFileSize..." << std::endl;
    
    FileSystemOperator fsOp;
    
    // Create test directory
    std::string testDir = "test_output";
    fsOp.createDirectoryRecursive(testDir);
    
    // Test non-existent file
    std::string filePath = testDir + "/size_test.txt";
    assert(fsOp.getFileSize(filePath) == 0);
    
    // Create file with known size
    std::vector<uint8_t> testData = {'1', '2', '3', '4', '5'};
    fsOp.writeFile(filePath, testData);
    assert(fsOp.getFileSize(filePath) == 5);
    
    // Test empty file
    std::string emptyFilePath = testDir + "/empty.txt";
    std::vector<uint8_t> emptyData;
    fsOp.writeFile(emptyFilePath, emptyData);
    assert(fsOp.getFileSize(emptyFilePath) == 0);
    
    // Clean up
    std::filesystem::remove_all("test_output");
    
    std::cout << "✓ getFileSize tests passed" << std::endl;
}

void testDirectoryExists() {
    std::cout << "Testing directoryExists..." << std::endl;
    
    FileSystemOperator fsOp;
    
    // Test non-existent directory
    assert(fsOp.directoryExists("non_existent_dir") == false);
    
    // Create directory and test
    std::string testDir = "test_output";
    fsOp.createDirectoryRecursive(testDir);
    assert(fsOp.directoryExists(testDir) == true);
    
    // Create file and test (should return false for files)
    std::string filePath = testDir + "/test_file.txt";
    std::vector<uint8_t> testData = {'T', 'e', 's', 't'};
    fsOp.writeFile(filePath, testData);
    assert(fsOp.directoryExists(filePath) == false);
    
    // Clean up
    std::filesystem::remove_all("test_output");
    
    std::cout << "✓ directoryExists tests passed" << std::endl;
}

void testChecksumCalculation() {
    std::cout << "Testing checksum calculation..." << std::endl;
    
    FileSystemOperator fsOp;
    
    // Create test directory
    std::string testDir = "test_output";
    fsOp.createDirectoryRecursive(testDir);
    
    // Test with known data
    std::string filePath = testDir + "/checksum_test.txt";
    std::vector<uint8_t> testData = {'A', 'B', 'C', 'D'};
    
    fsOp.writeFile(filePath, testData);
    uint32_t checksum1 = fsOp.getFileChecksum(filePath);
    
    // Same data should produce same checksum
    std::string filePath2 = testDir + "/checksum_test2.txt";
    fsOp.writeFile(filePath2, testData);
    uint32_t checksum2 = fsOp.getFileChecksum(filePath2);
    assert(checksum1 == checksum2);
    
    // Different data should produce different checksum
    std::vector<uint8_t> differentData = {'A', 'B', 'C', 'E'};
    std::string filePath3 = testDir + "/checksum_test3.txt";
    fsOp.writeFile(filePath3, differentData);
    uint32_t checksum3 = fsOp.getFileChecksum(filePath3);
    assert(checksum1 != checksum3);
    
    // Non-existent file should return 0
    uint32_t checksum4 = fsOp.getFileChecksum("non_existent.txt");
    assert(checksum4 == 0);
    
    // Clean up
    std::filesystem::remove_all("test_output");
    
    std::cout << "✓ checksum calculation tests passed" << std::endl;
}

int main() {
    std::cout << "Running FileSystemOperator unit tests..." << std::endl;
    
    try {
        testCreateDirectoryRecursive();
        testWriteFile();
        testVerifyFileIntegrity();
        testHandleFileConflict();
        testFileExists();
        testGetFileSize();
        testDirectoryExists();
        testChecksumCalculation();
        
        std::cout << "\n✅ All FileSystemOperator unit tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\n❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}