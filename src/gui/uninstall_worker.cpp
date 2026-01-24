#ifdef GUI_ENABLED

#include "../../include/gui/uninstall_worker.h"
#include "../../include/gui/gui_manager.h"
#include "../../include/installer/installer_helpers.h"
#include "../../include/installer/registry_utils.h"
#include "../../include/installer/uninstall_manager.h"
#include "../../include/installer/console_interface.h"
#include "../../include/installer/path_resolver.h"

#include <filesystem>
#include <algorithm>

namespace MultiThreadedInstaller {

namespace {

std::string sanitizeRegistryKeyName(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    if (result.empty()) {
        result = "Application";
    }
    return result;
}

std::string resolveManifestPath(const std::string& appName,
                                const std::string& exePath,
                                InstallerPathResolver& resolver) {
    std::string localManifest = getLocalManifestPath(exePath);
    if (!localManifest.empty() && std::filesystem::exists(localManifest)) {
        return localManifest;
    }

    if (appName.empty()) {
        return {};
    }

    std::string keyName = sanitizeRegistryKeyName(appName);
    std::string hkcuPath =
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;
    std::string hklmPath =
        "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyName;

    std::string installLocation;
    if (!readRegistryStringValue(hkcuPath, "InstallLocation", installLocation)) {
        readRegistryStringValue(hklmPath, "InstallLocation", installLocation);
    }
    if (!installLocation.empty()) {
        std::filesystem::path localPath = std::filesystem::path(installLocation) / "install.manifest.json";
        if (std::filesystem::exists(localPath)) {
            return localPath.string();
        }
    }

    std::string uninstallString;
    if (!readRegistryStringValue(hkcuPath, "UninstallString", uninstallString)) {
        readRegistryStringValue(hklmPath, "UninstallString", uninstallString);
    }
    if (!uninstallString.empty()) {
        std::filesystem::path uninstallPath(uninstallString);
        if (std::filesystem::exists(uninstallPath)) {
            std::filesystem::path baseDir = uninstallPath.parent_path();
            if (!baseDir.empty()) {
                std::filesystem::path localPath = baseDir / "install.manifest.json";
                if (std::filesystem::exists(localPath)) {
                    return localPath.string();
                }
            }
        }
    }

    std::string defaultManifest = getDefaultManifestPath(appName, resolver);
    if (!defaultManifest.empty() && std::filesystem::exists(defaultManifest)) {
        return defaultManifest;
    }

    return {};
}

} // namespace

UninstallWorker::UninstallWorker(HWND hNotifyWindow)
    : m_hNotifyWindow(hNotifyWindow) {}

UninstallWorker::~UninstallWorker() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void UninstallWorker::StartUninstall(const std::string& appName) {
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_thread = std::thread(&UninstallWorker::WorkerThreadFunc, this, appName);
}

void UninstallWorker::WorkerThreadFunc(const std::string& appName) {
    bool success = false;
    std::wstring errorMessage;

    try {
        InstallerPathResolver resolver;
        ConsoleInterface console;
        std::string exePath = getCurrentExecutablePath();
        std::string manifestPath = resolveManifestPath(appName, exePath, resolver);

        if (manifestPath.empty()) {
            throw std::runtime_error("Manifest not found for uninstall");
        }

        bool ok = uninstallFromManifest(manifestPath, resolver, console);
        if (!ok) {
            throw std::runtime_error("Uninstall failed");
        }
        success = true;
    } catch (const std::exception& e) {
        errorMessage = toWideUtf8(e.what());
    }

    PostCompletionMessage(success, errorMessage);
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
    ::PostMessage(m_hNotifyWindow, WM_UNINSTALL_COMPLETE, 0, reinterpret_cast<LPARAM>(pData));
}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
