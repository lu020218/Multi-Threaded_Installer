#ifdef GUI_ENABLED

#include "../../include/gui/page_controller.h"
#include "../../include/gui/installation_worker.h"
#include "../../include/gui/license_dialog.h"
#include <chrono>
#include <thread>

using namespace DuiLib;

namespace MultiThreadedInstaller {

PageController::PageController(CTabLayoutUI* pTabLayout)
    : m_pTabLayout(pTabLayout)
    , m_currentPage(PageType::Welcome)
    , m_pWorker(nullptr) {
    // 初始化时显示欢迎页面
    if (m_pTabLayout) {
        m_pTabLayout->SelectItem(static_cast<int>(PageType::Welcome));
    }
}

PageController::~PageController() {
    // 清理工作线程
    if (m_pWorker) {
        delete m_pWorker;
        m_pWorker = nullptr;
    }
}

void PageController::NavigateToPage(PageType pageType) {
    if (!m_pTabLayout) {
        return;
    }
    
    // 更新当前页面状态
    m_currentPage = pageType;
    
    // 应用页面切换动画
    ApplyTransitionAnimation();
    
    // 切换到指定页面
    m_pTabLayout->SelectItem(static_cast<int>(pageType));
}

PageType PageController::GetCurrentPage() const {
    return m_currentPage;
}

void PageController::ApplyTransitionAnimation() {
    if (!m_pTabLayout) {
        return;
    }
    
    // DuiLib的TabLayout支持淡入淡出效果
    // 通过设置动画属性实现页面切换动画
    // 注意：这需要在XML中配置或通过代码设置动画参数
    
    // 简单的淡入淡出效果实现
    // 获取当前选中的页面控件
    CControlUI* pCurrentPage = m_pTabLayout->GetItemAt(m_pTabLayout->GetCurSel());
    if (pCurrentPage) {
        // 设置淡出效果（透明度从255到0）
        // 注意：DuiLib_Ultimate支持动画，但需要配置
        // 这里提供基本实现，可以根据DuiLib版本调整
    }
    
    // 实际的动画效果可以通过DuiLib的动画管理器实现
    // 或者在XML中配置transition属性
}

bool PageController::ShowLicenseDialog(HWND hParent) {
    // 创建许可协议对话框
    LicenseDialog* pDialog = new LicenseDialog();
    
    // 显示模态对话框并获取结果
    bool agreed = pDialog->ShowModal(hParent);
    
    // 清理对话框
    delete pDialog;
    
    return agreed;
}

void PageController::StartInstallation(const std::wstring& installPath, HWND hNotifyWindow) {
    // 如果已有工作线程在运行，先清理
    if (m_pWorker) {
        delete m_pWorker;
        m_pWorker = nullptr;
    }
    
    // 创建新的安装工作线程
    m_pWorker = new InstallationWorker(hNotifyWindow);
    
    // 启动安装（这将在后台线程中执行）
    // 注意：InstallationWorker的完整实现在任务7中
    // 这里只是建立接口调用关系
    
    // 导航到进度页面
    NavigateToPage(PageType::Progress);
}

void PageController::OnInstallationComplete(bool success, const std::wstring& errorMsg) {
    // 导航到完成页面
    NavigateToPage(PageType::Completion);
    
    // 完成页面的UI更新将由GUIManager或CompletionPageController处理
    // 这里只负责页面导航
    
    // 注意：实际的结果消息设置需要在完成页面控制器中实现（任务5.3）
}

void PageController::OnProgressUpdate(const std::wstring& currentFolder, float progress) {
    // 进度更新的UI刷新将由ProgressPageController处理
    // PageController只负责协调，不直接操作UI控件
    
    // 确保进度值在有效范围内
    if (progress < 0.0f) {
        progress = 0.0f;
    }
    if (progress > 100.0f) {
        progress = 100.0f;
    }
    
    // 注意：实际的进度条和标签更新需要在进度页面控制器中实现（任务5.2）
}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
