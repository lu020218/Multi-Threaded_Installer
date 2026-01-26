#pragma once

#include "installer/stream_sink.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <fstream>

namespace MultiThreadedInstaller {

class TarStreamExtractor : public StreamSink {
public:
    explicit TarStreamExtractor(const std::string& targetRoot);
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
    
    bool openCurrentFile();
    bool consumeBytes(size_t count);
    const uint8_t* bufferData() const;
};

} // namespace MultiThreadedInstaller
