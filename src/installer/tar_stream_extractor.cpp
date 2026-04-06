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
#ifdef _WIN32
#include <Windows.h>
#endif

namespace MultiThreadedInstaller {

namespace {

constexpr uint32_t kMaxTarPathLength = 32 * 1024;
constexpr uint32_t kMaxTarFileSize = 2u * 1024u * 1024u * 1024u;

#ifdef _WIN32
constexpr int kOpenFileRetryCount = 8;
constexpr DWORD kOpenFileRetryDelayMs = 200;

std::string FormatWin32ErrorMessageLocal(DWORD errorCode) {
    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(flags,
                                     nullptr,
                                     errorCode,
                                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     reinterpret_cast<LPWSTR>(&buffer),
                                     0,
                                     nullptr);
    std::string message = "code=" + std::to_string(errorCode);
    if (len > 0 && buffer) {
        std::wstring text(buffer, len);
        while (!text.empty() &&
               (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
            text.pop_back();
        }
        if (!text.empty()) {
            message += " message=" + WideToUtf8(text);
        }
    }
    if (buffer) {
        LocalFree(buffer);
    }
    return message;
}

bool IsRetriableOpenError(DWORD errorCode) {
    return errorCode == ERROR_SHARING_VIOLATION ||
           errorCode == ERROR_LOCK_VIOLATION ||
           errorCode == ERROR_ACCESS_DENIED;
}

std::filesystem::path BuildBackupPath(const std::filesystem::path& fullPath) {
    return fullPath.native() + L".__mti_old";
}

std::filesystem::path BuildRebootReplaceStagingPath(const std::filesystem::path& fullPath) {
    return fullPath.native() + L".__mti_reboot_new";
}

bool IsSensitiveRebootReplacePath(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return extension == L".exe" || extension == L".dll" || extension == L".sys" ||
           extension == L".bat" || extension == L".cmd";
}

bool IsRebootReplaceEligibleError(DWORD errorCode) {
    return errorCode == ERROR_SHARING_VIOLATION ||
           errorCode == ERROR_LOCK_VIOLATION ||
           errorCode == ERROR_ACCESS_DENIED;
}

void ClearReadonlyAttributeIfNeeded(const std::filesystem::path& fullPath) {
    const std::filesystem::path longPath = toLongPath(fullPath);
    const DWORD attributes = GetFileAttributesW(longPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_READONLY) == 0) {
        return;
    }
    SetFileAttributesW(longPath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
}

bool RenameExistingFileWithRetry(const std::filesystem::path& fullPath,
                                 const std::filesystem::path& backupPath,
                                 DWORD& lastError) {
    const std::filesystem::path longFullPath = toLongPath(fullPath);
    const std::filesystem::path longBackupPath = toLongPath(backupPath);
    for (int attempt = 1; attempt <= kOpenFileRetryCount; ++attempt) {
        ClearReadonlyAttributeIfNeeded(fullPath);
        ClearReadonlyAttributeIfNeeded(backupPath);
        SetLastError(ERROR_SUCCESS);
        if (MoveFileExW(longFullPath.c_str(),
                        longBackupPath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            lastError = ERROR_SUCCESS;
            return true;
        }
        lastError = GetLastError();
        if (!IsRetriableOpenError(lastError) || attempt == kOpenFileRetryCount) {
            return false;
        }
        Sleep(kOpenFileRetryDelayMs);
    }
    return false;
}

bool DeletePathBestEffort(const std::filesystem::path& path,
                          DWORD& lastError) {
    const std::filesystem::path longPath = toLongPath(path);
    for (int attempt = 1; attempt <= kOpenFileRetryCount; ++attempt) {
        ClearReadonlyAttributeIfNeeded(path);
        std::error_code existsEc;
        if (!std::filesystem::exists(path, existsEc)) {
            lastError = ERROR_SUCCESS;
            return true;
        }
        SetLastError(ERROR_SUCCESS);
        if (DeleteFileW(longPath.c_str())) {
            lastError = ERROR_SUCCESS;
            return true;
        }
        lastError = GetLastError();
        if (!IsRetriableOpenError(lastError) || attempt == kOpenFileRetryCount) {
            return false;
        }
        Sleep(kOpenFileRetryDelayMs);
    }
    return false;
}

// Opens a file for writing using a single CreateFileW call and keeps the handle.
// This eliminates the gap between PrepareFileForOverwrite (close) and ofstream::open
// that allowed antivirus/indexer to lock the file.
HANDLE OpenOutputHandleWithRetry(const std::filesystem::path& fullPath,
                                 DWORD& lastError) {
    const std::filesystem::path longPath = toLongPath(fullPath);
    for (int attempt = 1; attempt <= kOpenFileRetryCount; ++attempt) {
        ClearReadonlyAttributeIfNeeded(fullPath);
        SetLastError(ERROR_SUCCESS);
        HANDLE handle = CreateFileW(longPath.c_str(),
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_DELETE,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                    nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            lastError = ERROR_SUCCESS;
            return handle;
        }
        lastError = GetLastError();
        if (!IsRetriableOpenError(lastError) || attempt == kOpenFileRetryCount) {
            return INVALID_HANDLE_VALUE;
        }
        Sleep(kOpenFileRetryDelayMs);
    }
    return INVALID_HANDLE_VALUE;
}

bool ScheduleDeleteOnReboot(const std::filesystem::path& path, DWORD& lastError) {
    SetLastError(ERROR_SUCCESS);
    if (MoveFileExW(toLongPath(path).c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
        lastError = ERROR_SUCCESS;
        return true;
    }
    lastError = GetLastError();
    return false;
}

bool ScheduleMoveOnReboot(const std::filesystem::path& source,
                          const std::filesystem::path& destination,
                          DWORD& lastError) {
    SetLastError(ERROR_SUCCESS);
    if (MoveFileExW(toLongPath(source).c_str(),
                    toLongPath(destination).c_str(),
                    MOVEFILE_DELAY_UNTIL_REBOOT)) {
        lastError = ERROR_SUCCESS;
        return true;
    }
    lastError = GetLastError();
    return false;
}
#endif

} // namespace

TarStreamExtractor::TarStreamExtractor(const std::string& targetRoot)
    : targetRoot_(targetRoot)
    , state_(State::ReadPathLength)
    , bufferOffset_(0)
    , pathLength_(0)
    , fileSize_(0)
    , remaining_(0)
    , currentFileRenamed_(false) {}

TarStreamExtractor::~TarStreamExtractor() {
    closeFile();
}

void TarStreamExtractor::setCurrentFileChangedCallback(
    std::function<void(const std::string&)> callback) {
    currentFileChangedCallback_ = std::move(callback);
}

// --- Platform-abstracted file I/O helpers ---

bool TarStreamExtractor::isFileOpen() const {
#ifdef _WIN32
    return currentFileHandle_ != INVALID_HANDLE_VALUE;
#else
    return currentFile_.is_open();
#endif
}

void TarStreamExtractor::closeFile() {
#ifdef _WIN32
    if (currentFileHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(currentFileHandle_);
        currentFileHandle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (currentFile_.is_open()) {
        currentFile_.close();
    }
#endif
}

bool TarStreamExtractor::writeToFile(const uint8_t* data, size_t size) {
#ifdef _WIN32
    const uint8_t* ptr = data;
    size_t remaining = size;
    while (remaining > 0) {
        // WriteFile takes a DWORD (max ~4GB), chunk to be safe.
        const DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<size_t>(0x7FFF0000u)));
        DWORD written = 0;
        if (!WriteFile(currentFileHandle_, ptr, chunk, &written, nullptr)) {
            return false;
        }
        if (written == 0) {
            return false;
        }
        ptr += written;
        remaining -= written;
    }
    return true;
#else
    currentFile_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(currentFile_);
#endif
}

void TarStreamExtractor::flushFile() {
#ifdef _WIN32
    if (currentFileHandle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(currentFileHandle_);
    }
#else
    if (currentFile_.is_open()) {
        currentFile_.flush();
    }
#endif
}

// --- Core streaming logic ---

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
                    closeFile();
                    if (!finalizeCurrentFileSuccess()) {
                        return false;
                    }
                    state_ = State::ReadPathLength;
                    break;
                }
                
                if (available == 0) {
                    return true;
                }
                
                size_t toWrite = (std::min)(static_cast<size_t>(remaining_), available);
                if (!writeToFile(bufferData(), toWrite)) {
                    finalizeCurrentFileFailure();
                    logInstallerError("[DECOMP][PayloadWrite] file write failed targetRoot=" + targetRoot_ +
                                      " relativePath=" + currentPath_ +
                                      " remaining=" + std::to_string(remaining_) +
                                      " writeSize=" + std::to_string(toWrite));
                    return false;
                }
                
                consumeBytes(toWrite);
                remaining_ -= static_cast<uint32_t>(toWrite);
                
                if (remaining_ == 0) {
                    closeFile();
                    if (!finalizeCurrentFileSuccess()) {
                        return false;
                    }
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
    flushFile();
}

bool TarStreamExtractor::openCurrentFile() {
    try {
        resetCurrentFileState();
        std::filesystem::path root = PathFromUtf8(targetRoot_);
        std::filesystem::path rel = PathFromUtf8(currentPath_);
        if (!validateCurrentPath(rel)) {
            logInstallerError("[DECOMP][PayloadWrite] rejected unsafe relative path currentPath=" +
                              currentPath_ + " targetRoot=" + targetRoot_);
            return false;
        }
        std::filesystem::path fullPath = root / rel;
        currentFullPath_ = fullPath;
        currentBackupPath_.clear();
        currentFileRenamed_ = false;

        FileSystemOperator fsOperator;
        std::filesystem::path parent = fullPath.parent_path();
        if (!parent.empty()) {
            if (!fsOperator.createDirectoryRecursive(Utf8FromPath(parent))) {
                logInstallerError("[DECOMP][PayloadWrite] failed to create parent directory path=" +
                                  Utf8FromPath(parent) + " currentPath=" + currentPath_);
                return false;
            }
        }

#ifdef _WIN32
        DWORD lastError = ERROR_SUCCESS;
        std::error_code existsEc;
        if (std::filesystem::exists(fullPath, existsEc)) {
            currentBackupPath_ = BuildBackupPath(fullPath);
            if (!RenameExistingFileWithRetry(fullPath, currentBackupPath_, lastError)) {
                lastFailureInfo_.hasFailure = true;
                lastFailureInfo_.errorCode = lastError;
                lastFailureInfo_.stage = "rename_old_failed";
                lastFailureInfo_.path = Utf8FromPath(fullPath);
                lastFailureInfo_.backupPath = Utf8FromPath(currentBackupPath_);
                if (IsSensitiveRebootReplacePath(fullPath) && IsRebootReplaceEligibleError(lastError)) {
                    currentPendingRebootReplace_ = true;
                    currentStagingPath_ = BuildRebootReplaceStagingPath(fullPath);
                    DWORD cleanupError = ERROR_SUCCESS;
                    DeletePathBestEffort(currentStagingPath_, cleanupError);
                    currentFileHandle_ = OpenOutputHandleWithRetry(currentStagingPath_, lastError);
                    if (currentFileHandle_ == INVALID_HANDLE_VALUE) {
                        logInstallerError("[DECOMP][PayloadWrite] rename_old_failed path=" +
                                          Utf8FromPath(fullPath) + " backupPath=" +
                                          Utf8FromPath(currentBackupPath_) + " currentPath=" + currentPath_ +
                                          " error=" + FormatWin32ErrorMessageLocal(lastError));
                        return false;
                    }
                    logInstallerWarning("[DECOMP][RebootReplace] staging_after_rename_conflict path=" +
                                        Utf8FromPath(fullPath) + " stagingPath=" +
                                        Utf8FromPath(currentStagingPath_) + " currentPath=" + currentPath_ +
                                        " error=" + FormatWin32ErrorMessageLocal(
                                                      static_cast<DWORD>(lastFailureInfo_.errorCode)));
                    if (currentFileChangedCallback_) {
                        currentFileChangedCallback_(currentPath_);
                    }
                    return true;
                }
                logInstallerError("[DECOMP][PayloadWrite] rename_old_failed path=" +
                                  Utf8FromPath(fullPath) + " backupPath=" +
                                  Utf8FromPath(currentBackupPath_) + " currentPath=" + currentPath_ +
                                  " error=" + FormatWin32ErrorMessageLocal(lastError));
                return false;
            }
            currentFileRenamed_ = true;
        }

        currentFileHandle_ = OpenOutputHandleWithRetry(fullPath, lastError);

        if (currentFileHandle_ == INVALID_HANDLE_VALUE) {
            lastFailureInfo_.hasFailure = true;
            lastFailureInfo_.errorCode = lastError;
            lastFailureInfo_.stage = "open_new_failed";
            lastFailureInfo_.path = Utf8FromPath(fullPath);
            lastFailureInfo_.backupPath = Utf8FromPath(currentBackupPath_);
            if (IsSensitiveRebootReplacePath(fullPath) && IsRebootReplaceEligibleError(lastError)) {
                currentPendingRebootReplace_ = true;
                currentStagingPath_ = BuildRebootReplaceStagingPath(fullPath);
                DWORD cleanupError = ERROR_SUCCESS;
                DeletePathBestEffort(currentStagingPath_, cleanupError);
                currentFileHandle_ = OpenOutputHandleWithRetry(currentStagingPath_, lastError);
                if (currentFileHandle_ != INVALID_HANDLE_VALUE) {
                    logInstallerWarning("[DECOMP][RebootReplace] staging_after_open_conflict path=" +
                                        Utf8FromPath(fullPath) + " stagingPath=" +
                                        Utf8FromPath(currentStagingPath_) + " currentPath=" + currentPath_ +
                                        " error=" + FormatWin32ErrorMessageLocal(
                                                      static_cast<DWORD>(lastFailureInfo_.errorCode)));
                    if (currentFileChangedCallback_) {
                        currentFileChangedCallback_(currentPath_);
                    }
                    return true;
                }
            }
            finalizeCurrentFileFailure();
            logInstallerError("[DECOMP][PayloadWrite] open_new_failed path=" +
                              Utf8FromPath(fullPath) + " currentPath=" + currentPath_ +
                              " error=" + FormatWin32ErrorMessageLocal(lastError));
            return false;
        }
#else
        currentFile_.open(toLongPath(fullPath), std::ios::binary | std::ios::out);
        if (!currentFile_.is_open()) {
            finalizeCurrentFileFailure();
            logInstallerError("[DECOMP][PayloadWrite] failed to open file path=" +
                              Utf8FromPath(fullPath) + " currentPath=" + currentPath_);
            return false;
        }
#endif
        if (currentFileChangedCallback_) {
            currentFileChangedCallback_(currentPath_);
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

bool TarStreamExtractor::finalizeCurrentFileSuccess() {
#ifdef _WIN32
    if (currentPendingRebootReplace_) {
        DWORD lastError = ERROR_SUCCESS;
        bool ok = true;
        if (currentFileRenamed_ && !currentBackupPath_.empty()) {
            if (!ScheduleDeleteOnReboot(currentBackupPath_, lastError)) {
                ok = false;
                logInstallerError("[DECOMP][RebootReplace] delete_backup_failed path=" +
                                  Utf8FromPath(currentBackupPath_) + " currentPath=" + currentPath_ +
                                  " error=" + FormatWin32ErrorMessageLocal(lastError));
            }
        } else if (!currentFullPath_.empty()) {
            if (!ScheduleDeleteOnReboot(currentFullPath_, lastError)) {
                ok = false;
                logInstallerError("[DECOMP][RebootReplace] delete_target_failed path=" +
                                  Utf8FromPath(currentFullPath_) + " currentPath=" + currentPath_ +
                                  " error=" + FormatWin32ErrorMessageLocal(lastError));
            }
        }

        if (ok &&
            !ScheduleMoveOnReboot(currentStagingPath_, currentFullPath_, lastError)) {
            ok = false;
            logInstallerError("[DECOMP][RebootReplace] schedule_move_failed path=" +
                              Utf8FromPath(currentFullPath_) + " stagingPath=" +
                              Utf8FromPath(currentStagingPath_) + " currentPath=" + currentPath_ +
                              " error=" + FormatWin32ErrorMessageLocal(lastError));
        }

        if (!ok) {
            finalizeCurrentFileFailure();
            return false;
        }

        pendingReplaceFiles_.push_back(Utf8FromPath(currentFullPath_));
        logInstallerWarning("[DECOMP][RebootReplace] registered path=" +
                            Utf8FromPath(currentFullPath_) + " stagingPath=" +
                            Utf8FromPath(currentStagingPath_) + " currentPath=" + currentPath_);
        resetCurrentFileState();
        return true;
    }

    if (currentFileRenamed_ && !currentBackupPath_.empty()) {
        DWORD lastError = ERROR_SUCCESS;
        if (!DeletePathBestEffort(currentBackupPath_, lastError)) {
            logInstallerWarning("[DECOMP][PayloadWrite] delete_backup_failed path=" +
                                Utf8FromPath(currentBackupPath_) + " currentPath=" + currentPath_ +
                                " error=" + FormatWin32ErrorMessageLocal(lastError));
        }
    }
#endif
    resetCurrentFileState();
    return true;
}

void TarStreamExtractor::finalizeCurrentFileFailure() {
#ifdef _WIN32
    const std::filesystem::path pathToDelete =
        currentPendingRebootReplace_ ? currentStagingPath_ : currentFullPath_;
    if (!isFileOpen() && !pathToDelete.empty()) {
        DWORD lastError = ERROR_SUCCESS;
        if (!DeletePathBestEffort(pathToDelete, lastError) && lastError != ERROR_SUCCESS) {
            logInstallerWarning("[DECOMP][PayloadWrite] cleanup_new_failed path=" +
                                Utf8FromPath(pathToDelete) + " currentPath=" + currentPath_ +
                                " error=" + FormatWin32ErrorMessageLocal(lastError));
        }
    } else if (isFileOpen()) {
        closeFile();
        DWORD lastError = ERROR_SUCCESS;
        if (!DeletePathBestEffort(pathToDelete, lastError) && lastError != ERROR_SUCCESS) {
            logInstallerWarning("[DECOMP][PayloadWrite] cleanup_new_failed path=" +
                                Utf8FromPath(pathToDelete) + " currentPath=" + currentPath_ +
                                " error=" + FormatWin32ErrorMessageLocal(lastError));
        }
    }

    if (currentFileRenamed_ && !currentBackupPath_.empty()) {
        logInstallerWarning("[DECOMP][PayloadWrite] backup_left_behind path=" +
                            Utf8FromPath(currentBackupPath_) + " currentPath=" + currentPath_);
    }
#endif
    resetCurrentFileState();
}

void TarStreamExtractor::resetCurrentFileState() {
    closeFile();
    currentFullPath_.clear();
    currentBackupPath_.clear();
    currentStagingPath_.clear();
    currentFileRenamed_ = false;
    currentPendingRebootReplace_ = false;
    lastFailureInfo_ = LastFailureInfo{};
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
