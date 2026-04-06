#pragma once

#include "installer/stream_sink.h"
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#else
#include <fstream>
#endif

namespace MultiThreadedInstaller {

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
    bool write(const uint8_t* data, size_t size) override;
    void flush() override;
    const std::vector<std::string>& pendingReplaceFiles() const { return pendingReplaceFiles_; }
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
    std::function<void(const std::string&)> currentFileChangedCallback_;
    std::vector<std::string> pendingReplaceFiles_;
    LastFailureInfo lastFailureInfo_;

    bool isFileOpen() const;
    void closeFile();
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
