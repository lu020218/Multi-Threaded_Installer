#include "packager/folder_scanner.h"
#include "common/utf8_utils.h"
#include <filesystem>
#include <iostream>

namespace MultiThreadedInstaller {

std::vector<FolderInfo> FolderScanner::scanInputDirectory(const std::string& inputPath) {
    std::vector<FolderInfo> folders;
    
    if (!isDirectory(inputPath)) {
        std::cerr << "Error: Input path is not a directory: " << inputPath << std::endl;
        return folders;
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(PathFromUtf8(inputPath))) {
            if (entry.is_directory()) {
                FolderInfo folderInfo;
                folderInfo.sourcePath = Utf8FromPath(entry.path());
                folderInfo.targetPath = Utf8FromPath(entry.path().filename());
                
                scanSingleFolder(folderInfo.sourcePath, folderInfo);
                folders.push_back(folderInfo);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
    }
    
    return folders;
}

bool FolderScanner::validateFolderStructure(const std::vector<FolderInfo>& folders) {
    if (folders.empty()) {
        std::cerr << "Error: No folders found to package" << std::endl;
        return false;
    }
    
    for (const auto& folder : folders) {
        if (!isDirectory(folder.sourcePath)) {
            std::cerr << "Error: Source path is not a directory: " << folder.sourcePath << std::endl;
            return false;
        }
        
        if (folder.files.empty()) {
            std::cerr << "Warning: Empty folder: " << folder.sourcePath << std::endl;
        }
        

        for (const auto& file : folder.files) {
            if (!isFileReadable(file)) {
                std::cerr << "Error: File is not readable: " << file << std::endl;
                return false;
            }
        }
    }
    
    return true;
}

void FolderScanner::scanSingleFolder(const std::string& folderPath, FolderInfo& folderInfo) {
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(PathFromUtf8(folderPath))) {
            if (entry.is_regular_file()) {
                folderInfo.files.push_back(Utf8FromPath(entry.path()));
            }
        }
        
        folderInfo.totalSize = calculateFolderSize(folderInfo.files);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error scanning folder " << folderPath << ": " << e.what() << std::endl;
    }
}

size_t FolderScanner::calculateFolderSize(const std::vector<std::string>& files) {
    size_t totalSize = 0;
    
    for (const auto& file : files) {
        try {
            totalSize += std::filesystem::file_size(PathFromUtf8(file));
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error getting file size for " << file << ": " << e.what() << std::endl;
        }
    }
    
    return totalSize;
}

bool FolderScanner::isDirectory(const std::string& path) {
    try {
        return std::filesystem::is_directory(PathFromUtf8(path));
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

bool FolderScanner::isFileReadable(const std::string& filePath) {
    try {
        auto path = PathFromUtf8(filePath);
        return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

} // namespace MultiThreadedInstaller
