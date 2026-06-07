#include "gui/presenters/gui_text_presenter.h"

#include "gui/core/gui_helpers.h"
#include "gui/core/gui_runtime_utils.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"

using namespace DuiLib;

namespace MultiThreadedInstaller::GUITextPresenter {

namespace {

void SetLabelText(CPaintManagerUI& manager, LPCTSTR controlName, const std::wstring& text) {
    CLabelUI* label = static_cast<CLabelUI*>(manager.FindControl(controlName));
    if (label) {
        label->SetText(WStringToTStr(text));
    }
}

std::wstring BuildVersionText(const InstallConfig& config) {
    return GUIHelpers::GetLocalizedText(L"msg.version.prefix", L"") + config.version;
}

} // namespace

void BindStaticAppTexts(CPaintManagerUI& manager, const InstallConfig& config) {
    SetLabelText(manager, _T("app_name"), config.applicationName);
    SetLabelText(manager, _T("app_name_progress"), config.applicationName);
    SetLabelText(manager, _T("app_name_completion"), config.applicationName);
    SetLabelText(manager, _T("app_name_uninstall"), config.applicationName);
    RefreshVersionTexts(manager, config);
}

void RefreshVersionTexts(CPaintManagerUI& manager, const InstallConfig& config) {
    const std::wstring versionText = BuildVersionText(config);
    SetLabelText(manager, _T("app_version"), versionText);
    SetLabelText(manager, _T("app_version_progress"), versionText);
    SetLabelText(manager, _T("app_version_completion"), versionText);
    SetLabelText(manager, _T("app_version_uninstall"), versionText);
}

bool ApplyLanguage(CPaintManagerUI& manager, InstallConfig& config, const std::wstring& code) {
    std::wstring langPath = GetLanguageFilePath(code);
    if (langPath.empty()) {
        return false;
    }

    std::wstring appliedCode = code;
    logInstallerInfo(std::string("[GUI] Language resource path: ") +
                     WideToUtf8(TCharToWide(CPaintManagerUI::GetResourcePath().GetData())) +
                     " file=" + WideToUtf8(langPath));

    if (!CResourceManager::GetInstance()->LoadLanguage(langPath.c_str())) {
        if (code != L"en_US") {
            std::wstring fallbackPath = GetLanguageFilePath(L"en_US");
            if (!fallbackPath.empty() &&
                CResourceManager::GetInstance()->LoadLanguage(fallbackPath.c_str())) {
                CResourceManager::GetInstance()->SetLanguage(L"en_US");
                appliedCode = L"en_US";
                logInstallerInfo(std::string("[GUI] Language fallback loaded: ") +
                                 WideToUtf8(fallbackPath));
            } else {
                logInstallerWarning(std::string("[GUI] Failed to load language file: ") +
                                    WideToUtf8(langPath));
                return false;
            }
        } else {
            logInstallerWarning(std::string("[GUI] Failed to load language file: ") +
                                WideToUtf8(langPath));
            return false;
        }
    } else {
        CResourceManager::GetInstance()->SetLanguage(code.c_str());
    }

    config.languageCode = appliedCode;
    CResourceManager::GetInstance()->ReloadText();
    manager.NeedUpdate();
    manager.Invalidate();
    RefreshVersionTexts(manager, config);
    return true;
}

} // namespace MultiThreadedInstaller::GUITextPresenter
