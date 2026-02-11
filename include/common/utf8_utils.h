#pragma once

#include <filesystem>
#include <string>
#ifdef _WIN32
#include <tchar.h>
#endif

namespace MultiThreadedInstaller {

// UTF-8 <-> UTF-16 helpers (Windows) with safe fallbacks.
std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);

// Generic multibyte <-> UTF-16 helpers (Windows code pages).
std::wstring MultiByteToWide(const std::string& text, unsigned int codePage, unsigned long flags = 0);
std::wstring MultiByteToWide(const char* text, int length, unsigned int codePage, unsigned long flags = 0);
std::string WideToMultiByte(const std::wstring& text, unsigned int codePage, unsigned long flags = 0);
std::string WideToMultiByte(const wchar_t* text, int length, unsigned int codePage, unsigned long flags = 0);
std::string AcpToUtf8(const std::string& text);

// Path helpers for UTF-8 encoded strings.
std::filesystem::path PathFromUtf8(const std::string& text);
std::string Utf8FromPath(const std::filesystem::path& path);

#ifdef _WIN32
// TCHAR helpers shared by GUI/installer code to avoid duplicate wrappers.
std::wstring TCharToWide(const TCHAR* text);
std::filesystem::path PathFromTChar(const TCHAR* text);
#endif

} // namespace MultiThreadedInstaller
