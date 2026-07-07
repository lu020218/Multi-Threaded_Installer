#include "gui/presenters/gui_status_presenter.h"

#include "gui/core/gui_helpers.h"
#include "gui/presenters/progress_path_formatter.h"
#include "gui/core/gui_runtime_utils.h"

using namespace DuiLib;

namespace MultiThreadedInstaller::GUIStatusPresenter {
namespace {

constexpr std::size_t kProgressPathDisplayMaxChars = 60;
constexpr int kProgressTooltipWidth = 640;

} // namespace

void UpdateProgressDisplay(CPaintManagerUI& manager,
                           const std::wstring& progressPrefix,
                           const std::wstring& progressFolder,
                           float percentage) {
    CLabelUI* currentFolderLabel = static_cast<CLabelUI*>(manager.FindControl(_T("current_folder")));
    if (currentFolderLabel) {
        std::wstring prefix = progressPrefix.empty()
            ? GUIHelpers::GetLocalizedText(L"msg.progress.stage.processing", L"")
            : progressPrefix;
        if (progressFolder.empty()) {
            // 只显示阶段名（不拼具体文件）。
            currentFolderLabel->SetText(WStringToTStr(prefix));
            currentFolderLabel->SetToolTip(WStringToTStr(prefix));
            currentFolderLabel->SetToolTipWidth(kProgressTooltipWidth);
        } else {
            const std::wstring displayPath =
                FormatProgressPathForDisplay(progressFolder, kProgressPathDisplayMaxChars);
            currentFolderLabel->SetText(WStringToTStr(prefix + displayPath));
            currentFolderLabel->SetToolTip(WStringToTStr(prefix + progressFolder));
            currentFolderLabel->SetToolTipWidth(kProgressTooltipWidth);
        }
    }

    CProgressUI* progressBar = static_cast<CProgressUI*>(manager.FindControl(_T("progress_bar")));
    if (progressBar) {
        progressBar->SetValue(static_cast<int>(percentage));
    }

    CLabelUI* progressPercentLabel =
        static_cast<CLabelUI*>(manager.FindControl(_T("progress_percent")));
    if (progressPercentLabel) {
        wchar_t percentText[32];
        swprintf_s(percentText, L"%.1f%%", percentage);
        progressPercentLabel->SetText(percentText);
    }

    // 精简为“阶段 + 百分比”，隐藏 ETA/预计剩余时间。
    CLabelUI* estimatedTimeLabel =
        static_cast<CLabelUI*>(manager.FindControl(_T("estimated_time")));
    if (estimatedTimeLabel) {
        estimatedTimeLabel->SetText(_T(""));
        estimatedTimeLabel->SetVisible(false);
    }
}

void ShowInstallCompletion(CPaintManagerUI& manager,
                           const InstallConfig& config,
                           const CompletionMessageData& data) {
    CLabelUI* completionTitle = static_cast<CLabelUI*>(manager.FindControl(_T("completion_title")));
    CLabelUI* resultMessageLabel = static_cast<CLabelUI*>(manager.FindControl(_T("result_message")));
    CControlUI* successIcon = manager.FindControl(_T("completion_success_icon"));
    CControlUI* failureIcon = manager.FindControl(_T("completion_failure_icon"));
    CCheckBoxUI* openWebCheckbox =
        static_cast<CCheckBoxUI*>(manager.FindControl(_T("open_web_checkbox")));
    CButtonUI* runButton = static_cast<CButtonUI*>(manager.FindControl(_T("btnRun")));

    if (data.rebootRequired) {
        if (completionTitle) {
            completionTitle->SetText(WStringToTStr(
                GUIHelpers::GetLocalizedText(L"msg.install.reboot_required", L"")));
            completionTitle->SetTextColor(0xFFE67E22);
        }
        if (resultMessageLabel) {
            resultMessageLabel->SetText(WStringToTStr(
                GUIHelpers::GetLocalizedText(L"msg.install.reboot_required_detail", L"")));
            resultMessageLabel->SetTextColor(0xFFE67E22);
        }
        if (successIcon) {
            successIcon->SetVisible(false);
        }
        if (failureIcon) {
            failureIcon->SetVisible(true);
        }
        if (runButton) {
            runButton->SetVisible(false);
        }
        if (openWebCheckbox) {
            openWebCheckbox->SetVisible(false);
        }
        return;
    }

    if (data.success) {
        if (completionTitle) {
            completionTitle->SetText(WStringToTStr(
                GUIHelpers::GetLocalizedText(L"msg.install.success", L"")));
            completionTitle->SetTextColor(0xFF333333);
        }
        if (resultMessageLabel) {
            resultMessageLabel->SetText(WStringToTStr(
                GUIHelpers::GetLocalizedText(L"msg.install.success", L"")));
            resultMessageLabel->SetTextColor(0xFF4CAF50);
        }
        if (successIcon) {
            successIcon->SetVisible(true);
        }
        if (failureIcon) {
            failureIcon->SetVisible(false);
        }
        if (runButton) {
            runButton->SetVisible(true);
        }
        if (openWebCheckbox) {
            openWebCheckbox->SetVisible(!config.webPageUrl.empty());
        }
        return;
    }

    if (completionTitle) {
        completionTitle->SetText(WStringToTStr(
            GUIHelpers::GetLocalizedText(L"msg.install.failed", L"")));
        completionTitle->SetTextColor(0xFFFF0000);
    }
    if (resultMessageLabel) {
        std::wstring errorText = data.errorMessage;
        if (errorText.empty()) {
            errorText = GUIHelpers::GetLocalizedText(L"msg.error.install_failed", L"");
        }
        resultMessageLabel->SetText(WStringToTStr(errorText));
        resultMessageLabel->SetTextColor(0xFFFF0000);
    }
    if (successIcon) {
        successIcon->SetVisible(false);
    }
    if (failureIcon) {
        failureIcon->SetVisible(true);
    }
    if (runButton) {
        runButton->SetVisible(false);
    }
    if (openWebCheckbox) {
        openWebCheckbox->SetVisible(false);
    }
}

void ShowUninstallCompletion(CPaintManagerUI& manager, const CompletionMessageData& data) {
    CLabelUI* resultMessageLabel =
        static_cast<CLabelUI*>(manager.FindControl(_T("uninstall_result_message")));
    if (!resultMessageLabel) {
        return;
    }

    if (data.success) {
        std::wstring text = GUIHelpers::GetLocalizedText(L"msg.uninstall.success", L"");
        resultMessageLabel->SetText(WStringToTStr(text));
        resultMessageLabel->SetTextColor(0xFF4CAF50);
        return;
    }

    std::wstring prefix = GUIHelpers::GetLocalizedText(L"msg.uninstall.failed", L"");
    resultMessageLabel->SetText(WStringToTStr(prefix + data.errorMessage));
    resultMessageLabel->SetTextColor(0xFFFF0000);
}

} // namespace MultiThreadedInstaller::GUIStatusPresenter
