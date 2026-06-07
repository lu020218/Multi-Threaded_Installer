#pragma once

// Win32 句柄的 RAII 包装：消除手工 CloseHandle / RegCloseKey，避免错误分支漏关泄漏。
// 仅 Windows 下有定义；非 Windows 平台为空（项目当前阶段仅构建 Windows）。

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace MultiThreadedInstaller {

// RAII for HKEY（析构时 RegCloseKey）。move-only。
class UniqueHKey {
public:
    UniqueHKey() = default;
    explicit UniqueHKey(HKEY key) : key_(key) {}
    UniqueHKey(const UniqueHKey&) = delete;
    UniqueHKey& operator=(const UniqueHKey&) = delete;
    UniqueHKey(UniqueHKey&& other) noexcept : key_(other.release()) {}
    UniqueHKey& operator=(UniqueHKey&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    ~UniqueHKey() { reset(); }

    HKEY get() const { return key_; }
    explicit operator bool() const { return key_ != nullptr; }

    // 供 RegOpenKeyExW / RegCreateKeyExW 的 PHKEY 出参使用：先清空再交出地址。
    PHKEY receive() {
        reset();
        return &key_;
    }

    HKEY release() {
        HKEY k = key_;
        key_ = nullptr;
        return k;
    }

    void reset(HKEY key = nullptr) {
        if (key_ != nullptr && key_ != key) {
            RegCloseKey(key_);
        }
        key_ = key;
    }

private:
    HKEY key_ = nullptr;
};

// RAII for HANDLE（析构时 CloseHandle）。nullptr 与 INVALID_HANDLE_VALUE 均视为空。move-only。
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    ~UniqueHandle() { reset(); }

    HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    explicit operator bool() const { return valid(); }

    HANDLE release() {
        HANDLE h = handle_;
        handle_ = nullptr;
        return h;
    }

    void reset(HANDLE handle = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE && handle_ != handle) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

} // namespace MultiThreadedInstaller

#endif // _WIN32
