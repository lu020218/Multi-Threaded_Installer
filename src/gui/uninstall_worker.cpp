#include "../../include/gui/uninstall_worker.h"
#include "../../include/gui/gui_manager.h"
#include "../../include/installer/installer_helpers.h"
#include "../../include/installer/registry_utils.h"
#include "../../include/installer/uninstall_manager.h"
#include "../../include/installer/console_interface.h"
#include "../../include/installer/path_resolver.h"
#include "common/utf8_utils.h"
#include "common/installer_logger.h"

namespace MultiThreadedInstaller {

namespace {

template <typename T>
bool PostOwnedWorkerMessage(HWND hwnd, UINT message, T* payload, const char* tag) {
    if (!payload) {
        return false;
    }
    if (!hwnd || !::IsWindow(hwnd)) {
        logInstallerWarning(std::string(tag) + " notify window is invalid; dropping message.");
        delete payload;
        return false;
    }
    if (!::PostMessage(hwnd, message, 0, reinterpret_cast<LPARAM>(payload))) {
        logInstallerWarning(std::string(tag) + " PostMessage failed; dropping message.");
        delete payload;
        return false;
    }
    return true;
}

} // namespace

UninstallWorker::UninstallWorker(HWND hNotifyWindow)
    : m_hNotifyWindow(hNotifyWindow) {}

UninstallWorker::~UninstallWorker() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void UninstallWorker::StartUninstall(const std::string& manifestPath) {
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_thread = std::thread(&UninstallWorker::WorkerThreadFunc, this, manifestPath);
}

bool UninstallWorker::Joinable() const {
    return m_thread.joinable();
}

void UninstallWorker::WorkerThreadFunc(const std::string& manifestPath) {
    bool success = false;
    std::wstring errorMessage;

    try {
        InstallerPathResolver resolver;
        CliSupport console;
        if (manifestPath.empty()) {
            throw std::runtime_error("Manifest not found for uninstall");
        }

        UninstallProgressCallback progressCb = [this](const UninstallProgressInfo& info) {
            std::wstring item = Utf8ToWide(info.currentItem);
            PostProgressMessage(info.progress, item);
        };

        bool ok = uninstallFromManifest(manifestPath, resolver, console, progressCb);
        if (!ok) {
            throw std::runtime_error("Uninstall failed");
        }
        success = true;
    } catch (const std::exception& e) {
        errorMessage = Utf8ToWide(e.what());
    }

    PostCompletionMessage(success, errorMessage);
}

void UninstallWorker::PostProgressMessage(float progress, const std::wstring& currentItem) {
    ProgressMessageData* pData = new ProgressMessageData();

    size_t copyLen = currentItem.length();
    if (copyLen >= 256) {
        copyLen = 255;
    }
    wcsncpy_s(pData->currentFolder, 256, currentItem.c_str(), copyLen);
    pData->currentFolder[copyLen] = L'\0';
    pData->percentage = progress * 100.0f;

    PostOwnedWorkerMessage(m_hNotifyWindow, WM_UNINSTALL_PROGRESS, pData, "[GUI][UninstallWorker]");
}

void UninstallWorker::PostCompletionMessage(bool success, const std::wstring& errorMsg) {
    CompletionMessageData* pData = new CompletionMessageData();
    pData->success = success;
    size_t copyLen = errorMsg.length();
    if (copyLen >= 512) {
        copyLen = 511;
    }
    wcsncpy_s(pData->errorMessage, 512, errorMsg.c_str(), copyLen);
    pData->errorMessage[copyLen] = L'\0';
    PostOwnedWorkerMessage(m_hNotifyWindow, WM_UNINSTALL_COMPLETE, pData, "[GUI][UninstallWorker]");
}

} // namespace MultiThreadedInstaller
