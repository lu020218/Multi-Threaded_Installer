#include "packager/folder_scanner.h"
#include <iostream>
#include <filesystem>
#include <cassert>
#include <fstream>

using namespace MultiThreadedInstaller;

// Simple test framework
void test_scanInputDirectory() {
    std::cout << "Testing scanInputDirectory..." << std::endl;
    
    // Create test directory structure
    std::filesystem::create_directories("test_input/folder1");
    std::filesystem::create_directories("test_input/folder2");
    
    // Create test files
    std::ofstream("test_input/folder1/file1.txt") << "test content 1";
    std::ofstream("test_input/folder1/file2.txt") << "test content 2";
    std::ofstream("test_input/folder2/file3.txt") << "test content 3";
    
    FolderScanner scanner;
    auto folders = scanner.scanInputDirectory("test_input");
    
    // Verify results
    assert(folders.size() == 2);
    
    bool found_folder1 = false, found_folder2 = false;
    for (const auto& folder : folders) {
        if (folder.targetPath == "folder1") {
            found_folder1 = true;
            assert(folder.files.size() == 2);
            assert(folder.totalSize > 0);
        } else if (folder.targetPath == "folder2") {
            found_folder2 = true;
            assert(folder.files.size() == 1);
            assert(folder.totalSize > 0);
        }
    }
    
    assert(found_folder1 && found_folder2);
    
    // Cleanup
    std::filesystem::remove_all("test_input");
    
    std::cout << "scanInputDirectory test passed!" << std::endl;
}

void test_validateFolderStructure() {
    std::cout << "Testing validateFolderStructure..." << std::endl;
    
    // Create test directory structure
    std::filesystem::create_directories("test_validate/folder1");
    std::ofstream("test_validate/folder1/file1.txt") << "test content";
    
    FolderScanner scanner;
    auto folders = scanner.scanInputDirectory("test_validate");
    
    // Test validation
    bool isValid = scanner.validateFolderStructure(folders);
    assert(isValid);
    
    // Test empty folder list
    std::vector<FolderInfo> emptyFolders;
    bool isEmpty = scanner.validateFolderStructure(emptyFolders);
    assert(!isEmpty);
    
    // Cleanup
    std::filesystem::remove_all("test_validate");
    
    std::cout << "validateFolderStructure test passed!" << std::endl;
}

void test_nonExistentDirectory() {
    std::cout << "Testing non-existent directory..." << std::endl;
    
    FolderScanner scanner;
    auto folders = scanner.scanInputDirectory("non_existent_directory");
    
    // Should return empty vector for non-existent directory
    assert(folders.empty());
    
    std::cout << "Non-existent directory test passed!" << std::endl;
}

int main() {
    try {
        test_scanInputDirectory();
        test_validateFolderStructure();
        test_nonExistentDirectory();
        
        std::cout << "All FolderScanner tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}