#pragma once

#include "common/config_types.h"

namespace MultiThreadedInstaller {

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

    CompressionResult()
        : checksum(0),
          originalSize(0),
          compressedSize(0),
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

    FolderMapping()
        : offset(0),
          compressedSize(0),
          originalSize(0),
          checksum(0),
          algorithm(CompressionAlgorithm::LZMA_HIGH) {}
};

struct ExtendedFolderMapping : public FolderMapping {
    SpecialDirectoryType targetDirType;
    std::string customTargetPath;
    bool appendDirectoryName;
    std::vector<FileIndexEntry> fileIndex;
    std::vector<BlockIndexEntry> blockIndex;

    ExtendedFolderMapping()
        : FolderMapping(),
          targetDirType(SpecialDirectoryType::INSTALL_DIRECTORY),
          appendDirectoryName(true) {}
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
    std::string appId;
    std::string directoryName;
    std::vector<std::string> legacyAppIds;
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
    std::vector<UiLinkBinding> uiLinks;
    std::vector<UninstallCleanupRule> uninstallCleanupRules;

    ExtendedInstallationMetadata()
        : InstallationMetadata(),
          applicationName("MyApplication"),
          appId(""),
          directoryName(""),
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

    BinaryMetadata()
        : magic(0x4D544950),
          version(1),
          folderCount(0),
          metadataSize(0),
          dataOffset(0) {}
};

struct DecompressionTask {
    std::vector<uint8_t> compressedData;
    std::string targetPath;
    uint32_t expectedChecksum;
    size_t originalSize;
    CompressionAlgorithm algorithm;

    DecompressionTask()
        : expectedChecksum(0),
          originalSize(0),
          algorithm(CompressionAlgorithm::LZMA_HIGH) {}
};

using ProgressCallback = std::function<void(const std::string&, const std::string&, float)>;

namespace Constants {
    constexpr uint32_t MAGIC_NUMBER = 0x4D544950;
    constexpr uint32_t DATA_MAGIC_NUMBER = 0x4D544450;
    constexpr uint32_t VERSION = 18;

    constexpr size_t DEFAULT_BLOCK_SIZE = 128 * 1024 * 1024;
    constexpr size_t MIN_BLOCK_SIZE = 4 * 1024 * 1024;
    constexpr size_t MAX_BLOCK_SIZE = 128 * 1024 * 1024;

    constexpr int DEFAULT_LZMA_LEVEL = 9;
    constexpr int DEFAULT_ZSTD_LEVEL = 3;
}

struct DataPackageHeader {
    uint32_t magic;
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
