#include "common/logging/file_sink.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace logging {

FileSink::FileSink(const Config& config)
    : LogSink("FileSink")
    , config_(config)
    , currentFileSize_(0)
    , lastFlush_(std::chrono::steady_clock::now()) {
    
    if (!config_.isValid()) {
        throw std::invalid_argument("Invalid FileSink configuration");
    }
    
    if (!openFile()) {
        throw std::runtime_error("Failed to open log file: " + config_.filePath);
    }
}

FileSink::FileSink(const std::string& name, const Config& config)
    : LogSink(name)
    , config_(config)
    , currentFileSize_(0)
    , lastFlush_(std::chrono::steady_clock::now()) {
    
    if (!config_.isValid()) {
        throw std::invalid_argument("Invalid FileSink configuration");
    }
    
    if (!openFile()) {
        throw std::runtime_error("Failed to open log file: " + config_.filePath);
    }
}

FileSink::~FileSink() {
    std::lock_guard<std::mutex> lock(fileMutex_);
    closeFile();
}

void FileSink::write(const LogMessage& message) {
    // 检查是否应该记录此级别的日志
    if (!shouldLog(message.level)) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    // 检查是否需要轮转文件
    if (needsRotation()) {
        rotateFile();
    }
    
    // 确保文件已打开
    if (!fileStream_.is_open()) {
        if (!openFile()) {
            // 如果无法打开文件，静默失败（避免递归日志）
            return;
        }
    }
    
    // 格式化消息
    std::string formattedMessage = formatter_.formatForFile(message);
    
    // 写入消息
    fileStream_ << formattedMessage << std::endl;
    
    // 更新文件大小
    currentFileSize_ += formattedMessage.length() + 1; // +1 for newline
    
    // 检查是否需要刷新
    if (config_.autoFlush && needsFlush()) {
        fileStream_.flush();
        lastFlush_ = std::chrono::steady_clock::now();
    }
}

void FileSink::flush() {
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    if (fileStream_.is_open()) {
        fileStream_.flush();
        lastFlush_ = std::chrono::steady_clock::now();
    }
}

void FileSink::rotateFile() {
    // 注意：此函数假设已经持有fileMutex_锁
    
    // 关闭当前文件
    closeFile();
    
    // 轮转现有文件
    for (int i = config_.maxFiles - 1; i > 0; --i) {
        std::string oldFile = generateRotatedFileName(i - 1);
        std::string newFile = generateRotatedFileName(i);
        
        if (std::filesystem::exists(oldFile)) {
            std::error_code ec;
            std::filesystem::rename(oldFile, newFile, ec);
            // 忽略错误，继续处理
        }
    }
    
    // 将当前文件重命名为 .1
    if (std::filesystem::exists(config_.filePath)) {
        std::string rotatedFile = generateRotatedFileName(1);
        std::error_code ec;
        std::filesystem::rename(config_.filePath, rotatedFile, ec);
        // 忽略错误，继续处理
    }
    
    // 清理旧文件
    cleanupOldFiles();
    
    // 重新打开文件
    openFile();
}

bool FileSink::needsRotation() const {
    return currentFileSize_ >= config_.maxFileSize;
}

void FileSink::updateConfig(const Config& config) {
    if (!config.isValid()) {
        throw std::invalid_argument("Invalid FileSink configuration");
    }
    
    std::lock_guard<std::mutex> lock(fileMutex_);
    
    bool needReopen = (config.filePath != config_.filePath);
    config_ = config;
    
    if (needReopen) {
        closeFile();
        openFile();
    }
}

size_t FileSink::getCurrentFileSize() const {
    std::lock_guard<std::mutex> lock(fileMutex_);
    return currentFileSize_;
}

bool FileSink::isFileOpen() const {
    std::lock_guard<std::mutex> lock(fileMutex_);
    return fileStream_.is_open();
}

bool FileSink::openFile() {
    // 创建目录（如果需要）
    if (!createDirectoryIfNeeded(config_.filePath)) {
        return false;
    }
    
    // 打开文件（追加模式）
    fileStream_.open(config_.filePath, std::ios::out | std::ios::app);
    
    if (!fileStream_.is_open()) {
        return false;
    }
    
    // 获取当前文件大小
    currentFileSize_ = getFileSize(config_.filePath);
    
    return true;
}

void FileSink::closeFile() {
    if (fileStream_.is_open()) {
        fileStream_.flush();
        fileStream_.close();
    }
    currentFileSize_ = 0;
}

std::string FileSink::generateRotatedFileName(int index) {
    std::filesystem::path filePath(config_.filePath);
    std::string stem = filePath.stem().string();
    std::string extension = filePath.extension().string();
    std::filesystem::path directory = filePath.parent_path();
    
    std::ostringstream oss;
    oss << stem << "." << index << extension;
    
    return (directory / oss.str()).string();
}

void FileSink::cleanupOldFiles() {
    // 删除超过最大文件数量的旧文件
    for (int i = config_.maxFiles; i <= config_.maxFiles + 10; ++i) {
        std::string oldFile = generateRotatedFileName(i);
        if (std::filesystem::exists(oldFile)) {
            std::error_code ec;
            std::filesystem::remove(oldFile, ec);
            // 忽略错误
        } else {
            // 如果文件不存在，停止清理
            break;
        }
    }
}

bool FileSink::needsFlush() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFlush_);
    return elapsed >= config_.flushInterval;
}

bool FileSink::createDirectoryIfNeeded(const std::string& filePath) {
    std::filesystem::path path(filePath);
    std::filesystem::path directory = path.parent_path();
    
    if (directory.empty()) {
        return true; // 当前目录
    }
    
    std::error_code ec;
    if (!std::filesystem::exists(directory)) {
        return std::filesystem::create_directories(directory, ec);
    }
    
    return std::filesystem::is_directory(directory);
}

size_t FileSink::getFileSize(const std::string& filePath) const {
    std::error_code ec;
    auto size = std::filesystem::file_size(filePath, ec);
    return ec ? 0 : static_cast<size_t>(size);
}

} // namespace logging