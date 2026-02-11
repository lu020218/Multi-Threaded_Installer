#include "installer/tar_stream_extractor.h"
#include "installer/file_system_operator.h"
#include "common/utf8_utils.h"
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace MultiThreadedInstaller {

TarStreamExtractor::TarStreamExtractor(const std::string& targetRoot)
    : targetRoot_(targetRoot)
    , state_(State::ReadPathLength)
    , bufferOffset_(0)
    , pathLength_(0)
    , fileSize_(0)
    , remaining_(0) {}

bool TarStreamExtractor::write(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return true;
    }
    
    buffer_.insert(buffer_.end(), data, data + size);
    
    while (true) {
        size_t available = buffer_.size() - bufferOffset_;
        if (available == 0) {
            break;
        }
        
        switch (state_) {
            case State::ReadPathLength: {
                if (available < sizeof(uint32_t)) {
                    return true;
                }
                
                std::memcpy(&pathLength_, bufferData(), sizeof(uint32_t));
                consumeBytes(sizeof(uint32_t));
                state_ = State::ReadFileSize;
                break;
            }
            case State::ReadFileSize: {
                if (available < sizeof(uint32_t)) {
                    return true;
                }
                
                std::memcpy(&fileSize_, bufferData(), sizeof(uint32_t));
                remaining_ = fileSize_;
                consumeBytes(sizeof(uint32_t));
                state_ = State::ReadPath;
                break;
            }
            case State::ReadPath: {
                if (available < pathLength_) {
                    return true;
                }
                
                currentPath_.assign(reinterpret_cast<const char*>(bufferData()), pathLength_);
                consumeBytes(pathLength_);
                
                if (!openCurrentFile()) {
                    return false;
                }
                
                state_ = State::ReadFileContent;
                break;
            }
            case State::ReadFileContent: {
                if (remaining_ == 0) {
                    if (currentFile_.is_open()) {
                        currentFile_.close();
                    }
                    state_ = State::ReadPathLength;
                    break;
                }
                
                if (available == 0) {
                    return true;
                }
                
                size_t toWrite = std::min(static_cast<size_t>(remaining_), available);
                currentFile_.write(reinterpret_cast<const char*>(bufferData()), static_cast<std::streamsize>(toWrite));
                if (!currentFile_) {
                    return false;
                }
                
                consumeBytes(toWrite);
                remaining_ -= static_cast<uint32_t>(toWrite);
                
                if (remaining_ == 0) {
                    currentFile_.close();
                    state_ = State::ReadPathLength;
                }
                break;
            }
            default:
                return false;
        }
    }
    
    return true;
}

void TarStreamExtractor::flush() {
    if (currentFile_.is_open()) {
        currentFile_.flush();
    }
}

bool TarStreamExtractor::openCurrentFile() {
    std::filesystem::path root = PathFromUtf8(targetRoot_);
    std::filesystem::path rel = PathFromUtf8(currentPath_);
    std::filesystem::path fullPath = root / rel;
    
    FileSystemOperator fsOperator;
    std::filesystem::path parent = fullPath.parent_path();
    if (!parent.empty()) {
        if (!fsOperator.createDirectoryRecursive(Utf8FromPath(parent))) {
            return false;
        }
    }
    
    currentFile_.open(fullPath, std::ios::binary);
    return currentFile_.is_open();
}

bool TarStreamExtractor::consumeBytes(size_t count) {
    bufferOffset_ += count;
    if (bufferOffset_ > 0 && bufferOffset_ >= buffer_.size() / 2) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(bufferOffset_));
        bufferOffset_ = 0;
    }
    return true;
}

const uint8_t* TarStreamExtractor::bufferData() const {
    return buffer_.data() + bufferOffset_;
}

} // namespace MultiThreadedInstaller
