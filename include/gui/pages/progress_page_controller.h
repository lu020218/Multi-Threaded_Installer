#pragma once

#include <UIlib.h>
#include <string>
#include <chrono>


namespace MultiThreadedInstaller {

/// 进度页控制器：更新当前文件夹、进度条、百分比与预计剩余时间。
class ProgressPageController {
public:
    ProgressPageController();
    ~ProgressPageController();

    /// 绑定页内控件。
    void Initialize(DuiLib::CPaintManagerUI* pManager);
    /// 更新进度显示（当前文件夹 + 百分比），并刷新预计剩余时间。
    void UpdateProgress(const std::wstring& folder, float percentage);
    /// 据当前进度与已用时间估算剩余时间文本。
    std::wstring CalculateEstimatedTime(float currentProgress);
    /// 标记安装开始（记录起始时间，用于剩余时间估算）。
    void StartInstallation();
    /// 重置到初始状态（复用同一控制器再次安装时）。
    void Reset();

private:
    DuiLib::CLabelUI* m_pCurrentFolderLabel;    ///< 当前文件夹标签。
    DuiLib::CProgressUI* m_pProgressBar;        ///< 进度条。
    DuiLib::CLabelUI* m_pProgressPercentLabel;  ///< 百分比标签。
    DuiLib::CLabelUI* m_pEstimatedTimeLabel;    ///< 预计剩余时间标签。

    std::chrono::steady_clock::time_point m_startTime;  ///< 安装起始时间。
    bool m_installationStarted;                         ///< 是否已开始。

    std::wstring FormatTime(int seconds);                                       ///< 秒数→可读时间。
    std::wstring TruncateFolderName(const std::wstring& folder, size_t maxLength);  ///< 截断过长路径。
};

} // namespace MultiThreadedInstaller

