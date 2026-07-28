#include "installer/payload/tar_stream_extractor.h"
#include "installer/platform/file_system_operator.h"
#include "installer/platform/installer_helpers.h"
#include "common/content_hash.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <exception>
#include <cstring>
#ifdef _WIN32
#include <Windows.h>
#include <RestartManager.h>
#endif

namespace MultiThreadedInstaller {

namespace {

constexpr uint32_t kMaxTarPathLength = 32 * 1024;
constexpr uint32_t kMaxTarFileSize = 2u * 1024u * 1024u * 1024u;

#ifdef _WIN32
constexpr int kOpenFileRetryCount = 8;
constexpr DWORD kInitialRetryDelayMs = 25;
constexpr DWORD kMaxRetryDelayMs = 400;

// Converged retry backoff: front-loaded fast retries clear transient AV/indexer
// locks quickly, then the delay grows and is capped so a lock that will not
// clear does not stall the install with a long fixed wait per attempt.
DWORD ComputeRetryDelayMs(int attempt) {
    const DWORD shift = static_cast<DWORD>(attempt > 0 ? attempt - 1 : 0);
    const DWORD delay = shift >= 31 ? kMaxRetryDelayMs : (kInitialRetryDelayMs << shift);
    return delay > kMaxRetryDelayMs ? kMaxRetryDelayMs : delay;
}

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

// Uses the Restart Manager API to identify which processes hold a lock on the
// given file.  Returns a human-readable summary suitable for log output.
// If the query fails or no locking processes are found, returns an empty string.
std::string QueryLockingProcesses(const std::filesystem::path& filePath) {
    DWORD session = 0;
    WCHAR sessionKey[CCH_RM_SESSION_KEY + 1] = {};
    if (RmStartSession(&session, 0, sessionKey) != ERROR_SUCCESS) {
        return {};
    }

    LPCWSTR pathStr = filePath.c_str();
    if (RmRegisterResources(session, 1, &pathStr, 0, nullptr, 0, nullptr) != ERROR_SUCCESS) {
        RmEndSession(session);
        return {};
    }

    UINT needed = 0;
    UINT count = 0;
    DWORD reason = 0;
    DWORD rc = RmGetList(session, &needed, &count, nullptr, &reason);
    if (rc != ERROR_MORE_DATA || needed == 0) {
        RmEndSession(session);
        return {};
    }

    std::vector<RM_PROCESS_INFO> procs(needed);
    count = needed;
    rc = RmGetList(session, &needed, &count, procs.data(), &reason);
    RmEndSession(session);

    if (rc != ERROR_SUCCESS || count == 0) {
        return {};
    }

    std::string result = "locking_processes=[";
    for (UINT i = 0; i < count; ++i) {
        if (i > 0) {
            result += ", ";
        }
        result += WideToUtf8(procs[i].strAppName);
        result += "(pid=" + std::to_string(procs[i].Process.dwProcessId) + ")";
    }
    result += "]";
    return result;
}

// Content for a changed file is written to this sibling staging file, then
// swapped into place atomically with ReplaceFileW (or MoveFileEx when the
// target does not yet exist). This avoids disturbing the target until the new
// content is fully written and verified by the OS write path.
std::filesystem::path BuildStagingPath(const std::filesystem::path& fullPath) {
    return fullPath.native() + L".__mti_new";
}

// Normalizes a relative path into a stable lookup key so the fingerprint built
// from the packaged file index and the path parsed from the tar stream compare
// equal regardless of separator or case differences.
std::string NormalizeRelKey(const std::string& relativePath) {
    std::string key = relativePath;
    for (char& ch : key) {
        if (ch == '/') {
            ch = '\\';
        } else {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    return key;
}

// Streams an existing file and returns true when its size and content hash both
// match the packaged fingerprint. Any read error fails closed (no skip).
bool ExistingFileMatchesFingerprint(const std::filesystem::path& fullPath,
                                    uint64_t expectedSize,
                                    uint64_t expectedHash) {
    std::error_code ec;
    const uint64_t actualSize = std::filesystem::file_size(fullPath, ec);
    if (ec || actualSize != expectedSize) {
        return false;
    }
    std::ifstream in(toLongPath(fullPath), std::ios::binary);
    if (!in) {
        return false;
    }
    ContentHasher hasher;
    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            hasher.update(buffer.data(), static_cast<size_t>(got));
        }
    }
    if (in.bad()) {
        return false;
    }
    return hasher.finalize() == expectedHash;
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
           errorCode == ERROR_ACCESS_DENIED ||
           errorCode == ERROR_USER_MAPPED_FILE;
}

void ClearReadonlyAttributeIfNeeded(const std::filesystem::path& fullPath) {
    const std::filesystem::path longPath = toLongPath(fullPath);
    const DWORD attributes = GetFileAttributesW(longPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_READONLY) == 0) {
        return;
    }
    SetFileAttributesW(longPath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
}

// Moves the staging file onto a destination that does not currently exist.
// Returns ERROR_SUCCESS on success, otherwise the last Win32 error.
DWORD MoveFileWithRetry(const std::filesystem::path& source,
                        const std::filesystem::path& destination) {
    const std::filesystem::path longSource = toLongPath(source);
    const std::filesystem::path longDestination = toLongPath(destination);
    DWORD lastError = ERROR_SUCCESS;
    for (int attempt = 1; attempt <= kOpenFileRetryCount; ++attempt) {
        ClearReadonlyAttributeIfNeeded(source);
        ClearReadonlyAttributeIfNeeded(destination);
        SetLastError(ERROR_SUCCESS);
        if (MoveFileExW(longSource.c_str(),
                        longDestination.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            return ERROR_SUCCESS;
        }
        lastError = GetLastError();
        if (!IsRetriableOpenError(lastError) || attempt == kOpenFileRetryCount) {
            std::string lockInfo = QueryLockingProcesses(destination);
            if (!lockInfo.empty()) {
                logInstallerWarning("[DECOMP][PayloadWrite] move_into_place_failed path=" +
                                    Utf8FromPath(destination) + " attempt=" + std::to_string(attempt) +
                                    " error=" + FormatWin32ErrorMessageLocal(lastError) +
                                    " " + lockInfo);
            }
            return lastError;
        }
        Sleep(ComputeRetryDelayMs(attempt));
    }
    return lastError;
}

// Atomically replaces an existing destination with the staging file, preserving
// the destination's attributes/ACLs. ReplaceFileW deletes the staging file on
// success. Returns ERROR_SUCCESS on success, otherwise the last Win32 error.
DWORD ReplaceFileWithRetry(const std::filesystem::path& destination,
                           const std::filesystem::path& staging) {
    const std::filesystem::path longDestination = toLongPath(destination);
    const std::filesystem::path longStaging = toLongPath(staging);
    DWORD lastError = ERROR_SUCCESS;
    for (int attempt = 1; attempt <= kOpenFileRetryCount; ++attempt) {
        ClearReadonlyAttributeIfNeeded(destination);
        ClearReadonlyAttributeIfNeeded(staging);
        SetLastError(ERROR_SUCCESS);
        if (ReplaceFileW(longDestination.c_str(),
                         longStaging.c_str(),
                         nullptr,
                         REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
                         nullptr,
                         nullptr)) {
            return ERROR_SUCCESS;
        }
        lastError = GetLastError();
        // ReplaceFileW can transiently fail to remove the replaced file or move
        // the replacement (e.g. AV/indexer touching the file); treat those as
        // retriable in addition to the usual sharing/lock violations.
        const bool retriable = IsRetriableOpenError(lastError) ||
                               lastError == ERROR_UNABLE_TO_REMOVE_REPLACED ||
                               lastError == ERROR_UNABLE_TO_MOVE_REPLACEMENT ||
                               lastError == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
        if (!retriable || attempt == kOpenFileRetryCount) {
            std::string lockInfo = QueryLockingProcesses(destination);
            if (!lockInfo.empty()) {
                logInstallerWarning("[DECOMP][PayloadWrite] replace_failed path=" +
                                    Utf8FromPath(destination) + " attempt=" + std::to_string(attempt) +
                                    " error=" + FormatWin32ErrorMessageLocal(lastError) +
                                    " " + lockInfo);
            }
            return lastError;
        }
        Sleep(ComputeRetryDelayMs(attempt));
    }
    return lastError;
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
        Sleep(ComputeRetryDelayMs(attempt));
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
            // On final failure, query which processes are locking the file.
            std::string lockInfo = QueryLockingProcesses(fullPath);
            if (!lockInfo.empty()) {
                logInstallerWarning("[DECOMP][PayloadWrite] open_handle_failed path=" +
                                    Utf8FromPath(fullPath) + " attempt=" + std::to_string(attempt) +
                                    " error=" + FormatWin32ErrorMessageLocal(lastError) +
                                    " " + lockInfo);
            }
            return INVALID_HANDLE_VALUE;
        }
        Sleep(ComputeRetryDelayMs(attempt));
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

void TarStreamExtractor::setSkipFingerprints(const std::vector<FileIndexEntry>& fileIndex) {
    skipFingerprints_.clear();
    skipFingerprints_.reserve(fileIndex.size());
    for (const auto& entry : fileIndex) {
        if (entry.relativePath.empty() || entry.contentHash == 0) {
            continue;
        }
        SkipFingerprint fingerprint;
        fingerprint.size = entry.size;
        fingerprint.contentHash = entry.contentHash;
        skipFingerprints_[NormalizeRelKey(entry.relativePath)] = fingerprint;
    }
}

void TarStreamExtractor::setOldInstalledFingerprints(
    std::shared_ptr<const InstalledFileFingerprintMap> oldInstalledFingerprints) {
    oldInstalledFingerprints_ = std::move(oldInstalledFingerprints);
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
            SetLastError(ERROR_WRITE_FAULT);
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
                
                if (memcpy_s(&pathLength_, sizeof(pathLength_), bufferData(), sizeof(uint32_t)) != 0) {
                    logInstallerError("[DECOMP][PayloadWrite] failed to read path length targetRoot=" + targetRoot_);
                    return false;
                }
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
                
                if (memcpy_s(&fileSize_, sizeof(fileSize_), bufferData(), sizeof(uint32_t)) != 0) {
                    logInstallerError("[DECOMP][PayloadWrite] failed to read file size targetRoot=" + targetRoot_);
                    return false;
                }
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

                resetCurrentFileState();
                if (canSkipCurrentFile()) {
                    currentFileSkipped_ = true;
                    if (currentFileChangedCallback_) {
                        currentFileChangedCallback_(currentPath_);
                    }
                } else if (!openCurrentFile()) {
                    return false;
                }

                state_ = State::ReadFileContent;
                break;
            }
            case State::ReadFileContent: {
                if (currentFileSkipped_) {
                    // The on-disk file already matches; consume (decompress and
                    // discard) the payload bytes without touching disk, since the
                    // XZ/ZSTD stream is sequential and cannot be seeked past.
                    if (remaining_ == 0) {
                        finalizeCurrentFileSkipped();
                        state_ = State::ReadPathLength;
                        break;
                    }
                    if (available == 0) {
                        return true;
                    }
                    const size_t toDiscard =
                        (std::min)(static_cast<size_t>(remaining_), available);
                    consumeBytes(toDiscard);
                    remaining_ -= static_cast<uint32_t>(toDiscard);
                    if (remaining_ == 0) {
                        finalizeCurrentFileSkipped();
                        state_ = State::ReadPathLength;
                    }
                    break;
                }

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
#ifdef _WIN32
                    const DWORD writeError = GetLastError();
#endif
                    finalizeCurrentFileFailure();
#ifdef _WIN32
                    logInstallerError("[DECOMP][PayloadWrite] file write failed targetRoot=" + targetRoot_ +
                                      " relativePath=" + currentPath_ +
                                      " remaining=" + std::to_string(remaining_) +
                                      " writeSize=" + std::to_string(toWrite) +
                                      " error=" + FormatWin32ErrorMessageLocal(writeError));
#else
                    logInstallerError("[DECOMP][PayloadWrite] file write failed targetRoot=" + targetRoot_ +
                                      " relativePath=" + currentPath_ +
                                      " remaining=" + std::to_string(remaining_) +
                                      " writeSize=" + std::to_string(toWrite));
#endif
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
        // Write the new content to a sibling staging file. The target is only
        // touched at finalize time, via an atomic ReplaceFileW swap (or a plain
        // move when the target does not exist yet). This keeps the existing file
        // intact until the new content is fully and successfully written.
        DWORD lastError = ERROR_SUCCESS;
        currentStagingPath_ = BuildStagingPath(fullPath);
        DWORD cleanupError = ERROR_SUCCESS;
        DeletePathBestEffort(currentStagingPath_, cleanupError);
        currentFileHandle_ = OpenOutputHandleWithRetry(currentStagingPath_, lastError);
        if (currentFileHandle_ == INVALID_HANDLE_VALUE) {
            lastFailureInfo_.hasFailure = true;
            lastFailureInfo_.errorCode = lastError;
            lastFailureInfo_.stage = "open_staging_failed";
            lastFailureInfo_.path = Utf8FromPath(fullPath);
            lastFailureInfo_.backupPath = Utf8FromPath(currentStagingPath_);
            finalizeCurrentFileFailure();
            logInstallerError("[DECOMP][PayloadWrite] open_staging_failed path=" +
                              Utf8FromPath(fullPath) + " stagingPath=" +
                              Utf8FromPath(currentStagingPath_) + " currentPath=" + currentPath_ +
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

bool ExistingInstalledFileMatches(const std::filesystem::path& fullPath,
                                  uint64_t expectedSize,
                                  uint64_t expectedHash,
                                  const InstalledFileFingerprintMap* oldFingerprints) {
    if (expectedHash == 0) {
        return false;
    }
    try {
        // Cheap metadata probe (no content read): the target must exist and its
        // size must match the packaged size for any skip to be possible.
        std::error_code sizeEc;
        const uint64_t actualSize = std::filesystem::file_size(fullPath, sizeEc);
        if (sizeEc || actualSize != expectedSize) {
            return false;
        }

        // Scheme A (zero-read): when the previous install recorded a fingerprint
        // for this exact target path, trust it. Equal hash => identical content
        // => skip without reading the file; unequal => rewrite without reading.
        if (oldFingerprints) {
            const auto oldIt = oldFingerprints->find(normalizePathForCompare(Utf8FromPath(fullPath)));
            if (oldIt != oldFingerprints->end()) {
                return oldIt->second.contentHash == expectedHash &&
                       oldIt->second.size == expectedSize;
            }
        }

        // Scheme B (fallback): no recorded hash for this path, so read the
        // on-disk content and compare. Trades read I/O for skipping write+AV.
        return ExistingFileMatchesFingerprint(fullPath, expectedSize, expectedHash);
    } catch (...) {
        return false;
    }
}

bool TarStreamExtractor::canSkipCurrentFile() {
    if (skipFingerprints_.empty()) {
        return false;
    }
    auto it = skipFingerprints_.find(NormalizeRelKey(currentPath_));
    if (it == skipFingerprints_.end()) {
        return false;
    }
    const SkipFingerprint& fingerprint = it->second;
    // Fail-safe: an absent fingerprint or a size mismatch means we cannot prove
    // the file is unchanged, so fall through to a normal rewrite.
    if (fingerprint.contentHash == 0 ||
        fingerprint.size != static_cast<uint64_t>(fileSize_)) {
        return false;
    }
    try {
        std::filesystem::path root = PathFromUtf8(targetRoot_);
        std::filesystem::path rel = PathFromUtf8(currentPath_);
        if (!validateCurrentPath(rel)) {
            return false;
        }
        std::filesystem::path fullPath = root / rel;
        if (ExistingInstalledFileMatches(fullPath,
                                         static_cast<uint64_t>(fileSize_),
                                         fingerprint.contentHash,
                                         oldInstalledFingerprints_.get())) {
            currentFullPath_ = fullPath;
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

void TarStreamExtractor::finalizeCurrentFileSkipped() {
    if (!currentFullPath_.empty()) {
        const std::string pathUtf8 = Utf8FromPath(currentFullPath_);
        installedFiles_.push_back(pathUtf8);
        skippedFiles_.push_back(pathUtf8);
    }
    resetCurrentFileState();
}

bool TarStreamExtractor::finalizeCurrentFileSuccess() {
#ifdef _WIN32
    // The staging handle is already closed by the caller. Swap the staging file
    // onto the target: ReplaceFileW when the target exists (atomic, preserves
    // attributes/ACLs), or a plain move for a brand-new file.
    std::error_code existsEc;
    const bool targetExists =
        std::filesystem::exists(currentFullPath_, existsEc) && !existsEc;
    DWORD swapError = targetExists
                          ? ReplaceFileWithRetry(currentFullPath_, currentStagingPath_)
                          : MoveFileWithRetry(currentStagingPath_, currentFullPath_);

    // ReplaceFileW occasionally fails with non-lock quirks (e.g. 1175
    // ERROR_UNABLE_TO_REMOVE_REPLACED). When the failure is not a true lock
    // (which would warrant the reboot path), fall back to an atomic MoveFileEx
    // replace, which sidesteps those quirks while staying a single rename.
    if (targetExists && swapError != ERROR_SUCCESS &&
        !IsRebootReplaceEligibleError(swapError)) {
        const DWORD moveError = MoveFileWithRetry(currentStagingPath_, currentFullPath_);
        if (moveError == ERROR_SUCCESS) {
            swapError = ERROR_SUCCESS;
        }
    }

    if (swapError != ERROR_SUCCESS) {
        lastFailureInfo_.hasFailure = true;
        lastFailureInfo_.errorCode = swapError;
        lastFailureInfo_.stage = targetExists ? "replace_failed" : "move_into_place_failed";
        lastFailureInfo_.path = Utf8FromPath(currentFullPath_);
        lastFailureInfo_.backupPath = Utf8FromPath(currentStagingPath_);

        // Locked sensitive binaries (e.g. a running .exe/.dll): defer the swap to
        // the next reboot. Keep the staging file so the scheduled move can find it.
        if (IsSensitiveRebootReplacePath(currentFullPath_) &&
            IsRebootReplaceEligibleError(swapError)) {
            DWORD rebootError = ERROR_SUCCESS;
            bool scheduled = true;
            if (targetExists && !ScheduleDeleteOnReboot(currentFullPath_, rebootError)) {
                scheduled = false;
                logInstallerError("[DECOMP][RebootReplace] delete_target_failed path=" +
                                  Utf8FromPath(currentFullPath_) + " currentPath=" + currentPath_ +
                                  " error=" + FormatWin32ErrorMessageLocal(rebootError));
            }
            if (scheduled &&
                !ScheduleMoveOnReboot(currentStagingPath_, currentFullPath_, rebootError)) {
                scheduled = false;
                logInstallerError("[DECOMP][RebootReplace] schedule_move_failed path=" +
                                  Utf8FromPath(currentFullPath_) + " stagingPath=" +
                                  Utf8FromPath(currentStagingPath_) + " currentPath=" + currentPath_ +
                                  " error=" + FormatWin32ErrorMessageLocal(rebootError));
            }
            if (scheduled) {
                pendingReplaceFiles_.push_back(Utf8FromPath(currentFullPath_));
                installedFiles_.push_back(Utf8FromPath(currentFullPath_));
                logInstallerWarning("[DECOMP][RebootReplace] registered path=" +
                                    Utf8FromPath(currentFullPath_) + " stagingPath=" +
                                    Utf8FromPath(currentStagingPath_) + " currentPath=" + currentPath_ +
                                    " error=" + FormatWin32ErrorMessageLocal(swapError));
                // Clear state without deleting the staging file (needed at reboot).
                currentFullPath_.clear();
                currentStagingPath_.clear();
                currentBackupPath_.clear();
                currentFileRenamed_ = false;
                currentPendingRebootReplace_ = false;
                currentFileSkipped_ = false;
                lastFailureInfo_ = LastFailureInfo{};
                return true;
            }
        }

        finalizeCurrentFileFailure();
        logInstallerError("[DECOMP][PayloadWrite] " +
                          std::string(targetExists ? "replace_failed" : "move_into_place_failed") +
                          " path=" + Utf8FromPath(currentFullPath_) + " stagingPath=" +
                          Utf8FromPath(currentStagingPath_) + " currentPath=" + currentPath_ +
                          " error=" + FormatWin32ErrorMessageLocal(swapError));
        return false;
    }
#endif
    if (!currentFullPath_.empty()) {
        installedFiles_.push_back(Utf8FromPath(currentFullPath_));
    }
    resetCurrentFileState();
    return true;
}

void TarStreamExtractor::finalizeCurrentFileFailure() {
#ifdef _WIN32
    // The target is never disturbed before the atomic swap, so failure cleanup
    // only needs to discard the staging file. The original file (if any) is
    // left intact.
    if (isFileOpen()) {
        closeFile();
    }
    if (!currentStagingPath_.empty()) {
        DWORD lastError = ERROR_SUCCESS;
        if (!DeletePathBestEffort(currentStagingPath_, lastError) && lastError != ERROR_SUCCESS) {
            logInstallerWarning("[DECOMP][PayloadWrite] cleanup_staging_failed path=" +
                                Utf8FromPath(currentStagingPath_) + " currentPath=" + currentPath_ +
                                " error=" + FormatWin32ErrorMessageLocal(lastError));
        }
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
    currentFileSkipped_ = false;
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
