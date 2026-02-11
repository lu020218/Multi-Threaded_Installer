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

std::wstring MultiByteToWide(const char* text, int length, unsigned int codePage, unsigned long flags) {
    if (!text || length == 0) {
        return {};
    }
#ifdef _WIN32
    int size = MultiByteToWideChar(static_cast<UINT>(codePage), static_cast<DWORD>(flags),
                                   text, length, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(static_cast<UINT>(codePage), static_cast<DWORD>(flags),
                        text, length, out.data(), size);
    if (length == -1 && !out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
#else
    try {
        if (codePage == 65001) {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
            if (length < 0) {
                return conv.from_bytes(text);
            }
            return conv.from_bytes(text, text + length);
        }
    } catch (...) {
    }
    return {};
#endif
}

std::wstring MultiByteToWide(const std::string& text, unsigned int codePage, unsigned long flags) {
    if (text.empty()) {
        return {};
    }
    return MultiByteToWide(text.data(), static_cast<int>(text.size()), codePage, flags);
}

std::string WideToMultiByte(const wchar_t* text, int length, unsigned int codePage, unsigned long flags) {
    if (!text || length == 0) {
        return {};
    }
#ifdef _WIN32
    int size = WideCharToMultiByte(static_cast<UINT>(codePage), static_cast<DWORD>(flags),
                                   text, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(static_cast<UINT>(codePage), static_cast<DWORD>(flags),
                        text, length, out.data(), size, nullptr, nullptr);
    if (length == -1 && !out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
#else
    try {
        if (codePage == 65001) {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
            if (length < 0) {
                return conv.to_bytes(text);
            }
            return conv.to_bytes(text, text + length);
        }
    } catch (...) {
    }
    return {};
#endif
}

std::string WideToMultiByte(const std::wstring& text, unsigned int codePage, unsigned long flags) {
    if (text.empty()) {
        return {};
    }
    return WideToMultiByte(text.data(), static_cast<int>(text.size()), codePage, flags);
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
#ifdef _WIN32
    // Keep decoding strict to avoid machine-dependent ACP fallbacks.
    return MultiByteToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS);
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
#ifdef _WIN32
    return WideToMultiByte(text, CP_UTF8, 0);
#else
    if (text.empty()) {
        return {};
    }
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        return conv.to_bytes(text);
    } catch (...) {
        return {};
    }
#endif
}

std::string AcpToUtf8(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) {
        return {};
    }
    std::wstring wide = MultiByteToWide(text, CP_ACP, 0);
    if (wide.empty()) {
        return {};
    }
    return WideToUtf8(wide);
#else
    return text;
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

#ifdef _WIN32
std::wstring TCharToWide(const TCHAR* text) {
#ifdef UNICODE
    return text ? std::wstring(text) : std::wstring();
#else
    if (!text) {
        return {};
    }
    return MultiByteToWide(text, -1, CP_ACP, 0);
#endif
}

std::filesystem::path PathFromTChar(const TCHAR* text) {
    return std::filesystem::path(TCharToWide(text));
}
#endif
} // namespace MultiThreadedInstaller
