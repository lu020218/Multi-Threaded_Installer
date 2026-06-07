#include "gui/dialogs/message_box_dialog.h"
#include "common/utf8_utils.h"
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace DuiLib;

namespace MultiThreadedInstaller {

static UINT GetDpiForWindowSafe(HWND hwnd) {
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
    return 96;
#endif
}

static LPCTSTR WStringToTStr(const std::wstring& wstr) {
#ifdef UNICODE
    return wstr.c_str();
#else
    static thread_local std::vector<std::string> stringPool;
    static thread_local size_t poolIndex = 0;

    if (stringPool.size() < 4) {
        stringPool.resize(4);
    }

    std::string& result = stringPool[poolIndex];
    poolIndex = (poolIndex + 1) % stringPool.size();

    if (wstr.empty()) {
        result.clear();
    } else {
        result = WideToMultiByte(wstr, CP_ACP, 0);
    }
    return result.c_str();
#endif
}

MessageBoxDialog::MessageBoxDialog(const std::wstring& title,
                                   const std::wstring& message,
                                   const std::wstring& okText,
                                   const std::wstring& cancelText,
                                   const std::wstring& altText)
    : m_title(title)
    , m_message(message)
    , m_okText(okText)
    , m_cancelText(cancelText)
    , m_altText(altText)
    , m_result(DialogResult::Cancel)
    , m_closeMeansOk(false)
    , m_pTitle(nullptr)
    , m_pMessage(nullptr)
    , m_pOk(nullptr)
    , m_pCancel(nullptr)
    , m_pAlt(nullptr) {
}

DialogResult MessageBoxDialog::ShowModal(HWND hParent) {
    m_pm.GetDPIObj()->SetScale(static_cast<int>(GetDpiForWindowSafe(hParent)));
    Create(hParent, _T("MessageBox"), UI_WNDSTYLE_DIALOG, 0);
    CenterWindow();
    ShowWindow(true, true);

    MSG msg = { 0 };
    while (::GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) break;

        if (!CPaintManagerUI::TranslateMessage(&msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }

        if (!::IsWindow(m_hWnd)) break;
    }

    return m_result;
}

CDuiString MessageBoxDialog::GetSkinFile() {
    return _T("skins\\msgBox.xml");
}

LPCTSTR MessageBoxDialog::GetWindowClassName() const {
    return _T("MessageBoxDialog");
}

void MessageBoxDialog::Notify(TNotifyUI& msg) {
    if (msg.sType == _T("click")) {
        CDuiString name = msg.pSender->GetName();
        if (name == _T("btnOK")) {
            m_result = DialogResult::Ok;
            Close();
            return;
        }
        if (name == _T("btnCancel")) {
            m_result = DialogResult::Cancel;
            Close();
            return;
        }
        if (name == _T("btnAlt")) {
            m_result = DialogResult::Alt;
            Close();
            return;
        }
        if (name == _T("btnClose")) {
            m_result = m_closeMeansOk ? DialogResult::Ok : DialogResult::Cancel;
            Close();
            return;
        }
    }

    WindowImplBase::Notify(msg);
}

void MessageBoxDialog::InitWindow() {
    int windowDpi = static_cast<int>(GetDpiForWindowSafe(m_hWnd));
    if (windowDpi > 0 && windowDpi != m_pm.GetDPIObj()->GetScale()) {
        m_pm.SetDPI(windowDpi);
    }

    m_pTitle = static_cast<CLabelUI*>(m_pm.FindControl(_T("lblTitle")));
    m_pMessage = static_cast<CRichEditUI*>(m_pm.FindControl(_T("lblMsg")));
    m_pOk = static_cast<CButtonUI*>(m_pm.FindControl(_T("btnOK")));
    m_pCancel = static_cast<CButtonUI*>(m_pm.FindControl(_T("btnCancel")));
    m_pAlt = static_cast<CButtonUI*>(m_pm.FindControl(_T("btnAlt")));

    if (m_pTitle && !m_title.empty()) {
        m_pTitle->SetText(WStringToTStr(m_title));
    }

    if (m_pMessage && !m_message.empty()) {
        m_pMessage->SetText(WStringToTStr(m_message));
    }

    if (m_pOk && !m_okText.empty()) {
        m_pOk->SetText(WStringToTStr(m_okText));
    }

    bool showCancel = !m_cancelText.empty();
    if (m_pCancel) {
        m_pCancel->SetVisible(showCancel);
        if (showCancel) {
            m_pCancel->SetText(WStringToTStr(m_cancelText));
        }
    }

    bool showAlt = !m_altText.empty();
    if (m_pAlt) {
        m_pAlt->SetVisible(showAlt);
        if (showAlt) {
            m_pAlt->SetText(WStringToTStr(m_altText));
        }
    }

    m_closeMeansOk = !showCancel && !showAlt;
}

} // namespace MultiThreadedInstaller



