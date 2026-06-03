#pragma once

#include "common/config_types.h"

#include <memory>
#include <unordered_map>

namespace MultiThreadedInstaller {

// Fingerprint of a previously installed file, recorded in install.manifest.json.
// Used by the incremental "zero-read" skip path (Scheme A): the installer can
// compare the new package's per-file hash against the previously installed
// hash without reading file content from disk.
struct InstalledFileFingerprint {
    uint64_t size = 0;
    uint64_t contentHash = 0;
};

// Keyed by normalizePathForCompare(absolute path).
using InstalledFileFingerprintMap = std::unordered_map<std::string, InstalledFileFingerprint>;

struct FileIndexEntry {
    std::string relativePath;
    uint64_t offset;
    uint64_t size;
    // Per-file content fingerprint (FNV-1a 64) used by incremental install to
    // skip rewriting unchanged files. 0 means "no fingerprint available"
    // (older packages), which forces a fail-safe rewrite.
    uint64_t contentHash = 0;
    // Per-file compressed frame location within the folder payload. Only
    // meaningful when the folder is framed (per-file compression); lets the
    // installer seek to and decompress just the changed files (P2).
    uint64_t frameOffset = 0;
    uint64_t frameCompressedSize = 0;
};

struct CompressionResult {
    // A single standard XZ/LZMA2 payload for one folder.
    std::vector<uint8_t> compressedData;
    uint32_t checksum;
    size_t originalSize;
    size_t compressedSize;
    CompressionAlgorithm algorithm;
    // When true, compressedData is a concatenation of independently compressed
    // per-file frames (see FileIndexEntry::frameOffset/frameCompressedSize)
    // rather than a single stream over the whole folder tar.
    bool framed = false;
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
    std::string target;
    // When true the payload is per-file framed (see CompressionResult::framed).
    bool framed = false;
    // File manifest for logging, validation, and post-install bookkeeping only.
    std::vector<FileIndexEntry> fileIndex;

    ExtendedFolderMapping()
        : FolderMapping() {}
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
    std::string appPublisher;
    std::string desktopShortcutDefaultName;
    std::unordered_map<std::string, std::string> desktopShortcutLocalizedNames;
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
    bool installUseMutex;
    std::string installMutexName;
    InstallInfoConfig installInfo;
    InstallStateConfig installState;
    SystemUninstallEntryConfig systemUninstallEntry;
    InstallerCleanupConfig installerCleanup;
    UninstallerCleanupConfigV3 uninstallerCleanup;
    std::vector<InstallerRegistryWriteGroup> installerRegistryWrite;
    std::vector<std::string> uninstallerKillBeforeUninstall;
    std::vector<ExtendedFolderMapping> extendedPayloadMappings;
    std::vector<RegistryEntry> lifecycleInstallRegistry;
    std::vector<std::string> installKillProcesses;
    std::vector<ComponentConfig> layoutComponents;
    UiComponentSelectionConfig uiComponentSelection;
    std::vector<UiLinkBinding> uiLinkBindings;
    UninstallCleanupConfig lifecycleUninstallCleanup;
    UpgradeCleanupConfig lifecycleUpgradeCleanup;
    std::string installStateCleanupMode;

    ExtendedInstallationMetadata()
        : InstallationMetadata(),
          appName("MyApplication"),
          appId(""),
          appDirectoryName(""),
          appPublisher(""),
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
          installUseMutex(true),
          installSparseFileThresholdBytes(4 * 1024 * 1024) {}
};

struct DecompressionTask {
    std::vector<uint8_t> compressedData;
    std::string folderName;
    std::string targetPath;
    unsigned int schedulerConcurrencyHint;
    uint32_t expectedChecksum;
    size_t originalSize;
    CompressionAlgorithm algorithm;
    // Per-file fingerprints for this folder. When populated, the extractor may
    // skip rewriting files whose on-disk content already matches.
    std::vector<FileIndexEntry> fileIndex;
    // Previously installed file fingerprints (from the old install manifest),
    // enabling the zero-read skip decision (Scheme A). May be null.
    std::shared_ptr<const InstalledFileFingerprintMap> oldInstalledFingerprints;

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
    constexpr uint32_t VERSION = 23;

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
