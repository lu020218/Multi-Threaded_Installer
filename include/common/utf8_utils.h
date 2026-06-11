#pragma once

#include <filesystem>
#include <string>
#ifdef _WIN32
#include <tchar.h>
#endif

namespace MultiThreadedInstaller {

// ---------------------------------------------------------------------------
// 编码转换工具。项目内部一律以 UTF-8 std::string 流转，仅在调用 Win32 宽字符 API
// 或处理本地代码页数据时转换，避免中文路径/文案乱码。
// ---------------------------------------------------------------------------

/// UTF-8 → UTF-16（Windows 宽字符）。转换失败时返回空串而非抛异常。
std::wstring Utf8ToWide(const std::string& text);
/// UTF-16 → UTF-8。
std::string WideToUtf8(const std::wstring& text);

/// 指定代码页的多字节 → UTF-16（如 CP_ACP/CP_OEMCP）。
std::wstring MultiByteToWide(const std::string& text, unsigned int codePage, unsigned long flags = 0);
/// 同上，按显式长度（可含内嵌 NUL）。
std::wstring MultiByteToWide(const char* text, int length, unsigned int codePage, unsigned long flags = 0);
/// UTF-16 → 指定代码页多字节。
std::string WideToMultiByte(const std::wstring& text, unsigned int codePage, unsigned long flags = 0);
/// 同上，按显式长度。
std::string WideToMultiByte(const wchar_t* text, int length, unsigned int codePage, unsigned long flags = 0);
/// 本地 ANSI 代码页（CP_ACP）→ UTF-8。用于把系统返回的本地编码字符串归一到 UTF-8。
std::string AcpToUtf8(const std::string& text);

/// 去除字符串首尾的 ASCII 空白（空格/制表/换行等），返回拷贝。多处通用，统一在此提供。
std::string TrimAsciiCopy(const std::string& value);

/// 由 UTF-8 字符串构造文件系统路径（正确处理中文路径）。
std::filesystem::path PathFromUtf8(const std::string& text);
/// 把路径转回 UTF-8 字符串。
std::string Utf8FromPath(const std::filesystem::path& path);

#ifdef _WIN32
/// TCHAR* → 宽字符串（GUI/安装器共用，避免各处重复封装）。
std::wstring TCharToWide(const TCHAR* text);
/// TCHAR* → 文件系统路径。
std::filesystem::path PathFromTChar(const TCHAR* text);
#endif

} // namespace MultiThreadedInstaller
