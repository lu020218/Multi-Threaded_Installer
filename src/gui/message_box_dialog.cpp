#ifdef GUI_ENABLED

#include "../../include/gui/message_box_dialog.h"
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace DuiLib;

namespace MultiThreadedInstaller {

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
        int size = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (size > 0) {
            result.resize(size - 1);
            WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
        } else {
            result.clear();
        }
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
    bool useZip = CPaintManagerUI::GetResourceType() == UILIB_ZIP &&
                  !CPaintManagerUI::GetResourceZip().IsEmpty();
    return useZip ? _T("skins\\msgBox.xml") : _T("msgBox.xml");
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

#endif // GUI_ENABLED
