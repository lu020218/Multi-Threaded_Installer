#pragma once

#include <UIlib.h>
#include <functional>
#include <string>
#include <unordered_map>

namespace MultiThreadedInstaller {

struct GuiNotifyCallbacks {
    std::function<void()> onInstall;
    std::function<void()> onCancel;
    std::function<void()> onBrowse;
    std::function<void()> onFinish;
    std::function<void()> onCancelProgress;
    std::function<void()> onMinimize;
    std::function<void()> onShowMore;
    std::function<void()> onLicenseLink;
    std::function<void()> onLicenseBack;
    std::function<void()> onUninstallConfirm;
    std::function<void()> onCloseWindow;
    std::function<void()> onLicenseCheckboxChanged;
    std::function<void()> onLicenseAgreementSync;
    std::function<void(int)> onLanguageSelect;
    std::function<void(const std::wstring&)> onOpenLink;
};

struct GuiKeydownCallbacks {
    std::function<void()> onInstall;
    std::function<void()> onCancel;
    std::function<void()> onCancelProgress;
    std::function<void()> onFinish;
};

bool RouteGuiNotify(
    const DuiLib::TNotifyUI& msg,
    const std::unordered_map<std::string, std::wstring>& uiLinks,
    const GuiNotifyCallbacks& callbacks);

bool RouteGuiKeyDown(
    WPARAM key,
    int currentPage,
    bool uninstallMode,
    bool installEnabled,
    int welcomePageIndex,
    int progressPageIndex,
    int completionPageIndex,
    int licensePageIndex,
    const GuiKeydownCallbacks& callbacks);

}  // namespace MultiThreadedInstaller
