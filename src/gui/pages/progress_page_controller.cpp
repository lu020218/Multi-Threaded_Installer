#include "gui/pages/progress_page_controller.h"
#include "gui/core/gui_helpers.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

using namespace DuiLib;

namespace MultiThreadedInstaller {

ProgressPageController::ProgressPageController()
    : m_pCurrentFolderLabel(nullptr)
    , m_pProgressBar(nullptr)
    , m_pProgressPercentLabel(nullptr)
    , m_pEstimatedTimeLabel(nullptr)
    , m_installationStarted(false) {
}

ProgressPageController::~ProgressPageController() {

}

void ProgressPageController::Initialize(CPaintManagerUI* pManager) {
    if (!pManager) {
        return;
    }
    

    m_pCurrentFolderLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("current_folder")));
    m_pProgressBar = static_cast<CProgressUI*>(pManager->FindControl(_T("progress_bar")));
    m_pProgressPercentLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("progress_percent")));
    m_pEstimatedTimeLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("estimated_time")));
    

    if (m_pProgressBar) {
        m_pProgressBar->SetMinValue(0);
        m_pProgressBar->SetMaxValue(100);
        m_pProgressBar->SetValue(0);
    }
    

    if (m_pCurrentFolderLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(L"msg.progress.preparing", L"");
        m_pCurrentFolderLabel->SetText(text.c_str());
    }
    
    if (m_pProgressPercentLabel) {
        m_pProgressPercentLabel->SetText(_T("0%"));
    }
    
    if (m_pEstimatedTimeLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(L"msg.progress.eta", L"");
        m_pEstimatedTimeLabel->SetText(text.c_str());
    }
}

void ProgressPageController::UpdateProgress(const std::wstring& folder, float percentage) {

    if (percentage < 0.0f) {
        percentage = 0.0f;
    }
    if (percentage > 100.0f) {
        percentage = 100.0f;
    }
    

    if (m_pCurrentFolderLabel && !folder.empty()) {
        std::wstring displayFolder = TruncateFolderName(folder, 50);
        std::wstring prefix = GUIHelpers::GetLocalizedText(L"msg.progress.installing_prefix", L"");
        std::wstring text = prefix + displayFolder;
        CDuiString displayText(text.c_str());
        m_pCurrentFolderLabel->SetText(displayText);
    }
    

    if (m_pProgressBar) {
        int progressValue = static_cast<int>(percentage);
        m_pProgressBar->SetValue(progressValue);
    }
    

    if (m_pProgressPercentLabel) {
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(1) << percentage << L"%";
        CDuiString percentText(ss.str().c_str());
        m_pProgressPercentLabel->SetText(percentText);
    }
    

    if (m_pEstimatedTimeLabel && m_installationStarted) {
        std::wstring timeStr = CalculateEstimatedTime(percentage);
        CDuiString timeText(timeStr.c_str());
        m_pEstimatedTimeLabel->SetText(timeText);
    }
}

std::wstring ProgressPageController::CalculateEstimatedTime(float currentProgress) {
    if (!m_installationStarted || currentProgress <= 0.0f) {
        return GUIHelpers::GetLocalizedText(L"msg.progress.eta", L"");
    }
    

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime);
    int elapsedSeconds = static_cast<int>(elapsed.count());
    

    float progressFraction = currentProgress / 100.0f;
    if (progressFraction < 0.01f) {

        return GUIHelpers::GetLocalizedText(L"msg.progress.eta", L"");
    }
    
    int estimatedTotalSeconds = static_cast<int>(elapsedSeconds / progressFraction);
    int remainingSeconds = estimatedTotalSeconds - elapsedSeconds;
    

    if (remainingSeconds < 0) {
        remainingSeconds = 0;
    }
    

    std::wstring timeStr = FormatTime(remainingSeconds);
    std::wstring prefix = GUIHelpers::GetLocalizedText(L"msg.progress.eta_prefix", L"");
    return prefix + timeStr;
}

void ProgressPageController::StartInstallation() {
    m_startTime = std::chrono::steady_clock::now();
    m_installationStarted = true;
    

    if (m_pCurrentFolderLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(L"msg.progress.preparing", L"");
        m_pCurrentFolderLabel->SetText(text.c_str());
    }
    
    if (m_pProgressBar) {
        m_pProgressBar->SetValue(0);
    }
    
    if (m_pProgressPercentLabel) {
        m_pProgressPercentLabel->SetText(_T("0%"));
    }
    
    if (m_pEstimatedTimeLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(L"msg.progress.eta", L"");
        m_pEstimatedTimeLabel->SetText(text.c_str());
    }
}

void ProgressPageController::Reset() {
    m_installationStarted = false;
    
    if (m_pCurrentFolderLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(L"msg.progress.preparing", L"");
        m_pCurrentFolderLabel->SetText(text.c_str());
    }
    
    if (m_pProgressBar) {
        m_pProgressBar->SetValue(0);
    }
    
    if (m_pProgressPercentLabel) {
        m_pProgressPercentLabel->SetText(_T("0%"));
    }
    
    if (m_pEstimatedTimeLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(L"msg.progress.eta", L"");
        m_pEstimatedTimeLabel->SetText(text.c_str());
    }
}

std::wstring ProgressPageController::FormatTime(int seconds) {
    if (seconds < 60) {

        std::wstringstream ss;
        std::wstring unit = GUIHelpers::GetLocalizedText(L"msg.time.seconds", L"");
        ss << seconds << unit;
        return ss.str();
    } else if (seconds < 3600) {

        int minutes = seconds / 60;
        int remainingSeconds = seconds % 60;
        std::wstringstream ss;
        std::wstring minUnit = GUIHelpers::GetLocalizedText(L"msg.time.minutes", L"");
        std::wstring secUnit = GUIHelpers::GetLocalizedText(L"msg.time.seconds", L"");
        ss << minutes << minUnit << remainingSeconds << secUnit;
        return ss.str();
    } else {

        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        int remainingSeconds = seconds % 60;
        std::wstringstream ss;
        std::wstring hourUnit = GUIHelpers::GetLocalizedText(L"msg.time.hours", L"");
        std::wstring minUnit = GUIHelpers::GetLocalizedText(L"msg.time.minutes", L"");
        std::wstring secUnit = GUIHelpers::GetLocalizedText(L"msg.time.seconds", L"");
        ss << hours << hourUnit << minutes << minUnit << remainingSeconds << secUnit;
        return ss.str();
    }
}

std::wstring ProgressPageController::TruncateFolderName(const std::wstring& folder, size_t maxLength) {
    if (folder.length() <= maxLength) {
        return folder;
    }
    

    return folder.substr(0, maxLength - 3) + L"...";
}

} // namespace MultiThreadedInstaller

