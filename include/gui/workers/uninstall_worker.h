#pragma once

#include <Windows.h>
#include "installer/uninstall/uninstall_manager.h"
#include <string>
#include <thread>
#include <vector>

namespace MultiThreadedInstaller {

class UninstallWorker {
public:
    explicit UninstallWorker(HWND hNotifyWindow);
    ~UninstallWorker();

    void StartUninstall(const UninstallContext& context);
    bool Joinable() const;

private:
    void WorkerThreadFunc(UninstallContext context);
    void PostProgressMessage(float progress, const std::wstring& currentItem);
    void PostCompletionMessage(bool success, const std::wstring& errorMsg);

    HWND m_hNotifyWindow;
    std::thread m_thread;
};

} // namespace MultiThreadedInstaller

