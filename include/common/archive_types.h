#pragma once

#include "common/config_types.h"

namespace MultiThreadedInstaller {

struct FileIndexEntry {
    std::string relativePath;
    uint64_t offset;
    uint64_t size;
};

struct CompressionResult {
    // A single standard XZ/LZMA2 payload for one folder.
    std::vector<uint8_t> compressedData;
    uint32_t checksum;
    size_t originalSize;
    size_t compressedSize;
    CompressionAlgorithm algorithm;
    // File manifest for logging, validation, and post-install bookkeeping only.
    std::vector<FileIndexEntry> fileIndex;

    CompressionResult()
        : checksum(0),
          originalSize(0),
          compressedSize(0),
          algorithm(CompressionAlgorithm::LZMA2_XZ) {}
};

struct FolderMapping {
    std::string folderId;
    std::string folderName;
    std::string targetPath;
    // Payload location and size in the package data area.
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
          algorithm(CompressionAlgorithm::LZMA2_XZ) {}
};

struct ExtendedFolderMapping : public FolderMapping {
    SpecialDirectoryType targetDirType;
    std::string customTargetPath;
    bool appendDirectoryName;
    // File manifest for logging, validation, and post-install bookkeeping only.
    std::vector<FileIndexEntry> fileIndex;

    ExtendedFolderMapping()
        : FolderMapping(),
          targetDirType(SpecialDirectoryType::INSTALL_DIRECTORY),
          appendDirectoryName(true) {}
};

struct InstallationMetadata {
    uint32_t version;
    uint32_t folderCount;
    std::vector<FolderMapping> payloadMappings;
    uint64_t totalPayloadCompressedSize;

    InstallationMetadata() : version(1), folderCount(0), totalPayloadCompressedSize(0) {}
};

struct ExtendedInstallationMetadata : public InstallationMetadata {
    std::string appName;
    std::string appId;
    std::string appDirectoryName;
    std::vector<std::string> compatibilityLegacyAppIds;
    std::string desktopShortcutDefaultName;
    std::unordered_map<std::string, std::string> desktopShortcutLocalizedNames;
    std::vector<std::string> compatibilityLegacyDesktopShortcutNames;
    std::string appVersion;
    std::string installDefaultDir;
    std::string appWebsite;
    bool installAutoStartup;
    bool installDesktopIcon;
    bool installAutoCleanOldInstall;
    bool installRequireAdmin;
    uint16_t installMinWindowsMajor;
    uint16_t installMinWindowsMinor;
    uint32_t installMinWindowsBuild;
    uint64_t installSparseFileThresholdBytes;
    InstallStateConfig installStateConfig;
    std::vector<ExtendedFolderMapping> extendedPayloadMappings;
    std::vector<RegistryEntry> lifecycleInstallRegistry;
    std::vector<std::string> installKillProcesses;
    std::vector<ComponentConfig> layoutComponents;
    UiComponentSelectionConfig uiComponentSelection;
    std::vector<UiLinkBinding> uiLinkBindings;
    std::vector<UninstallCleanupRule> lifecycleUninstallCleanupRules;
    UpgradeCleanupConfig lifecycleUpgradeCleanup;

    ExtendedInstallationMetadata()
        : InstallationMetadata(),
          appName("MyApplication"),
          appId(""),
          appDirectoryName(""),
          desktopShortcutDefaultName(""),
          appVersion("1.0"),
          installDefaultDir("%ProgramFiles%"),
          appWebsite(""),
          installAutoStartup(false),
          installDesktopIcon(false),
          installAutoCleanOldInstall(false),
          installRequireAdmin(false),
          installMinWindowsMajor(0),
          installMinWindowsMinor(0),
          installMinWindowsBuild(0),
          installSparseFileThresholdBytes(4 * 1024 * 1024) {}
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
    std::string folderName;
    std::string targetPath;
    unsigned int schedulerConcurrencyHint;
    uint32_t expectedChecksum;
    size_t originalSize;
    CompressionAlgorithm algorithm;

    DecompressionTask()
        : schedulerConcurrencyHint(1),
          expectedChecksum(0),
          originalSize(0),
          algorithm(CompressionAlgorithm::LZMA2_XZ) {}
};

using ProgressCallback = std::function<void(const std::string&, const std::string&, float)>;

namespace Constants {
    constexpr uint32_t MAGIC_NUMBER = 0x4D544950;
    constexpr uint32_t DATA_MAGIC_NUMBER = 0x4D544450;
    constexpr uint32_t VERSION = 22;

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
