#pragma once

#include <string>
#include <filesystem>

namespace MultiThreadedInstaller {

// UTF-8 <-> UTF-16 helpers (Windows) with safe fallbacks.
std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);

// Path helpers for UTF-8 encoded strings.
std::filesystem::path PathFromUtf8(const std::string& text);
std::string Utf8FromPath(const std::filesystem::path& path);

} // namespace MultiThreadedInstaller
