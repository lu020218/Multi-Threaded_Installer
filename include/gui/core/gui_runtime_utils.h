#pragma once

#include <UIlib.h>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

UINT GetDpiForWindowSafe(HWND hwnd);
LPCTSTR WStringToTStr(const std::wstring& wstr);
std::wstring ToLowerString(const std::wstring& value);
int GetDefaultLanguageComboIndex();
std::wstring GetLanguageCodeForIndex(int index);
int GetLanguageIndexForCode(const std::wstring& code);
std::wstring GetLanguageFilePath(const std::wstring& code);

} // namespace MultiThreadedInstaller
