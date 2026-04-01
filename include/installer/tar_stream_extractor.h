#pragma once

#include "installer/stream_sink.h"
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <fstream>

namespace MultiThreadedInstaller {

class TarStreamExtractor : public StreamSink {
public:
    explicit TarStreamExtractor(const std::string& targetRoot);
    // Callback is intended to be installed for the lifetime of a single
    // decompression operation and should be cleared by the caller afterwards.
    void setCurrentFileChangedCallback(std::function<void(const std::string&)> callback);
    bool write(const uint8_t* data, size_t size) override;
    void flush() override;
    
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
    std::ofstream currentFile_;
    std::function<void(const std::string&)> currentFileChangedCallback_;
    
    bool validatePathLength(uint32_t pathLength) const;
    bool validateFileSize(uint32_t fileSize) const;
    bool validateCurrentPath(const std::filesystem::path& relativePath) const;
    bool openCurrentFile();
    bool consumeBytes(size_t count);
    const uint8_t* bufferData() const;
};

} // namespace MultiThreadedInstaller
