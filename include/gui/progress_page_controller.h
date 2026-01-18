#pragma once

#ifdef GUI_ENABLED

#include <UIlib.h>
#include <string>
#include <chrono>

using namespace DuiLib;

namespace MultiThreadedInstaller {

/**
 * ProgressPageController类 - 进度页面控制器
 * 管理安装进度页面的业务逻辑，包括进度条更新、时间估算和状态显示
 */
class ProgressPageController {
public:
    ProgressPageController();
    ~ProgressPageController();
    
    // 初始化控件指针
    void Initialize(CPaintManagerUI* pManager);
    
    // 更新进度（文件夹名称和百分比）
    void UpdateProgress(const std::wstring& folder, float percentage);
    
    // 计算预计剩余时间
    std::wstring CalculateEstimatedTime(float currentProgress);
    
    // 记录安装开始时间
    void StartInstallation();
    
    // 重置进度状态
    void Reset();
    
private:
    // 控件指针
    CLabelUI* m_pCurrentFolderLabel;
    CProgressUI* m_pProgressBar;
    CLabelUI* m_pProgressPercentLabel;
    CLabelUI* m_pEstimatedTimeLabel;
    
    // 时间跟踪
    std::chrono::steady_clock::time_point m_startTime;
    bool m_installationStarted;
    
    // 辅助函数
    std::wstring FormatTime(int seconds);
    std::wstring TruncateFolderName(const std::wstring& folder, size_t maxLength);
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
