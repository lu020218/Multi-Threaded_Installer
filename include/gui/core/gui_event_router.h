#pragma once

#include <UIlib.h>
#include <functional>
#include <string>
#include <unordered_map>

namespace MultiThreadedInstaller {

// 把 DuiLib 的控件通知/按键事件路由到具体业务回调，将"事件分发"从 GUIManager 中拆出。

/// 各 UI 控件点击/变更对应的业务回调（按控件名匹配后调用）。
struct GuiNotifyCallbacks {
    std::function<void()> onInstall;                 ///< 点击"安装"。
    std::function<void()> onCancel;                  ///< 点击"取消"。
    std::function<void()> onBrowse;                  ///< 点击"浏览"选目录。
    std::function<void()> onFinish;                  ///< 点击"完成"。
    std::function<void()> onCancelProgress;          ///< 安装中点击取消。
    std::function<void()> onMinimize;                ///< 最小化窗口。
    std::function<void()> onShowMore;                ///< 展开更多选项。
    std::function<void()> onLicenseLink;             ///< 点击许可协议链接。
    std::function<void()> onLicenseAgree;            ///< 同意许可。
    std::function<void()> onLicenseDisagree;         ///< 不同意许可。
    std::function<void()> onUninstallConfirm;        ///< 确认卸载。
    std::function<void()> onCloseWindow;             ///< 关闭窗口。
    std::function<void()> onLicenseCheckboxChanged;  ///< 许可勾选框变化。
    std::function<void(int)> onLanguageSelect;       ///< 切换语言（下标）。
    std::function<void(const std::wstring&)> onOpenLink;  ///< 打开外部链接。
};

/// 快捷键对应的业务回调。
struct GuiKeydownCallbacks {
    std::function<void()> onInstall;          ///< 回车触发安装。
    std::function<void()> onCancel;           ///< Esc 取消。
    std::function<void()> onCancelProgress;   ///< 安装中取消。
    std::function<void()> onFinish;           ///< 完成。
};

/// 路由一条控件通知：按 msg 的控件名/类型匹配 uiLinks 或 callbacks 并调用。命中返回 true。
bool RouteGuiNotify(
    const DuiLib::TNotifyUI& msg,
    const std::unordered_map<std::string, std::wstring>& uiLinks,
    const GuiNotifyCallbacks& callbacks);

/// 路由一次按键：依据当前页/模式/各页索引决定按键语义并调用对应回调。命中返回 true。
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
