#ifdef GUI_ENABLED

#include "../../include/gui/progress_page_controller.h"
#include "../../include/gui/gui_helpers.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace MultiThreadedInstaller {

ProgressPageController::ProgressPageController()
    : m_pCurrentFolderLabel(nullptr)
    , m_pProgressBar(nullptr)
    , m_pProgressPercentLabel(nullptr)
    , m_pEstimatedTimeLabel(nullptr)
    , m_installationStarted(false) {
}

ProgressPageController::~ProgressPageController() {
    // 控件由DuiLib管理，不需要手动释放
}

void ProgressPageController::Initialize(CPaintManagerUI* pManager) {
    if (!pManager) {
        return;
    }
    
    // 获取控件指针
    m_pCurrentFolderLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("current_folder")));
    m_pProgressBar = static_cast<CProgressUI*>(pManager->FindControl(_T("progress_bar")));
    m_pProgressPercentLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("progress_percent")));
    m_pEstimatedTimeLabel = static_cast<CLabelUI*>(pManager->FindControl(_T("estimated_time")));
    
    // 初始化进度条范围
    if (m_pProgressBar) {
        m_pProgressBar->SetMinValue(0);
        m_pProgressBar->SetMaxValue(100);
        m_pProgressBar->SetValue(0);
    }
    
    // 设置初始文本
    if (m_pCurrentFolderLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(
            L"msg.progress.preparing",
            L"Preparing installation...");
        m_pCurrentFolderLabel->SetText(text.c_str());
    }
    
    if (m_pProgressPercentLabel) {
        m_pProgressPercentLabel->SetText(_T("0%"));
    }
    
    if (m_pEstimatedTimeLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(
            L"msg.progress.eta",
            L"Estimated time remaining: Calculating...");
        m_pEstimatedTimeLabel->SetText(text.c_str());
    }
}

void ProgressPageController::UpdateProgress(const std::wstring& folder, float percentage) {
    // 确保百分比在有效范围内 (0-100)
    if (percentage < 0.0f) {
        percentage = 0.0f;
    }
    if (percentage > 100.0f) {
        percentage = 100.0f;
    }
    
    // 更新当前文件夹名称
    if (m_pCurrentFolderLabel && !folder.empty()) {
        std::wstring displayFolder = TruncateFolderName(folder, 50);
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.progress.installing_prefix",
            L"Installing: ");
        std::wstring text = prefix + displayFolder;
        CDuiString displayText(text.c_str());
        m_pCurrentFolderLabel->SetText(displayText);
    }
    
    // 更新进度条
    if (m_pProgressBar) {
        int progressValue = static_cast<int>(percentage);
        m_pProgressBar->SetValue(progressValue);
    }
    
    // 更新百分比标签
    if (m_pProgressPercentLabel) {
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(1) << percentage << L"%";
        CDuiString percentText(ss.str().c_str());
        m_pProgressPercentLabel->SetText(percentText);
    }
    
    // 更新预计剩余时间
    if (m_pEstimatedTimeLabel && m_installationStarted) {
        std::wstring timeStr = CalculateEstimatedTime(percentage);
        CDuiString timeText(timeStr.c_str());
        m_pEstimatedTimeLabel->SetText(timeText);
    }
}

std::wstring ProgressPageController::CalculateEstimatedTime(float currentProgress) {
    if (!m_installationStarted || currentProgress <= 0.0f) {
        return GUIHelpers::GetLocalizedText(
            L"msg.progress.eta",
            L"Estimated time remaining: Calculating...");
    }
    
    // 计算已用时间
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime);
    int elapsedSeconds = static_cast<int>(elapsed.count());
    
    // 根据当前进度估算总时间
    float progressFraction = currentProgress / 100.0f;
    if (progressFraction < 0.01f) {
        // 进度太小，无法准确估算
        return GUIHelpers::GetLocalizedText(
            L"msg.progress.eta",
            L"Estimated time remaining: Calculating...");
    }
    
    int estimatedTotalSeconds = static_cast<int>(elapsedSeconds / progressFraction);
    int remainingSeconds = estimatedTotalSeconds - elapsedSeconds;
    
    // 确保剩余时间不为负数
    if (remainingSeconds < 0) {
        remainingSeconds = 0;
    }
    
    // 格式化时间字符串
    std::wstring timeStr = FormatTime(remainingSeconds);
    std::wstring prefix = GUIHelpers::GetLocalizedText(
        L"msg.progress.eta_prefix",
        L"Estimated time remaining: ");
    return prefix + timeStr;
}

void ProgressPageController::StartInstallation() {
    m_startTime = std::chrono::steady_clock::now();
    m_installationStarted = true;
    
    // 重置进度显示
    if (m_pCurrentFolderLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(
            L"msg.progress.preparing",
            L"Preparing installation...");
        m_pCurrentFolderLabel->SetText(text.c_str());
    }
    
    if (m_pProgressBar) {
        m_pProgressBar->SetValue(0);
    }
    
    if (m_pProgressPercentLabel) {
        m_pProgressPercentLabel->SetText(_T("0%"));
    }
    
    if (m_pEstimatedTimeLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(
            L"msg.progress.eta",
            L"Estimated time remaining: Calculating...");
        m_pEstimatedTimeLabel->SetText(text.c_str());
    }
}

void ProgressPageController::Reset() {
    m_installationStarted = false;
    
    if (m_pCurrentFolderLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(
            L"msg.progress.preparing",
            L"Preparing installation...");
        m_pCurrentFolderLabel->SetText(text.c_str());
    }
    
    if (m_pProgressBar) {
        m_pProgressBar->SetValue(0);
    }
    
    if (m_pProgressPercentLabel) {
        m_pProgressPercentLabel->SetText(_T("0%"));
    }
    
    if (m_pEstimatedTimeLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(
            L"msg.progress.eta",
            L"Estimated time remaining: Calculating...");
        m_pEstimatedTimeLabel->SetText(text.c_str());
    }
}

std::wstring ProgressPageController::FormatTime(int seconds) {
    if (seconds < 60) {
        // 少于1分钟，显示秒数
        std::wstringstream ss;
        std::wstring unit = GUIHelpers::GetLocalizedText(
            L"msg.time.seconds",
            L" sec");
        ss << seconds << unit;
        return ss.str();
    } else if (seconds < 3600) {
        // 少于1小时，显示分钟和秒
        int minutes = seconds / 60;
        int remainingSeconds = seconds % 60;
        std::wstringstream ss;
        std::wstring minUnit = GUIHelpers::GetLocalizedText(
            L"msg.time.minutes",
            L" min ");
        std::wstring secUnit = GUIHelpers::GetLocalizedText(
            L"msg.time.seconds",
            L" sec");
        ss << minutes << minUnit << remainingSeconds << secUnit;
        return ss.str();
    } else {
        // 1小时或更多，显示小时、分钟和秒
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        int remainingSeconds = seconds % 60;
        std::wstringstream ss;
        std::wstring hourUnit = GUIHelpers::GetLocalizedText(
            L"msg.time.hours",
            L" hr ");
        std::wstring minUnit = GUIHelpers::GetLocalizedText(
            L"msg.time.minutes",
            L" min ");
        std::wstring secUnit = GUIHelpers::GetLocalizedText(
            L"msg.time.seconds",
            L" sec");
        ss << hours << hourUnit << minutes << minUnit << remainingSeconds << secUnit;
        return ss.str();
    }
}

std::wstring ProgressPageController::TruncateFolderName(const std::wstring& folder, size_t maxLength) {
    if (folder.length() <= maxLength) {
        return folder;
    }
    
    // 截断并添加省略号
    return folder.substr(0, maxLength - 3) + L"...";
}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
