#include "common/utf8_utils.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <codecvt>
#include <locale>

namespace MultiThreadedInstaller {

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
#ifdef _WIN32
    bool usedAcp = false;
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   text.data(), static_cast<int>(text.size()),
                                   nullptr, 0);
    if (size <= 0) {
        // Fallback to ACP to be resilient for non-UTF8 console input.
        usedAcp = true;
        size = MultiByteToWideChar(CP_ACP, 0,
                                   text.data(), static_cast<int>(text.size()),
                                   nullptr, 0);
    }
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    UINT codePage = usedAcp ? CP_ACP : CP_UTF8;
    DWORD flags = usedAcp ? 0 : MB_ERR_INVALID_CHARS;
    MultiByteToWideChar(codePage, flags,
                        text.data(), static_cast<int>(text.size()),
                        out.data(), size);
    return out;
#else
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        return conv.from_bytes(text);
    } catch (...) {
        return {};
    }
#endif
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
#ifdef _WIN32
    int size = WideCharToMultiByte(CP_UTF8, 0,
                                   text.data(), static_cast<int>(text.size()),
                                   nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                        text.data(), static_cast<int>(text.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
#else
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        return conv.to_bytes(text);
    } catch (...) {
        return {};
    }
#endif
}

std::filesystem::path PathFromUtf8(const std::string& text) {
#ifdef _WIN32
    return std::filesystem::path(Utf8ToWide(text));
#else
    return std::filesystem::path(text);
#endif
}

std::string Utf8FromPath(const std::filesystem::path& path) {
#ifdef _WIN32
    return WideToUtf8(path.native());
#else
    return path.u8string();
#endif
}

} // namespace MultiThreadedInstaller
