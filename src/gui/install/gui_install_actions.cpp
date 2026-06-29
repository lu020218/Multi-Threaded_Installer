#include "gui/install/gui_install_actions.h"

#include "gui/core/gui_helpers.h"
#include "gui/core/gui_runtime_utils.h"
#include "installer/platform/installer_helpers.h"
#include "common/utf8_utils.h"

using namespace DuiLib;

namespace MultiThreadedInstaller::GUIInstallActions {

namespace {

// userdata 前缀约定：组件勾选框写 userdata="component:<id>"。
const std::wstring kComponentUserDataPrefix = L"component:";

// FindControl 收集回调：把所有 component:<id> 勾选框累积进 pData（始终返回 nullptr 以遍历全部）。
CControlUI* CALLBACK CollectComponentCheckboxProc(CControlUI* control, LPVOID data) {
    if (!control || !data) {
        return nullptr;
    }
    DuiLib::CDuiString ud = control->GetUserData();
    const wchar_t* raw = ud.GetData();
    if (raw) {
        std::wstring text(raw);
        if (text.rfind(kComponentUserDataPrefix, 0) == 0) {
            const std::wstring idW = text.substr(kComponentUserDataPrefix.size());
            if (!idW.empty()) {
                auto* out =
                    static_cast<std::vector<std::pair<std::string, CCheckBoxUI*>>*>(data);
                out->emplace_back(WideToUtf8(idW), static_cast<CCheckBoxUI*>(control));
            }
        }
    }
    return nullptr;
}

std::wstring ResolveLanguageCode(CPaintManagerUI& manager, const InstallConfig& config) {
    if (auto* langCombo = static_cast<CComboUI*>(manager.FindControl(_T("comboLanguageSelect")))) {
        int index = langCombo->GetCurSel();
        if (index >= 0) {
            return GetLanguageCodeForIndex(index);
        }
    }
    if (!config.languageCode.empty()) {
        return config.languageCode;
    }
    return GetLanguageCodeForIndex(GetDefaultLanguageComboIndex());
}

} // namespace

std::vector<std::pair<std::string, CCheckBoxUI*>> EnumerateComponentCheckboxes(
    CPaintManagerUI& manager) {
    std::vector<std::pair<std::string, CCheckBoxUI*>> result;
    if (CControlUI* root = manager.GetRoot()) {
        root->FindControl(CollectComponentCheckboxProc, &result, UIFIND_ALL);
    }
    return result;
}

std::vector<std::string> CollectSelectedComponentIds(CPaintManagerUI& manager) {
    std::vector<std::string> ids;
    for (const auto& entry : EnumerateComponentCheckboxes(manager)) {
        if (entry.second && entry.second->GetCheck()) {
            ids.push_back(entry.first);
        }
    }
    return ids;
}

bool TryBuildInstallStartRequest(HWND hWnd,
                                 CPaintManagerUI& manager,
                                 CEditUI* installPathEdit,
                                 const InstallConfig& config,
                                 InstallStartRequest& request) {
    CCheckBoxUI* agree = static_cast<CCheckBoxUI*>(manager.FindControl(_T("license_checkbox")));
    if (agree && !agree->GetCheck()) {
        GUIHelpers::ShowWarningDialog(
            hWnd,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L""),
            GUIHelpers::GetLocalizedText(L"msg.dialog.agree_required", L""));
        return false;
    }

    request.installPath.clear();
    if (installPathEdit) {
        request.installPath = installPathEdit->GetText().GetData();
    }
    if (request.installPath.empty()) {
        GUIHelpers::ShowWarningDialog(
            hWnd,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.warning", L""),
            GUIHelpers::GetLocalizedText(L"msg.dialog.select_install_dir", L""));
        return false;
    }

    request.autoRun = false;
    if (auto* autoRun = static_cast<CCheckBoxUI*>(manager.FindControl(_T("chkAutoRun")))) {
        request.autoRun = autoRun->GetCheck();
    }

    request.desktopIcons = false;
    if (auto* shortcut = static_cast<CCheckBoxUI*>(manager.FindControl(_T("chkShotcut")))) {
        request.desktopIcons = shortcut->GetCheck();
    }

    request.languageCode = ResolveLanguageCode(manager, config);
    request.selectedComponentIds = CollectSelectedComponentIds(manager);
    return true;
}

void RunPostInstallActions(HWND hWnd,
                           CPaintManagerUI& manager,
                           CEditUI* installPathEdit,
                           const InstallConfig& config) {
    bool shouldRun = true;
    if (auto* runAppCheckbox =
            static_cast<CCheckBoxUI*>(manager.FindControl(_T("run_app_checkbox")))) {
        shouldRun = runAppCheckbox->GetCheck();
    }

    if (shouldRun) {
        std::wstring installPath;
        if (installPathEdit) {
            installPath = installPathEdit->GetText().GetData();
        }

        if (!installPath.empty() && !config.executableName.empty()) {
            std::wstring exePath = installPath;
            if (!exePath.empty() && exePath.back() != L'\\' && exePath.back() != L'/') {
                exePath += L"\\";
            }
            exePath += config.executableName;
            bool started = GUIHelpers::LaunchApplication(exePath, installPath);
            if (!started) {
                GUIHelpers::ShowWarningDialog(
                    hWnd,
                    GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L""),
                    GUIHelpers::GetLocalizedText(L"msg.dialog.launch_failed", L""));
            }
        }
    }

    if (auto* openWebCheckbox =
            static_cast<CCheckBoxUI*>(manager.FindControl(_T("open_web_checkbox")))) {
        if (openWebCheckbox->GetCheck() && !config.webPageUrl.empty()) {
            if (!GUIHelpers::OpenWebPage(config.webPageUrl)) {
                GUIHelpers::ShowWarningDialog(
                    hWnd,
                    GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L""),
                    GUIHelpers::GetLocalizedText(L"msg.dialog.web_failed", L""));
            }
        }
    }
}

} // namespace MultiThreadedInstaller::GUIInstallActions
