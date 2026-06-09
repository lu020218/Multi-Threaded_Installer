#pragma once

#include <UIlib.h>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

// GUI 运行时小工具：DPI 查询、宽窄字符串转换、语言下拉与语言文件路径映射。

/// 安全获取窗口 DPI（GetDpiForWindow 不可用时回退默认 96 等）。
UINT GetDpiForWindowSafe(HWND hwnd);
/// 宽字符串 → TCHAR*（指向内部缓冲，供 DuiLib 接口使用）。
LPCTSTR WStringToTStr(const std::wstring& wstr);
/// 宽字符串转小写。
std::wstring ToLowerString(const std::wstring& value);
/// 语言下拉框的默认选中下标（按系统/默认语言）。
int GetDefaultLanguageComboIndex();
/// 下拉下标 → 语言代码（如 "zh_CN"）。
std::wstring GetLanguageCodeForIndex(int index);
/// 语言代码 → 下拉下标。
int GetLanguageIndexForCode(const std::wstring& code);
/// 语言代码 → 资源内语言文件路径（如 lang/zh_CN.xml）。
std::wstring GetLanguageFilePath(const std::wstring& code);

} // namespace MultiThreadedInstaller
