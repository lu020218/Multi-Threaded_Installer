#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace MultiThreadedInstaller {


enum class CompressionAlgorithm {
    LZMA_HIGH,
    ZSTD
};


enum class SpecialDirectoryType {
    INSTALL_DIRECTORY,
    PROGRAM_FILES,      // %ProgramFiles%
    APPDATA_ROAMING,    // %AppData%
    APPDATA_LOCAL,      // %LocalAppData%
    PROGRAM_DATA        // %ProgramData%
};


enum class InstallStateMode {
    REGISTRY,
    FILE,
    BOTH
};


enum class RegistryValueType {
    STRING,
    DWORD,
    EXPAND_STRING
};


enum class ComponentSourceType : uint8_t {
    EMBEDDED = 0,
    LOCAL = 1,
    DOWNLOAD = 2
};


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


struct FolderInfo {
    std::string sourcePath;
    std::string targetPath;
    std::vector<std::string> files;
    size_t totalSize;
    
    FolderInfo() : totalSize(0) {}
    
    FolderInfo(const std::string& source, const std::string& target)
        : sourcePath(source), targetPath(target), totalSize(0) {}
};


struct FolderTargetConfig {
    std::string folderName;
    std::string targetDirectory;
    SpecialDirectoryType dirType;
    
    FolderTargetConfig()
        : dirType(SpecialDirectoryType::INSTALL_DIRECTORY) {}
};


struct RegistryEntry {
    std::string path;
    std::string key;
    std::string value;
    RegistryValueType type;
    
    RegistryEntry()
        : type(RegistryValueType::STRING) {}
};


struct LocalInstallerConfig {
    std::string base;
    std::string installer;
    std::string args;
    bool wait;
    uint32_t timeoutSec;
    std::string uninstall;

    LocalInstallerConfig()
        : wait(true), timeoutSec(900) {}
};


struct DownloadInstallerConfig {
    std::string url;
    std::string sha256;
    std::string saveAs;
    std::string args;
    bool wait;
    uint32_t timeoutSec;
    std::string uninstall;

    DownloadInstallerConfig()
        : wait(true), timeoutSec(1800) {}
};


struct ComponentSourceConfig {
    ComponentSourceType type;
    LocalInstallerConfig local;
    DownloadInstallerConfig download;

    ComponentSourceConfig()
        : type(ComponentSourceType::EMBEDDED) {}
};


struct ComponentConfig {
    std::string id;
    std::string name;
    std::string description;
    bool required;
    bool defaultSelected;
    uint32_t sizeHintMB;
    std::vector<std::string> dependsOn;
    std::vector<std::string> folders;
    ComponentSourceConfig source;
    std::vector<RegistryEntry> registry;
    std::vector<std::string> killProcesses;
    bool createDesktopShortcut;
    bool autoStartup;

    ComponentConfig()
        : required(false),
          defaultSelected(true),
          sizeHintMB(0),
          createDesktopShortcut(false),
          autoStartup(false) {}
};


struct UiComponentBindingPage {
    std::string skin;
    std::vector<std::string> controls;
};


struct UiComponentSelectionConfig {
    std::string mode;        // dedicatedPage | embeddedInExistingPages | hybrid
    std::string strategy;    // xml_userdata
    std::string tokenPrefix; // component:
    std::vector<UiComponentBindingPage> pages;

    UiComponentSelectionConfig()
        : mode("dedicatedPage"),
          strategy("xml_userdata"),
          tokenPrefix("component:") {}
};


struct PackagerConfiguration {
    std::string version;
    std::string applicationName;
    std::string defaultInstallDir;
    std::string iconPath;
    std::string webPageUrl;
    std::string productName;
    std::string fileVersion;
    std::string productVersion;
    std::string companyName;
    std::string fileDescription;
    std::string copyright;
    CompressionAlgorithm compressionAlgorithm;
    int compressionLevel;
    std::vector<FolderTargetConfig> folderTargets;
    std::vector<RegistryEntry> registry;
    std::vector<std::string> installKillProcesses;
    std::vector<ComponentConfig> components;
    UiComponentSelectionConfig componentUi;
    bool autoStartup;
    bool desktopIcons;
    bool autoCleanOldInstall;
    bool requireAdmin;
    uint16_t minWindowsMajor;
    uint16_t minWindowsMinor;
    uint32_t minWindowsBuild;
    uint64_t sparseFileThresholdBytes;
    InstallStateConfig installState;
    

    PackagerConfiguration() 
        : version("1.0"),
          applicationName("MyApplication"),
          defaultInstallDir("%ProgramFiles%"),
          compressionAlgorithm(CompressionAlgorithm::LZMA_HIGH),
          compressionLevel(-1),
          autoStartup(false),
          desktopIcons(false),
          autoCleanOldInstall(false),
          requireAdmin(false),
          minWindowsMajor(0),
          minWindowsMinor(0),
          minWindowsBuild(0),
          sparseFileThresholdBytes(4 * 1024 * 1024) {}
};


struct FileIndexEntry {
    std::string relativePath;
    uint64_t offset;
    uint64_t size;
};


struct BlockIndexEntry {
    uint32_t blockId;
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint32_t checksum;
};


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


struct ExtendedFolderMapping : public FolderMapping {
    SpecialDirectoryType targetDirType;
    std::string customTargetPath;
    std::vector<FileIndexEntry> fileIndex;
    std::vector<BlockIndexEntry> blockIndex;
    
    ExtendedFolderMapping() 
        : FolderMapping(),
          targetDirType(SpecialDirectoryType::INSTALL_DIRECTORY) {}
};


struct InstallationMetadata {
    uint32_t version;
    uint32_t folderCount;
    std::vector<FolderMapping> folderMappings;
    uint64_t totalCompressedSize;
    
    InstallationMetadata() : version(1), folderCount(0), totalCompressedSize(0) {}
};


struct ExtendedInstallationMetadata : public InstallationMetadata {
    std::string applicationName;
    std::string configVersion;
    std::string defaultInstallDir;
    std::string webPageUrl;
    bool autoStartup;
    bool desktopIcons;
    bool autoCleanOldInstall;
    bool requireAdmin;
    uint16_t minWindowsMajor;
    uint16_t minWindowsMinor;
    uint32_t minWindowsBuild;
    uint64_t sparseFileThresholdBytes;
    InstallStateConfig installState;
    std::vector<ExtendedFolderMapping> extendedMappings;
    std::vector<RegistryEntry> registry;
    std::vector<std::string> installKillProcesses;
    std::vector<ComponentConfig> components;
    UiComponentSelectionConfig componentUi;
    
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


struct BinaryMetadata {
    uint32_t magic;
    uint32_t version;
    uint32_t folderCount;
    uint64_t metadataSize;
    uint64_t dataOffset;
    
    BinaryMetadata() : magic(0x4D544950), version(1), folderCount(0), 
                      metadataSize(0), dataOffset(0) {}
};


struct DecompressionTask {
    std::vector<uint8_t> compressedData;
    std::string targetPath;
    uint32_t expectedChecksum;
    size_t originalSize;
    CompressionAlgorithm algorithm;
    
    DecompressionTask() : expectedChecksum(0), originalSize(0),
                         algorithm(CompressionAlgorithm::LZMA_HIGH) {}
};


using ProgressCallback = std::function<void(const std::string&, const std::string&, float)>;


namespace Constants {
    constexpr uint32_t MAGIC_NUMBER = 0x4D544950;  // "MTIP"
    constexpr uint32_t DATA_MAGIC_NUMBER = 0x4D544450;  // "MTDP"
    constexpr uint32_t VERSION = 13;
    

    constexpr size_t DEFAULT_BLOCK_SIZE = 128 * 1024 * 1024;
    constexpr size_t MIN_BLOCK_SIZE = 4 * 1024 * 1024;      // 1MB
    constexpr size_t MAX_BLOCK_SIZE = 128 * 1024 * 1024;      // 8MB
    
    constexpr int DEFAULT_LZMA_LEVEL = 9;
    constexpr int DEFAULT_ZSTD_LEVEL = 3;
}


struct DataPackageHeader {
    uint32_t magic;           // "MTDP"
    uint32_t version;
    uint64_t metadataOffset;
    uint64_t metadataSize;
    uint64_t dataOffset;
    uint64_t dataSize;
    
    DataPackageHeader()
        : magic(Constants::DATA_MAGIC_NUMBER),
          version(1),
          metadataOffset(0),
          metadataSize(0),
          dataOffset(0),
          dataSize(0) {}
};

} // namespace MultiThreadedInstaller
