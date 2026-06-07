#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace MultiThreadedInstaller {

class FileSystemOperator {
public:
    FileSystemOperator() = default;
    ~FileSystemOperator() = default;
    

    bool createDirectoryRecursive(const std::string& path);
    

    bool writeFile(const std::string& filePath, const std::vector<uint8_t>& data);
    

    bool verifyFileIntegrity(const std::string& filePath, uint32_t expectedChecksum);
    

    bool handleFileConflict(const std::string& filePath);
    

    bool fileExists(const std::string& filePath);
    

    size_t getFileSize(const std::string& filePath);
    

    bool directoryExists(const std::string& dirPath);
    

    uint32_t getFileChecksum(const std::string& filePath);
    
private:

    bool createSingleDirectory(const std::string& path);
    

    std::string getParentDirectory(const std::string& path);
    

    std::string normalizePath(const std::string& path);
    

    uint32_t calculateChecksum(const std::vector<uint8_t>& data);
};

} // namespace MultiThreadedInstaller
