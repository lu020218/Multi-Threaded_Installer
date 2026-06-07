#include "gui/core/gui_runtime_utils.h"

#include "common/utf8_utils.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <vector>

namespace MultiThreadedInstaller {

using namespace DuiLib;

UINT GetDpiForWindowSafe(HWND hwnd) {
#ifdef _WIN32
    typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (fn && hwnd) {
            return fn(hwnd);
        }
        if (!hwnd) {
            auto getSystemDpi = reinterpret_cast<UINT(WINAPI*)(void)>(
                GetProcAddress(user32, "GetDpiForSystem"));
            if (getSystemDpi) {
                return getSystemDpi();
            }
        }
    }

    if (!hwnd) {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            typedef HRESULT(WINAPI* GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
            auto getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
                GetProcAddress(shcore, "GetDpiForMonitor"));
            if (getDpiForMonitor) {
                POINT pt = {0, 0};
                HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
                UINT dpiX = 96;
                UINT dpiY = 96;
                if (SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY))) {
                    FreeLibrary(shcore);
                    return dpiX ? dpiX : 96;
                }
            }
            FreeLibrary(shcore);
        }
    }
    HDC screen = GetDC(NULL);
    if (!screen) {
        return 96;
    }
    int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
#else
    (void)hwnd;
    return 96;
#endif
}

LPCTSTR WStringToTStr(const std::wstring& wstr) {
#ifdef UNICODE
    static thread_local std::vector<std::wstring> stringPool;
#else
    static thread_local std::vector<std::string> stringPool;
#endif
    static thread_local size_t poolIndex = 0;

    if (stringPool.size() < 10) {
        stringPool.resize(10);
    }

#ifdef UNICODE
    std::wstring& result = stringPool[poolIndex];
#else
    std::string& result = stringPool[poolIndex];
#endif
    poolIndex = (poolIndex + 1) % stringPool.size();

#ifdef UNICODE
    result = wstr;
    return result.c_str();
#else
    if (wstr.empty()) {
        result.clear();
    } else {
        result = WideToMultiByte(wstr, CP_ACP, 0);
    }
    return result.c_str();
#endif
}

std::wstring ToLowerString(const std::wstring& value) {
    std::wstring result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return result;
}

int GetDefaultLanguageComboIndex() {
#ifdef _WIN32
    LANGID langId = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(langId)) {
        case LANG_CHINESE:
            return 0;
        case LANG_ENGLISH:
            return 1;
        case LANG_JAPANESE:
            return 2;
        case LANG_KOREAN:
            return 3;
        case LANG_SPANISH:
            return 4;
        case LANG_FRENCH:
            return 5;
        default:
            return 1;
    }
#else
    return 1;
#endif
}

std::wstring GetLanguageCodeForIndex(int index) {
    switch (index) {
        case 0: return L"zh_CN";
        case 1: return L"en_US";
        case 2: return L"ja_JP";
        case 3: return L"ko_KR";
        case 4: return L"es_ES";
        case 5: return L"fr_FR";
        default: return L"en_US";
    }
}

int GetLanguageIndexForCode(const std::wstring& code) {
    std::wstring lower = ToLowerString(code);
    if (lower == L"zh_cn" || lower == L"zh-cn" || lower == L"zh") {
        return 0;
    }
    if (lower == L"en_us" || lower == L"en-us" || lower == L"en") {
        return 1;
    }
    if (lower == L"ja_jp" || lower == L"ja-jp" || lower == L"ja") {
        return 2;
    }
    if (lower == L"ko_kr" || lower == L"ko-kr" || lower == L"ko") {
        return 3;
    }
    if (lower == L"es_es" || lower == L"es-es" || lower == L"es") {
        return 4;
    }
    if (lower == L"fr_fr" || lower == L"fr-fr" || lower == L"fr") {
        return 5;
    }
    return 1;
}

std::wstring GetLanguageFilePath(const std::wstring& code) {
    CDuiString resourcePath = CPaintManagerUI::GetResourcePath();
    if (resourcePath.IsEmpty()) {
        return L"";
    }
    std::filesystem::path resPath = PathFromTChar(resourcePath.GetData());
    if (resPath.filename().empty()) {
        resPath = resPath.parent_path();
    }
    std::wstring tail = ToLowerString(resPath.filename().wstring());
    bool inSkins = (tail == L"skins");

    std::filesystem::path langPath;
    if (inSkins) {
        langPath = std::filesystem::path(L"..") / L"lang";
    } else {
        langPath = std::filesystem::path(L"lang");
    }
    langPath /= code + L".xml";
    return langPath.wstring();
}

} // namespace MultiThreadedInstaller
