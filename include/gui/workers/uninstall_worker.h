#pragma once

#include <Windows.h>
#include "installer/uninstall/uninstall_manager.h"
#include <string>
#include <thread>
#include <vector>

namespace MultiThreadedInstaller {

/// 卸载后台 worker：在独立线程执行卸载，通过 PostMessage 把进度/完成回传给 GUI 窗口。
class UninstallWorker {
public:
    /// @param hNotifyWindow 接收 WM_UNINSTALL_* 消息的窗口。
    explicit UninstallWorker(HWND hNotifyWindow);
    ~UninstallWorker();

    /// 在后台线程按给定上下文启动卸载。
    void StartUninstall(const UninstallContext& context);
    /// 工作线程是否可 join。
    bool Joinable() const;

private:
    void WorkerThreadFunc(UninstallContext context);                         ///< 工作线程主体。
    void PostProgressMessage(float progress, const std::wstring& currentItem);///< 投递进度消息。
    void PostCompletionMessage(bool success, const std::wstring& errorMsg);   ///< 投递完成消息。

    HWND m_hNotifyWindow;     ///< 通知窗口。
    std::thread m_thread;     ///< 卸载工作线程。
};

} // namespace MultiThreadedInstaller

