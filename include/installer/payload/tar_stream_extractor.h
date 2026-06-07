#pragma once

#include "common/archive_types.h"
#include "installer/payload/stream_sink.h"
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#else
#include <fstream>
#endif

namespace MultiThreadedInstaller {

// Returns true when the file already present at fullPath matches the given
// fingerprint and can be skipped. Uses the previous-install fingerprints for a
// zero-read decision (Scheme A) when the path is recorded there, otherwise
// reads and hashes the on-disk file (Scheme B). expectedHash == 0 always
// returns false (fail-safe rewrite). Shared by the streaming extractor and the
// per-file framed installer.
bool ExistingInstalledFileMatches(const std::filesystem::path& fullPath,
                                  uint64_t expectedSize,
                                  uint64_t expectedHash,
                                  const InstalledFileFingerprintMap* oldFingerprints);

class TarStreamExtractor : public StreamSink {
public:
    struct LastFailureInfo {
        bool hasFailure = false;
        uint32_t errorCode = 0;
        std::string stage;
        std::string path;
        std::string backupPath;
    };

    explicit TarStreamExtractor(const std::string& targetRoot);
    ~TarStreamExtractor();

    TarStreamExtractor(const TarStreamExtractor&) = delete;
    TarStreamExtractor& operator=(const TarStreamExtractor&) = delete;

    // Callback is intended to be installed for the lifetime of a single
    // decompression operation and should be cleared by the caller afterwards.
    void setCurrentFileChangedCallback(std::function<void(const std::string&)> callback);
    // Supplies per-file fingerprints (size + content hash) so unchanged files
    // already present on disk can be skipped instead of rewritten. Files whose
    // hash is 0 (no fingerprint) are never skipped.
    void setSkipFingerprints(const std::vector<FileIndexEntry>& fileIndex);
    // Supplies the previous install's per-file fingerprints (keyed by
    // normalizePathForCompare(absolute path)) for the zero-read skip path
    // (Scheme A): when the previously recorded hash equals the new package hash,
    // the file is skipped without reading its content from disk.
    void setOldInstalledFingerprints(
        std::shared_ptr<const InstalledFileFingerprintMap> oldInstalledFingerprints);
    bool write(const uint8_t* data, size_t size) override;
    void flush() override;
    const std::vector<std::string>& pendingReplaceFiles() const { return pendingReplaceFiles_; }
    const std::vector<std::string>& installedFiles() const { return installedFiles_; }
    const std::vector<std::string>& skippedFiles() const { return skippedFiles_; }
    const LastFailureInfo& lastFailureInfo() const { return lastFailureInfo_; }
    
private:
    enum class State {
        ReadPathLength,
        ReadFileSize,
        ReadPath,
        ReadFileContent
    };
    
    std::string targetRoot_;
    State state_;
    std::vector<uint8_t> buffer_;
    size_t bufferOffset_;
    
    uint32_t pathLength_;
    uint32_t fileSize_;
    uint32_t remaining_;
    std::string currentPath_;
#ifdef _WIN32
    HANDLE currentFileHandle_ = INVALID_HANDLE_VALUE;
#else
    std::ofstream currentFile_;
#endif
    std::filesystem::path currentFullPath_;
    std::filesystem::path currentBackupPath_;
    std::filesystem::path currentStagingPath_;
    bool currentFileRenamed_ = false;
    bool currentPendingRebootReplace_ = false;
    bool currentFileSkipped_ = false;
    std::function<void(const std::string&)> currentFileChangedCallback_;
    std::vector<std::string> pendingReplaceFiles_;
    std::vector<std::string> installedFiles_;
    std::vector<std::string> skippedFiles_;
    struct SkipFingerprint {
        uint64_t size = 0;
        uint64_t contentHash = 0;
    };
    std::unordered_map<std::string, SkipFingerprint> skipFingerprints_;
    std::shared_ptr<const InstalledFileFingerprintMap> oldInstalledFingerprints_;
    LastFailureInfo lastFailureInfo_;

    bool isFileOpen() const;
    void closeFile();
    // Decides whether the current file matches an existing on-disk file and can
    // be skipped (no rename/write/AV scan). On a hit it sets currentFullPath_.
    bool canSkipCurrentFile();
    void finalizeCurrentFileSkipped();
    bool writeToFile(const uint8_t* data, size_t size);
    void flushFile();

    bool validatePathLength(uint32_t pathLength) const;
    bool validateFileSize(uint32_t fileSize) const;
    bool validateCurrentPath(const std::filesystem::path& relativePath) const;
    bool openCurrentFile();
    bool finalizeCurrentFileSuccess();
    void finalizeCurrentFileFailure();
    void resetCurrentFileState();
    bool consumeBytes(size_t count);
    const uint8_t* bufferData() const;
};

} // namespace MultiThreadedInstaller
