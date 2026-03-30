#include "gui/gui_install_actions.h"

#include "gui/gui_helpers.h"
#include "gui/gui_runtime_utils.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include "installer/installer_helpers.h"

#include <filesystem>
#include <fstream>
#include <json.hpp>

namespace MultiThreadedInstaller::GUIInstallActions {

namespace {

using json = nlohmann::json;

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

std::wstring ReplaceToken(std::wstring value,
                          const std::wstring& token,
                          const std::wstring& replacement) {
    if (token.empty() || replacement.empty()) {
        return value;
    }
    size_t position = 0;
    while ((position = value.find(token, position)) != std::wstring::npos) {
        value.replace(position, token.size(), replacement);
        position += replacement.size();
    }
    return value;
}

std::wstring ExpandStatePathTemplate(const std::wstring& templatePath,
                                     const std::wstring& installPath,
                                     const InstallConfig& config) {
    std::wstring expanded = templatePath;
    expanded = ReplaceToken(std::move(expanded), L"%InstallDir%", installPath);
    expanded = ReplaceToken(std::move(expanded), L"%AppVersion%", config.version);
    expanded = ReplaceToken(std::move(expanded), L"%AppId%", config.appId);
    expanded = ReplaceToken(std::move(expanded), L"%DirectoryName%", config.directoryName);

    DWORD required = ExpandEnvironmentStringsW(expanded.c_str(), nullptr, 0);
    if (required == 0) {
        return expanded;
    }
    std::wstring output(required, L'\0');
    DWORD written = ExpandEnvironmentStringsW(expanded.c_str(), output.data(), required);
    if (written == 0) {
        return expanded;
    }
    if (!output.empty() && output.back() == L'\0') {
        output.pop_back();
    }
    return output;
}

std::vector<std::pair<std::wstring, std::wstring>> LoadAppliedEnvironmentFromStateFile(
    const std::wstring& stateFilePath) {
    std::vector<std::pair<std::wstring, std::wstring>> environment;
    if (stateFilePath.empty() || !std::filesystem::exists(stateFilePath)) {
        return environment;
    }

    std::ifstream input(stateFilePath, std::ios::binary);
    if (!input) {
        logInstallerWarning("[GUI] Failed to open post-setup state file for environment replay.");
        return environment;
    }

    std::string payload((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (payload.empty()) {
        return environment;
    }

    json root = json::parse(payload, nullptr, false);
    if (root.is_discarded() || !root.contains("environment") || !root["environment"].is_array()) {
        logInstallerWarning("[GUI] Invalid post-setup state file format; launching without replayed environment.");
        return environment;
    }

    for (const auto& item : root["environment"]) {
        if (!item.is_object()) {
            continue;
        }
        const std::string key = item.value("key", "");
        const std::string value = item.value("value", "");
        if (key.empty()) {
            continue;
        }
        environment.emplace_back(Utf8ToWide(key), Utf8ToWide(value));
    }
    return environment;
}

} // namespace

bool TryBuildInstallStartRequest(HWND hWnd,
                                 CPaintManagerUI& manager,
                                 CEditUI* installPathEdit,
                                 const InstallConfig& config,
                                 InstallStartRequest& request) {
    CCheckBoxUI* agree = static_cast<CCheckBoxUI*>(manager.FindControl(_T("chkAgree")));
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
    return true;
}

std::vector<std::string> BuildIdentityCandidatesFromConfig(const InstallConfig& config) {
    std::vector<std::string> legacyIds;
    legacyIds.reserve(config.legacyAppIds.size());
    for (const auto& legacyId : config.legacyAppIds) {
        legacyIds.push_back(WideToUtf8(legacyId));
    }
    return buildIdentityCandidates(
        WideToUtf8(config.appId), legacyIds, WideToUtf8(config.applicationName));
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
            bool started = false;
            if (!config.postSetupStatePath.empty()) {
                const std::wstring stateFilePath =
                    ExpandStatePathTemplate(config.postSetupStatePath, installPath, config);
                const auto environment = LoadAppliedEnvironmentFromStateFile(stateFilePath);
                if (!environment.empty()) {
                    started =
                        GUIHelpers::LaunchApplicationWithEnvironment(exePath, installPath, environment);
                } else {
                    logInstallerWarning("[GUI] Post-setup state file missing or empty; launching application with current environment.");
                }
            }
            if (!started) {
                started = GUIHelpers::LaunchApplication(exePath, installPath);
            }
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
