#include "../../include/gui/gui_event_router.h"
#include "common/utf8_utils.h"

namespace MultiThreadedInstaller {

namespace {

using namespace DuiLib;

std::string ControlNameToUtf8(const CDuiString& name) {
    return WideToUtf8(TCharToWide(name.GetData()));
}

}  // namespace

bool RouteGuiNotify(
    const TNotifyUI& msg,
    const std::unordered_map<std::string, std::wstring>& uiLinks,
    const GuiNotifyCallbacks& callbacks) {
    const CDuiString senderName = msg.pSender ? msg.pSender->GetName() : _T("");

    if (msg.sType == _T("click")) {
        if (senderName == _T("install_button") || senderName == _T("btnInstall")) {
            callbacks.onInstall();
            return true;
        }
        if (senderName == _T("cancel_button") ||
            senderName == _T("btnClose") ||
            senderName == _T("closebtn") ||
            senderName == _T("close_button")) {
            callbacks.onCancel();
            return true;
        }
        if (senderName == _T("browse_button") || senderName == _T("btnSelectDir")) {
            callbacks.onBrowse();
            return true;
        }
        if (senderName == _T("finish_button") || senderName == _T("btnRun")) {
            callbacks.onFinish();
            return true;
        }
        if (senderName == _T("cancel_progress_button")) {
            callbacks.onCancelProgress();
            return true;
        }
        if (senderName == _T("minbtn")) {
            callbacks.onMinimize();
            return true;
        }
        if (senderName == _T("btnShowMore")) {
            callbacks.onShowMore();
            return true;
        }
        if (senderName == _T("btnAgreement")) {
            callbacks.onLicenseLink();
            return true;
        }
        if (senderName == _T("btnLicenseAgree")) {
            callbacks.onLicenseAgree();
            return true;
        }
        if (senderName == _T("btnLicenseDisagree")) {
            callbacks.onLicenseDisagree();
            return true;
        }
        if (senderName == _T("btnUninstallConfirm")) {
            callbacks.onUninstallConfirm();
            return true;
        }
        if (senderName == _T("btnUninstallCancel") || senderName == _T("btnUninstallFinish")) {
            callbacks.onCloseWindow();
            return true;
        }

        const std::string controlName = ControlNameToUtf8(senderName);
        const auto it = uiLinks.find(controlName);
        if (it != uiLinks.end() && !it->second.empty()) {
            callbacks.onOpenLink(it->second);
            return true;
        }
        return false;
    }

    if (msg.sType == _T("selectchanged")) {
        if (senderName == _T("license_checkbox")) {
            callbacks.onLicenseCheckboxChanged();
            return true;
        }
        return false;
    }

    if (msg.sType == _T("itemselect")) {
        if (senderName == _T("comboLanguageSelect")) {
            callbacks.onLanguageSelect(static_cast<int>(msg.wParam));
            return true;
        }
        return false;
    }

    if (msg.sType == _T("link")) {
        if (senderName == _T("license_link")) {
            callbacks.onLicenseLink();
            return true;
        }
        return false;
    }

    return false;
}

bool RouteGuiKeyDown(
    WPARAM key,
    int currentPage,
    bool uninstallMode,
    bool installEnabled,
    int welcomePageIndex,
    int progressPageIndex,
    int completionPageIndex,
    int licensePageIndex,
    const GuiKeydownCallbacks& callbacks) {
    const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

    if (altPressed) {
        if (key == 'I' && currentPage == welcomePageIndex && installEnabled) {
            callbacks.onInstall();
            return true;
        }
        if (key == 'C' &&
            (currentPage == welcomePageIndex ||
             (!uninstallMode && currentPage == licensePageIndex) ||
             currentPage == progressPageIndex)) {
            if (currentPage == progressPageIndex) {
                callbacks.onCancelProgress();
            } else {
                callbacks.onCancel();
            }
            return true;
        }
        if (key == 'F' && currentPage == completionPageIndex) {
            callbacks.onFinish();
            return true;
        }
        return false;
    }

    if (key == VK_RETURN) {
        if (currentPage == welcomePageIndex && installEnabled) {
            callbacks.onInstall();
            return true;
        }
        if (currentPage == completionPageIndex) {
            callbacks.onFinish();
            return true;
        }
        return false;
    }

    if (key == VK_ESCAPE) {
        if (currentPage == welcomePageIndex ||
            (!uninstallMode && currentPage == licensePageIndex)) {
            callbacks.onCancel();
            return true;
        }
        if (currentPage == progressPageIndex) {
            callbacks.onCancelProgress();
            return true;
        }
        if (currentPage == completionPageIndex) {
            callbacks.onFinish();
            return true;
        }
    }

    return false;
}

}  // namespace MultiThreadedInstaller
