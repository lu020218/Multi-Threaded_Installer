#ifdef GUI_ENABLED

#include "../../include/gui/license_dialog.h"
#include <fstream>
#include <sstream>

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
    // 创建模态对话框
    Create(hParent, _T("许可协议"), UI_WNDSTYLE_DIALOG, 0);
    CenterWindow();
    
    // 显示对话框并进入模态消息循环
    ShowWindow(true, true);
    
    // 进入模态消息循环
    MSG msg = { 0 };
    while (::GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) break;
        
        if (!CPaintManagerUI::TranslateMessage(&msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        
        // 如果窗口已关闭，退出循环
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
            // 关闭按钮等同于不同意
            OnDisagreeButtonClick();
        }
    }
    
    WindowImplBase::Notify(msg);
}

void LicenseDialog::InitWindow() {
    // 获取许可协议文本控件
    m_pLicenseText = static_cast<CRichEditUI*>(m_pm.FindControl(_T("license_text")));
    
    if (m_pLicenseText) {
        // 加载许可协议文本
        std::wstring licenseText = LoadLicenseText();
        m_pLicenseText->SetText(licenseText.c_str());
        
        // 设置为只读
        m_pLicenseText->SetReadOnly(true);
    }
}

std::wstring LicenseDialog::LoadLicenseText() {
    // 尝试从资源目录加载许可协议文本
    std::wstring licensePath = CPaintManagerUI::GetResourcePath() + _T("\\license.txt");
    
    std::wifstream file(licensePath);
    if (!file.is_open()) {
        // 如果文件不存在，返回默认文本
        return L"许可协议文本未找到。\n\n请确保 resources/license.txt 文件存在。";
    }
    
    std::wstringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    return buffer.str();
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
