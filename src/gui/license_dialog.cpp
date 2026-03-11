#include "../../include/gui/license_dialog.h"
#include "../../include/gui/gui_helpers.h"
#include "../../include/gui/license_text_loader.h"
#include "common/utf8_utils.h"
#ifdef _WIN32
#include <Windows.h>
#endif

using namespace DuiLib;

namespace MultiThreadedInstaller {
LicenseDialog::LicenseDialog()
    : m_agreed(false)
    , m_modalResult(false)
    , m_pLicenseText(nullptr) {
}

LicenseDialog::~LicenseDialog() {
}

bool LicenseDialog::ShowModal(HWND hParent) {
    // NOTE: Comment text normalized to avoid encoding mojibake.
    std::wstring title = GUIHelpers::GetLocalizedText(L"msg.license_title", L"");
    Create(hParent, title.c_str(), UI_WNDSTYLE_DIALOG, 0);
    CenterWindow();
    
    // NOTE: Comment text normalized to avoid encoding mojibake.
    ShowWindow(true, true);
    
    // NOTE: Comment text normalized to avoid encoding mojibake.
    MSG msg = { 0 };
    while (::GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) break;
        
        if (!CPaintManagerUI::TranslateMessage(&msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        
        // NOTE: Comment text normalized to avoid encoding mojibake.
        if (!::IsWindow(m_hWnd)) break;
    }
    
    return m_agreed;
}

CDuiString LicenseDialog::GetSkinFolder() {
    return _T("skins\\");
}

CDuiString LicenseDialog::GetSkinFile() {
    return _T("skins\\license.xml");
}

LPCTSTR LicenseDialog::GetWindowClassName() const {
    return _T("LicenseDialog");
}

void LicenseDialog::Notify(TNotifyUI& msg) {
    if (msg.sType == _T("click")) {
        CDuiString sName = msg.pSender->GetName();
        
        if (sName == _T("agree_button")) {
            OnAgreeButtonClick();
        }
        else if (sName == _T("disagree_button")) {
            OnDisagreeButtonClick();
        }
        else if (sName == _T("closebtn")) {
            // NOTE: Comment text normalized to avoid encoding mojibake.
            OnDisagreeButtonClick();
        }
    }
    
    WindowImplBase::Notify(msg);
}

void LicenseDialog::InitWindow() {
    // NOTE: Comment text normalized to avoid encoding mojibake.
    m_pLicenseText = static_cast<CRichEditUI*>(m_pm.FindControl(_T("license_text")));
    
    if (m_pLicenseText) {
        // NOTE: Comment text normalized to avoid encoding mojibake.
        std::wstring licenseText = LoadLicenseText();
        m_pLicenseText->SetText(licenseText.c_str());
        
        // NOTE: Comment text normalized to avoid encoding mojibake.
        m_pLicenseText->SetReadOnly(true);
    }
}

std::wstring LicenseDialog::LoadLicenseText() {
    return LoadLocalizedLicenseText(CResourceManager::GetInstance()->GetLanguage());
}

void LicenseDialog::OnAgreeButtonClick() {
    m_agreed = true;
    m_modalResult = true;
    Close();
}

void LicenseDialog::OnDisagreeButtonClick() {
    m_agreed = false;
    m_modalResult = false;
    Close();
}

} // namespace MultiThreadedInstaller

