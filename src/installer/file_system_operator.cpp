#include "installer/file_system_operator.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace MultiThreadedInstaller {

bool FileSystemOperator::createDirectoryRecursive(const std::string& path) {
    try {
        // create_directories returns false if directory already exists
        // but we want to return true if the directory exists after the call
        std::filesystem::create_directories(path);
        return std::filesystem::exists(path) && std::filesystem::is_directory(path);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Failed to create directory " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool FileSystemOperator::writeFile(const std::string& filePath, const std::vector<uint8_t>& data) {
    try {
        // 处理文件冲突（直接覆盖）
        if (fileExists(filePath)) {
            if (!handleFileConflict(filePath)) {
                return false;
            }
        }
        
        std::ofstream file(filePath, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to create file: " << filePath << std::endl;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        return file.good();
        
    } catch (const std::exception& e) {
        std::cerr << "Error writing file " << filePath << ": " << e.what() << std::endl;
        return false;
    }
}

bool FileSystemOperator::verifyFileIntegrity(const std::string& filePath, uint32_t expectedChecksum) {
    if (!fileExists(filePath)) {
        return false;
    }
    
    uint32_t actualChecksum = getFileChecksum(filePath);
    return actualChecksum == expectedChecksum;
}

bool FileSystemOperator::handleFileConflict(const std::string& filePath) {
    // 直接覆盖策略 - 删除现有文件
    try {
        if (fileExists(filePath)) {
            std::filesystem::remove(filePath);
        }
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Failed to handle file conflict for " << filePath << ": " << e.what() << std::endl;
        return false;
    }
}

bool FileSystemOperator::fileExists(const std::string& filePath) {
    try {
        return std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

size_t FileSystemOperator::getFileSize(const std::string& filePath) {
    try {
        return std::filesystem::file_size(filePath);
    } catch (const std::filesystem::filesystem_error&) {
        return 0;
    }
}

bool FileSystemOperator::directoryExists(const std::string& dirPath) {
    try {
        return std::filesystem::exists(dirPath) && std::filesystem::is_directory(dirPath);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

uint32_t FileSystemOperator::getFileChecksum(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return 0;
    }
    
    std::vector<uint8_t> content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return calculateChecksum(content);
}

bool FileSystemOperator::createSingleDirectory(const std::string& path) {
    try {
        return std::filesystem::create_directory(path);
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

std::string FileSystemOperator::getParentDirectory(const std::string& path) {
    try {
        return std::filesystem::path(path).parent_path().string();
    } catch (const std::exception&) {
        return "";
    }
}

std::string FileSystemOperator::normalizePath(const std::string& path) {
    try {
        return std::filesystem::path(path).lexically_normal().string();
    } catch (const std::exception&) {
        return path;
    }
}

uint32_t FileSystemOperator::calculateChecksum(const std::vector<uint8_t>& data) {
    // 简单的CRC32实现
    uint32_t crc = 0xFFFFFFFF;
    
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; i++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return ~crc;
}

} // namespace MultiThreadedInstaller