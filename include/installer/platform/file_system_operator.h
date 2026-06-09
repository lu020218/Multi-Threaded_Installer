#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace MultiThreadedInstaller {

/// 文件系统操作封装（UTF-8 路径）：目录创建、写文件、校验、查询等的小工具集合。
class FileSystemOperator {
public:
    FileSystemOperator() = default;
    ~FileSystemOperator() = default;

    /// 递归创建目录（含所有中间层级）；已存在视为成功。
    bool createDirectoryRecursive(const std::string& path);
    /// 将字节写入文件（覆盖），必要时创建父目录。
    bool writeFile(const std::string& filePath, const std::vector<uint8_t>& data);
    /// 校验文件内容的校验和是否等于 expectedChecksum。
    bool verifyFileIntegrity(const std::string& filePath, uint32_t expectedChecksum);
    /// 处理目标文件冲突（如已存在/被占用时的重命名/排期替换策略）。
    bool handleFileConflict(const std::string& filePath);
    /// 文件是否存在。
    bool fileExists(const std::string& filePath);
    /// 取文件大小（字节）；不存在返回 0。
    size_t getFileSize(const std::string& filePath);
    /// 目录是否存在。
    bool directoryExists(const std::string& dirPath);
    /// 计算文件内容的 CRC32 校验和。
    uint32_t getFileChecksum(const std::string& filePath);

private:
    bool createSingleDirectory(const std::string& path);        ///< 创建单层目录。
    std::string getParentDirectory(const std::string& path);    ///< 取父目录路径。
    std::string normalizePath(const std::string& path);         ///< 归一化路径分隔符等。
    uint32_t calculateChecksum(const std::vector<uint8_t>& data);  ///< 计算字节缓冲的 CRC32。
};

} // namespace MultiThreadedInstaller
