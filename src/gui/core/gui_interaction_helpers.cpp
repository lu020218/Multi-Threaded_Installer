#include "gui/core/gui_interaction_helpers.h"

#include "gui/core/gui_helpers.h"
#include "gui/install/gui_install_flow_utils.h"
#include "gui/core/gui_manager.h"
#include "gui/core/gui_runtime_utils.h"
#include "gui/dialogs/license_text_loader.h"
#include "installer/platform/installer_helpers.h"
#include "common/installer_logger.h"

using namespace DuiLib;

namespace MultiThreadedInstaller {

bool HandleRunningApplicationDialog(HWND hWnd, const std::vector<std::string>& processNames) {
    (void)hWnd;
    if (processNames.empty()) {
        return true;
    }

    std::vector<std::string> running = getRunningProcessesByName(processNames);
    if (running.empty()) {
        return true;
    }

    auto joinNames = [](const std::vector<std::string>& names) {
        std::string joined;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                joined += ", ";
            }
            joined += names[i];
        }
        return joined;
    };

    logInstallerInfo(std::string("[GUI] Terminating processes: ") + joinNames(running));
    terminateProcessesByName(running);
    Sleep(500);

    std::vector<std::string> remaining = getRunningProcessesByName(processNames);
    if (!remaining.empty()) {
        std::wstring title = GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L"");
        std::wstring message = GUIHelpers::GetLocalizedText(L"msg.dialog.running_app.failed", L"");
        GUIHelpers::ShowErrorDialog(hWnd, title, message);
        return false;
    }

    return true;
}

void RefreshLicenseText(CPaintManagerUI& paintManager, const InstallConfig& config) {
    CRichEditUI* licenseText = static_cast<CRichEditUI*>(paintManager.FindControl(_T("editLicense")));
    if (!licenseText) {
        return;
    }

    std::wstring languageCode = config.languageCode;
    if (languageCode.empty()) {
        languageCode = GetLanguageCodeForIndex(GetDefaultLanguageComboIndex());
    }

    const std::wstring text = LoadLocalizedLicenseText(languageCode);
    licenseText->SetText(WStringToTStr(text));
    licenseText->SetReadOnly(true);
}

void ApplyLicenseAgreementSelection(CCheckBoxUI* licenseCheckbox,
                                    bool agreed,
                                    CButtonUI* installButton,
                                    uint64_t requiredDiskSpace,
                                    CEditUI* installPathEdit) {
    if (licenseCheckbox) {
        licenseCheckbox->SetCheck(agreed);
    }
    GUIInstallFlowUtils::UpdateInstallButtonEnabled(
        installButton, licenseCheckbox, installPathEdit, requiredDiskSpace);
}

void ShowLicensePage(CPaintManagerUI& paintManager,
                     CTabLayoutUI* tabPages,
                     CCheckBoxUI* licenseCheckbox,
                     const InstallConfig& config,
                     int licensePageIndex) {
    if (!tabPages) {
        return;
    }

    RefreshLicenseText(paintManager, config);

    tabPages->SelectItem(licensePageIndex);
}

} // namespace MultiThreadedInstaller
