#ifdef GUI_ENABLED

#include "../../include/gui/license_dialog.h"
#include "../../include/gui/gui_helpers.h"
#include "common/utf8_utils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif
#include "Utils/unzip.h"

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
    if (CPaintManagerUI::GetResourceType() == UILIB_ZIP &&
        !CPaintManagerUI::GetResourceZip().IsEmpty()) {
        CDuiString basePath = CPaintManagerUI::GetResourcePath();
        CDuiString zipName = CPaintManagerUI::GetResourceZip();
        CDuiString zipPath = basePath + zipName;

        HZIP hz = OpenZip(zipPath.GetData(), 0);
        if (hz != NULL) {
            ZIPENTRY ze;
            int index = 0;
            if (FindZipItem(hz, _T("license.txt"), true, &index, &ze) == 0) {
                std::vector<char> buffer(static_cast<size_t>(ze.unc_size));
                if (UnzipItem(hz, index, buffer.data(), ze.unc_size) == 0) {
                    CloseZip(hz);
                    std::string text(buffer.begin(), buffer.end());
                    return Utf8ToWide(text);
                }
            }
            CloseZip(hz);
        }
    }

    // NOTE: Comment text normalized to avoid encoding mojibake.
    CDuiString resourcePath = CPaintManagerUI::GetResourcePath();
    std::filesystem::path licensePath = PathFromTChar(resourcePath.GetData()) / "license.txt";

    std::ifstream file(licensePath, std::ios::binary);
    if (!file.is_open()) {
        std::filesystem::path fallback = PathFromTChar(resourcePath.GetData()) / ".." / "license.txt";
        file.open(fallback, std::ios::binary);
    }
    if (!file.is_open()) {
        // NOTE: Comment text normalized to avoid encoding mojibake.
        return GUIHelpers::GetLocalizedText(L"msg.license.text_missing", L"");
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return Utf8ToWide(content);
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

#endif // GUI_ENABLED
