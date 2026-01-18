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
    // 控件由DuiLib管理，不需要手动释放
}

void CompletionPageController::Initialize(CPaintManagerUI* pManager) {
    if (!pManager) {
        return;
    }
    
    // 获取控件指针
    m_pResultMessageLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("result_message")));
    m_pRunAppCheckbox = static_cast<CCheckBoxUI*>(pManager->FindControl(_T("run_app_checkbox")));
    m_pOpenWebCheckbox = static_cast<CCheckBoxUI*>(pManager->FindControl(_T("open_web_checkbox")));
    
    // 初始化时隐藏复选框（直到设置结果）
    UpdateCheckboxVisibility();
}

void CompletionPageController::SetInstallationResult(bool success, const std::wstring& message) {
    m_installSuccess = success;
    
    // 更新结果消息
    if (m_pResultMessageLabel) {
        CDuiString messageText(message.c_str());
        m_pResultMessageLabel->SetText(messageText);
        
        // 根据结果设置文本颜色
        if (success) {
            // 成功：绿色
            m_pResultMessageLabel->SetTextColor(0xFF4CAF50);
        } else {
            // 失败：红色
            m_pResultMessageLabel->SetTextColor(0xFFF44336);
        }
    }
    
    // 更新复选框可见性
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
    
    // 重置结果消息
    if (m_pResultMessageLabel) {
        m_pResultMessageLabel->SetText(_T(""));
        m_pResultMessageLabel->SetTextColor(0xFF333333); // 默认颜色
    }
    
    // 取消选中复选框
    if (m_pRunAppCheckbox) {
        m_pRunAppCheckbox->SetCheck(false);
    }
    
    if (m_pOpenWebCheckbox) {
        m_pOpenWebCheckbox->SetCheck(false);
    }
    
    // 隐藏复选框
    UpdateCheckboxVisibility();
}

void CompletionPageController::UpdateCheckboxVisibility() {
    // 只有在安装成功时才显示复选框
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
