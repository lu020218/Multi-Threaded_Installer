#include "packager/pe_resource_session.h"

#include "common/utf8_utils.h"

#include <Windows.h>

namespace MultiThreadedInstaller {
namespace {

constexpr int kMaxAttempts = 10;

// 前几次快重试清掉瞬时 AV/索引锁，随后退避并封顶，避免真锁时长时间空等。
DWORD RetryDelayMs(int attempt) {
    const unsigned shift = attempt > 0 ? static_cast<unsigned>(attempt - 1) : 0u;
    const DWORD delay = shift >= 5 ? 500u : (25u << shift);
    return delay > 500u ? 500u : delay;
}

bool IsRetriableError(DWORD code) {
    return code == ERROR_SHARING_VIOLATION ||   // 32
           code == ERROR_LOCK_VIOLATION ||      // 33
           code == ERROR_ACCESS_DENIED;         // 5
}

} // namespace

bool RunResourceUpdateSession(
    const std::wstring& exePath,
    const std::function<bool(void* update, std::string& error)>& applyFn,
    std::string& error) {
    error.clear();
    DWORD lastError = 0;

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        SetLastError(ERROR_SUCCESS);
        HANDLE update = BeginUpdateResourceW(exePath.c_str(), FALSE);  // FALSE: 保留已有资源
        if (!update) {
            lastError = GetLastError();
            if (IsRetriableError(lastError) && attempt < kMaxAttempts) {
                Sleep(RetryDelayMs(attempt));
                continue;
            }
            error = "BeginUpdateResource failed (code=" + std::to_string(lastError) +
                    "): " + WideToUtf8(exePath);
            return false;
        }

        std::string applyError;
        if (!applyFn(update, applyError)) {
            EndUpdateResource(update, TRUE);  // 丢弃
            error = applyError.empty() ? "resource apply failed" : applyError;
            return false;  // 应用失败非文件锁，不重试。
        }

        SetLastError(ERROR_SUCCESS);
        if (EndUpdateResource(update, FALSE)) {
            return true;  // 提交成功。
        }
        lastError = GetLastError();
        if (IsRetriableError(lastError) && attempt < kMaxAttempts) {
            Sleep(RetryDelayMs(attempt));
            continue;  // 重开整轮会话（EndUpdateResource 已消费句柄）。
        }
        error = "EndUpdateResource failed (code=" + std::to_string(lastError) +
                "): " + WideToUtf8(exePath);
        return false;
    }

    error = "resource update retries exhausted (code=" + std::to_string(lastError) + ")";
    return false;
}

} // namespace MultiThreadedInstaller
