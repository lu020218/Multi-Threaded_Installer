#pragma once

#include <UIlib.h>
#include <string>
#include <chrono>

using namespace DuiLib;

namespace MultiThreadedInstaller {

/**
 *
 *
 */
class ProgressPageController {
public:
    ProgressPageController();
    ~ProgressPageController();
    

    void Initialize(CPaintManagerUI* pManager);
    

    void UpdateProgress(const std::wstring& folder, float percentage);
    

    std::wstring CalculateEstimatedTime(float currentProgress);
    

    void StartInstallation();
    

    void Reset();
    
private:

    CLabelUI* m_pCurrentFolderLabel;
    CProgressUI* m_pProgressBar;
    CLabelUI* m_pProgressPercentLabel;
    CLabelUI* m_pEstimatedTimeLabel;
    

    std::chrono::steady_clock::time_point m_startTime;
    bool m_installationStarted;
    

    std::wstring FormatTime(int seconds);
    std::wstring TruncateFolderName(const std::wstring& folder, size_t maxLength);
};

} // namespace MultiThreadedInstaller

