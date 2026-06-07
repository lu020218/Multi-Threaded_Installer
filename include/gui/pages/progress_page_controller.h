#pragma once

#include <UIlib.h>
#include <string>
#include <chrono>


namespace MultiThreadedInstaller {

/**
 *
 *
 */
class ProgressPageController {
public:
    ProgressPageController();
    ~ProgressPageController();
    

    void Initialize(DuiLib::CPaintManagerUI* pManager);
    

    void UpdateProgress(const std::wstring& folder, float percentage);
    

    std::wstring CalculateEstimatedTime(float currentProgress);
    

    void StartInstallation();
    

    void Reset();
    
private:

    DuiLib::CLabelUI* m_pCurrentFolderLabel;
    DuiLib::CProgressUI* m_pProgressBar;
    DuiLib::CLabelUI* m_pProgressPercentLabel;
    DuiLib::CLabelUI* m_pEstimatedTimeLabel;
    

    std::chrono::steady_clock::time_point m_startTime;
    bool m_installationStarted;
    

    std::wstring FormatTime(int seconds);
    std::wstring TruncateFolderName(const std::wstring& folder, size_t maxLength);
};

} // namespace MultiThreadedInstaller

