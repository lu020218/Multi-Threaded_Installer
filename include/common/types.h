#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace MultiThreadedInstaller {

// 压缩算法枚举
enum class CompressionAlgorithm {
    LZMA_HIGH     // 7z LZMA高压缩比模式
};

// 特殊目录类型枚举
enum class SpecialDirectoryType {
    INSTALL_DIRECTORY,  // 用户选择的安装目录
    PROGRAM_FILES,      // %ProgramFiles%
    APPDATA_ROAMING,    // %AppData%
    APPDATA_LOCAL,      // %LocalAppData%
    PROGRAM_DATA        // %ProgramData%
};

// 安装状态写入方式
enum class InstallStateMode {
    REGISTRY,
    FILE,
    BOTH
};

// 注册表值类型
enum class RegistryValueType {
    STRING,
    DWORD,
    EXPAND_STRING
};

// 安装状态配置
struct InstallStateConfig {
    InstallStateMode mode;
    std::string registryPath;
    std::string registryKey;
    std::string filePath;
    bool useMutex;
    std::string mutexName;
    
    InstallStateConfig()
        : mode(InstallStateMode::REGISTRY),
          registryKey("InstallState"),
          useMutex(true) {}
};

// 文件夹信息结构
struct FolderInfo {
    std::string sourcePath;
    std::string targetPath;
    std::vector<std::string> files;
    size_t totalSize;
    
    FolderInfo() : totalSize(0) {}
    
    FolderInfo(const std::string& source, const std::string& target)
        : sourcePath(source), targetPath(target), totalSize(0) {}
};

// 文件夹目标目录配置
struct FolderTargetConfig {
    std::string folderName;           // 文件夹名称（相对于输入目录）
    std::string targetDirectory;      // 目标目录配置字符串
    SpecialDirectoryType dirType;     // 目标目录类型
    
    FolderTargetConfig()
        : dirType(SpecialDirectoryType::INSTALL_DIRECTORY) {}
};

// 注册表配置
struct RegistryEntry {
    std::string path;   // 注册表路径
    std::string key;    // 键名
    std::string value;  // 值
    RegistryValueType type; // 值类型
    
    RegistryEntry()
        : type(RegistryValueType::STRING) {}
};

// 打包器配置
struct PackagerConfiguration {
    std::string version;                           // 配置版本
    std::string applicationName;                    // 应用程序名称
    std::string defaultInstallDir;                  // 建议的默认安装目录（不含应用程序名）
    std::string iconPath;                           // 安装程序图标路径（可选）
    std::string webPageUrl;                         // 介绍网页URL（可选）
    std::string productName;                        // 产品名称（可选）
    std::string fileVersion;                        // 文件版本（可选）
    std::string productVersion;                     // 产品版本（可选）
    std::string companyName;                        // 公司名称（可选）
    std::string fileDescription;                    // 文件描述（可选）
    std::string copyright;                          // 版权信息（可选）
    CompressionAlgorithm compressionAlgorithm;      // 压缩算法
    std::vector<FolderTargetConfig> folderTargets;  // 文件夹目标配置
    std::vector<RegistryEntry> registry;           // 注册表配置（预留）
    bool autoStartup;                              // 默认开机自启动（预留）
    bool desktopIcons;                             // 默认创建桌面图标（预留）
    bool autoCleanOldInstall;                      // 自动清理旧安装目录（仅当目录不同）
    bool requireAdmin;                             // 安装程序需要管理员权限
    uint16_t minWindowsMajor;                      // 最小Windows主版本
    uint16_t minWindowsMinor;                      // 最小Windows次版本
    uint32_t minWindowsBuild;                      // 最小Windows构建号
    uint64_t sparseFileThresholdBytes;             // 稀疏文件阈值（字节）
    InstallStateConfig installState;               // 安装状态写入配置
    
    // 默认值
    PackagerConfiguration() 
        : version("1.0"),
          applicationName("MyApplication"),
          defaultInstallDir("%ProgramFiles%"),
          compressionAlgorithm(CompressionAlgorithm::LZMA_HIGH),
          autoStartup(false),
          desktopIcons(false),
          autoCleanOldInstall(false),
          requireAdmin(false),
          minWindowsMajor(0),
          minWindowsMinor(0),
          minWindowsBuild(0),
          sparseFileThresholdBytes(4 * 1024 * 1024) {}
};

// 文件索引条目
struct FileIndexEntry {
    std::string relativePath;
    uint64_t offset;
    uint64_t size;
};

// 块索引条目
struct BlockIndexEntry {
    uint32_t blockId;
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint32_t checksum;
};

// 压缩结果结构
struct CompressionResult {
    std::vector<uint8_t> compressedData;
    uint32_t checksum;
    size_t originalSize;
    size_t compressedSize;
    CompressionAlgorithm algorithm;
    std::vector<FileIndexEntry> fileIndex;
    std::vector<BlockIndexEntry> blockIndex;
    
    CompressionResult() : checksum(0), originalSize(0), compressedSize(0),
                         algorithm(CompressionAlgorithm::LZMA_HIGH) {}
};

// 文件夹映射结构
struct FolderMapping {
    std::string folderName;
    std::string targetPath;
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint32_t checksum;
    CompressionAlgorithm algorithm;
    
    FolderMapping() : offset(0), compressedSize(0), originalSize(0),
                     checksum(0), algorithm(CompressionAlgorithm::LZMA_HIGH) {}
};

// 扩展的文件夹映射结构（向后兼容）
struct ExtendedFolderMapping : public FolderMapping {
    SpecialDirectoryType targetDirType;   // 目标目录类型
    std::string customTargetPath;         // 自定义目标路径
    std::vector<FileIndexEntry> fileIndex;
    std::vector<BlockIndexEntry> blockIndex;
    
    ExtendedFolderMapping() 
        : FolderMapping(),
          targetDirType(SpecialDirectoryType::INSTALL_DIRECTORY) {}
};

// 安装元数据结构
struct InstallationMetadata {
    uint32_t version;
    uint32_t folderCount;
    std::vector<FolderMapping> folderMappings;
    uint64_t totalCompressedSize;
    
    InstallationMetadata() : version(1), folderCount(0), totalCompressedSize(0) {}
};

// 扩展的安装元数据结构（向后兼容）
struct ExtendedInstallationMetadata : public InstallationMetadata {
    std::string applicationName;                    // 应用程序名称
    std::string configVersion;                      // 配置版本
    std::string defaultInstallDir;                  // 建议的默认安装目录
    std::string webPageUrl;                         // 介绍网页URL
    bool autoStartup;                               // 默认开机自启动
    bool desktopIcons;                              // 默认创建桌面图标
    bool autoCleanOldInstall;                       // 自动清理旧安装目录（仅当目录不同）
    bool requireAdmin;                              // 安装程序需要管理员权限
    uint16_t minWindowsMajor;                       // 最小Windows主版本
    uint16_t minWindowsMinor;                       // 最小Windows次版本
    uint32_t minWindowsBuild;                       // 最小Windows构建号
    uint64_t sparseFileThresholdBytes;             // 稀疏文件阈值（字节）
    InstallStateConfig installState;               // 安装状态写入配置
    std::vector<ExtendedFolderMapping> extendedMappings; // 扩展的文件夹映射
    std::vector<RegistryEntry> registry;            // 注册表写入配置
    
    ExtendedInstallationMetadata() 
        : InstallationMetadata(),
          applicationName("MyApplication"),
          configVersion("1.0"),
          defaultInstallDir("%ProgramFiles%"),
          webPageUrl(""),
          autoStartup(false),
          desktopIcons(false),
          autoCleanOldInstall(false),
          requireAdmin(false),
          minWindowsMajor(0),
          minWindowsMinor(0),
          minWindowsBuild(0),
          sparseFileThresholdBytes(4 * 1024 * 1024) {}
};

// 二进制元数据头结构
struct BinaryMetadata {
    uint32_t magic;           // 魔数标识: 0x4D544950 ("MTIP")
    uint32_t version;         // 版本号
    uint32_t folderCount;     // 文件夹数量
    uint64_t metadataSize;    // 元数据总大小
    uint64_t dataOffset;      // 压缩数据起始偏移
    
    BinaryMetadata() : magic(0x4D544950), version(1), folderCount(0), 
                      metadataSize(0), dataOffset(0) {}
};

// 解压任务结构
struct DecompressionTask {
    std::vector<uint8_t> compressedData;
    std::string targetPath;
    uint32_t expectedChecksum;
    size_t originalSize;
    CompressionAlgorithm algorithm;
    
    DecompressionTask() : expectedChecksum(0), originalSize(0),
                         algorithm(CompressionAlgorithm::LZMA_HIGH) {}
};

// 进度回调函数类型
using ProgressCallback = std::function<void(const std::string&, float)>;

// 常量定义
namespace Constants {
    constexpr uint32_t MAGIC_NUMBER = 0x4D544950;  // "MTIP"
    constexpr uint32_t DATA_MAGIC_NUMBER = 0x4D544450;  // "MTDP"
    constexpr uint32_t VERSION = 10;
    
    // 块大小配置 (优化后)
    constexpr size_t DEFAULT_BLOCK_SIZE = 128 * 1024 * 1024;  // 2MB (从 64KB 优化)
    constexpr size_t MIN_BLOCK_SIZE = 4 * 1024 * 1024;      // 1MB
    constexpr size_t MAX_BLOCK_SIZE = 128 * 1024 * 1024;      // 8MB
    
    constexpr int DEFAULT_LZMA_LEVEL = 9;          // 平衡压缩
}

// 外部数据包头
struct DataPackageHeader {
    uint32_t magic;           // "MTDP"
    uint32_t version;         // 版本号
    uint64_t metadataOffset;  // 元数据偏移
    uint64_t metadataSize;    // 元数据大小
    uint64_t dataOffset;      // 压缩数据偏移
    uint64_t dataSize;        // 压缩数据大小
    
    DataPackageHeader()
        : magic(Constants::DATA_MAGIC_NUMBER),
          version(1),
          metadataOffset(0),
          metadataSize(0),
          dataOffset(0),
          dataSize(0) {}
};

} // namespace MultiThreadedInstaller
