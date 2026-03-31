#include "installer/tar_stream_extractor.h"
#include "installer/file_system_operator.h"
#include "installer/installer_helpers.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <filesystem>
#include <algorithm>
#include <cwctype>
#include <exception>
#include <cstring>

namespace MultiThreadedInstaller {

namespace {

constexpr uint32_t kMaxTarPathLength = 32 * 1024;
constexpr uint32_t kMaxTarFileSize = 2u * 1024u * 1024u * 1024u;

} // namespace

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
                if (!validatePathLength(pathLength_)) {
                    logInstallerError("[DECOMP][PayloadWrite] invalid path length=" +
                                      std::to_string(pathLength_) +
                                      " targetRoot=" + targetRoot_);
                    return false;
                }
                consumeBytes(sizeof(uint32_t));
                state_ = State::ReadFileSize;
                break;
            }
            case State::ReadFileSize: {
                if (available < sizeof(uint32_t)) {
                    return true;
                }
                
                std::memcpy(&fileSize_, bufferData(), sizeof(uint32_t));
                if (!validateFileSize(fileSize_)) {
                    logInstallerError("[DECOMP][PayloadWrite] invalid file size=" +
                                      std::to_string(fileSize_) +
                                      " currentPathLength=" + std::to_string(pathLength_) +
                                      " targetRoot=" + targetRoot_);
                    return false;
                }
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
                    logInstallerError("[DECOMP][PayloadWrite] file write failed targetRoot=" + targetRoot_ +
                                      " relativePath=" + currentPath_ +
                                      " remaining=" + std::to_string(remaining_) +
                                      " writeSize=" + std::to_string(toWrite));
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
    try {
        std::filesystem::path root = PathFromUtf8(targetRoot_);
        std::filesystem::path rel = PathFromUtf8(currentPath_);
        if (!validateCurrentPath(rel)) {
            logInstallerError("[DECOMP][PayloadWrite] rejected unsafe relative path currentPath=" +
                              currentPath_ + " targetRoot=" + targetRoot_);
            return false;
        }
        std::filesystem::path fullPath = root / rel;

        FileSystemOperator fsOperator;
        std::filesystem::path parent = fullPath.parent_path();
        if (!parent.empty()) {
            if (!fsOperator.createDirectoryRecursive(Utf8FromPath(parent))) {
                logInstallerError("[DECOMP][PayloadWrite] failed to create parent directory path=" +
                                  Utf8FromPath(parent) + " currentPath=" + currentPath_);
                return false;
            }
        }

        currentFile_.open(toLongPath(fullPath), std::ios::binary);
        if (!currentFile_.is_open()) {
            logInstallerError("[DECOMP][PayloadWrite] failed to open file path=" +
                              Utf8FromPath(fullPath) + " currentPath=" + currentPath_);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        logInstallerError(std::string("[DECOMP][PayloadWrite] exception opening current file currentPath=") +
                          currentPath_ + " error=" + e.what());
        return false;
    } catch (...) {
        logInstallerError(std::string("[DECOMP][PayloadWrite] unknown exception opening current file currentPath=") +
                          currentPath_);
        return false;
    }
}

bool TarStreamExtractor::validatePathLength(uint32_t pathLength) const {
    return pathLength > 0 && pathLength <= kMaxTarPathLength;
}

bool TarStreamExtractor::validateFileSize(uint32_t fileSize) const {
    return fileSize <= kMaxTarFileSize;
}

bool TarStreamExtractor::validateCurrentPath(const std::filesystem::path& relativePath) const {
    if (relativePath.empty() || relativePath.is_absolute()) {
        return false;
    }

    const std::filesystem::path normalized = relativePath.lexically_normal();
    if (normalized.empty() || normalized == ".") {
        return false;
    }

#ifdef _WIN32
    const std::wstring native = normalized.native();
    if (native.size() >= 2 && std::iswalpha(native[0]) && native[1] == L':') {
        return false;
    }
#endif

    for (const auto& part : normalized) {
        if (part == "..") {
            return false;
        }
    }

    return true;
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
