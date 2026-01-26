#pragma once

#ifdef GUI_ENABLED

#include <UIlib.h>
#include <string>

using namespace DuiLib;

namespace MultiThreadedInstaller {

// 页面类型枚举
enum class PageType {
    Welcome = 0,
    License = 1,
    Progress = 2,
    Completion = 3
};

// Forward declarations
class InstallationWorker;

class PageController {
public:
    PageController(CTabLayoutUI* pTabLayout);
    ~PageController();
    
    // 导航到指定页面
    void NavigateToPage(PageType pageType);
    
    // 获取当前页面
    PageType GetCurrentPage() const;
    
    // 显示许可协议对话框
    bool ShowLicenseDialog(HWND hParent);
    
    // 启动安装过程
    void StartInstallation(const std::wstring& installPath,
                           bool autoRun,
                           bool desktopIcons,
                           const std::wstring& languageCode,
                           bool cleanupOldInstall,
                           HWND hNotifyWindow);
    
    // 处理安装完成
    void OnInstallationComplete(bool success, const std::wstring& errorMsg);
    
    // 处理安装进度更新
    void OnProgressUpdate(const std::wstring& currentFolder, float progress);
    
private:
    CTabLayoutUI* m_pTabLayout;
    PageType m_currentPage;
    InstallationWorker* m_pWorker;
    
    // 应用页面切换动画
    void ApplyTransitionAnimation();
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
