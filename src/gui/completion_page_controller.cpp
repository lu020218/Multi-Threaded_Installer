#ifdef GUI_ENABLED

#include "../../include/gui/completion_page_controller.h"

namespace MultiThreadedInstaller {

CompletionPageController::CompletionPageController()
    : m_pResultMessageLabel(nullptr)
    , m_pRunAppCheckbox(nullptr)
    , m_pOpenWebCheckbox(nullptr)
    , m_installSuccess(false) {
}

CompletionPageController::~CompletionPageController() {

}

void CompletionPageController::Initialize(CPaintManagerUI* pManager) {
    if (!pManager) {
        return;
    }
    

    m_pResultMessageLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("result_message")));
    m_pRunAppCheckbox = static_cast<CCheckBoxUI*>(pManager->FindControl(_T("run_app_checkbox")));
    m_pOpenWebCheckbox = static_cast<CCheckBoxUI*>(pManager->FindControl(_T("open_web_checkbox")));
    

    UpdateCheckboxVisibility();
}

void CompletionPageController::SetInstallationResult(bool success, const std::wstring& message) {
    m_installSuccess = success;
    

    if (m_pResultMessageLabel) {
        CDuiString messageText(message.c_str());
        m_pResultMessageLabel->SetText(messageText);
        

        if (success) {

            m_pResultMessageLabel->SetTextColor(0xFF4CAF50);
        } else {

            m_pResultMessageLabel->SetTextColor(0xFFF44336);
        }
    }
    

    UpdateCheckboxVisibility();
}

bool CompletionPageController::ShouldRunApplication() const {
    if (!m_installSuccess || !m_pRunAppCheckbox) {
        return false;
    }
    
    return m_pRunAppCheckbox->GetCheck();
}

bool CompletionPageController::ShouldOpenWebPage() const {
    if (!m_installSuccess || !m_pOpenWebCheckbox) {
        return false;
    }
    
    return m_pOpenWebCheckbox->GetCheck();
}

void CompletionPageController::Reset() {
    m_installSuccess = false;
    

    if (m_pResultMessageLabel) {
        m_pResultMessageLabel->SetText(_T(""));
        m_pResultMessageLabel->SetTextColor(0xFF333333);
    }
    

    if (m_pRunAppCheckbox) {
        m_pRunAppCheckbox->SetCheck(false);
    }
    
    if (m_pOpenWebCheckbox) {
        m_pOpenWebCheckbox->SetCheck(false);
    }
    

    UpdateCheckboxVisibility();
}

void CompletionPageController::UpdateCheckboxVisibility() {

    bool visible = m_installSuccess;
    
    if (m_pRunAppCheckbox) {
        m_pRunAppCheckbox->SetVisible(visible);
    }
    
    if (m_pOpenWebCheckbox) {
        m_pOpenWebCheckbox->SetVisible(visible);
    }
}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
