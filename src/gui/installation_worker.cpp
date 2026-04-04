#include "../../include/gui/installation_worker.h"
#include "../../include/gui/gui_manager.h"
#include "../../include/gui/gui_helpers.h"
#include "../../include/installer/metadata_parser.h"
#include "../../include/installer/path_resolver.h"
#include "../../include/installer/installer_helpers.h"
#include "../../include/installer/install_service.h"
#include "common/utf8_utils.h"
#include "common/installer_logger.h"

#include <codecvt>
#include <locale>
#include <filesystem>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

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

InstallationWorker::InstallationWorker(HWND hNotifyWindow)
    : m_hNotifyWindow(hNotifyWindow)
    , m_running(false)
    , m_cancellationRequested(false)
    , m_autoRun(false)
    , m_desktopIcons(false)
    , m_repairMode(false)
    , m_cleanupOldInstallRequested(false)
    , m_totalBytes(0)
    , m_completedBytes(0)
    , m_currentFolderBytes(0)
    , m_currentBaseBytes(0) {
}

InstallationWorker::~InstallationWorker() {
    if (m_workerThread.joinable()) {
        RequestCancellation();
        m_workerThread.join();
    }
}

// ============================================================================
// NOTE: Comment text normalized to avoid encoding mojibake.
// ============================================================================

void InstallationWorker::StartInstallation(const std::wstring& installPath,
                                           bool autoRun,
                                           bool desktopIcons,
                                           const std::wstring& languageCode,
                                           bool repairMode,
                                           bool cleanupOldInstall,
                                           const std::vector<std::string>& selectedComponents) {
    if (m_running.load()) {
        return;
    }

    JoinFinishedThreadIfNeeded();
    m_cancellationRequested = false;
    m_running = true;
    m_autoRun = autoRun;
    m_desktopIcons = desktopIcons;
    m_languageCode = languageCode;
    m_repairMode = repairMode;
    m_cleanupOldInstallRequested = cleanupOldInstall;
    m_selectedComponents = selectedComponents;
    
    m_workerThread = std::thread(&InstallationWorker::WorkerThreadFunc, this, installPath);
}

void InstallationWorker::RequestCancellation() {
    m_cancellationRequested = true;
}

bool InstallationWorker::IsRunning() const {
    return m_running;
}

bool InstallationWorker::Joinable() const {
    return m_workerThread.joinable();
}

// ============================================================================
// NOTE: Comment text normalized to avoid encoding mojibake.
// ============================================================================

void InstallationWorker::ProgressCallback(const std::string& folder, const std::string& currentFile, float progress, void* userData) {
    // NOTE: Comment text normalized to avoid encoding mojibake.
    InstallationWorker* worker = static_cast<InstallationWorker*>(userData);
    if (worker) {
        // NOTE: Comment text normalized to avoid encoding mojibake.
        std::wstring wFolder = Utf8ToWide(folder);
        std::wstring wDisplay = wFolder;
        if (!currentFile.empty()) {
            wDisplay = Utf8ToWide(currentFile);
        }
        uint64_t total = worker->m_totalBytes.load();
        if (total == 0) {
            worker->PostProgressMessage(wDisplay, progress * 100.0f);
            return;
        }
        uint64_t base = worker->m_currentBaseBytes.load();
        uint64_t current = worker->m_currentFolderBytes.load();
        double overall = (static_cast<double>(base) + progress * static_cast<double>(current)) /
                         static_cast<double>(total);
        if (overall > 1.0) {
            overall = 1.0;
        }
        worker->PostProgressMessage(wDisplay, static_cast<float>(overall * 100.0));
    }
}

void InstallationWorker::PostProgressMessage(const std::wstring& folder, float progress) {
    ProgressMessageData* pData = new ProgressMessageData();

    size_t copyLen = folder.length();
    if (copyLen >= MAX_PATH) {
        copyLen = MAX_PATH - 1;
    }
    wcsncpy_s(pData->currentFolder, MAX_PATH, folder.c_str(), copyLen);
    pData->currentFolder[copyLen] = L'\0';

    pData->percentage = progress;

    PostOwnedWorkerMessage(m_hNotifyWindow, WM_INSTALLATION_PROGRESS, pData, "[GUI][Worker]");
}

void InstallationWorker::PostCompletionMessage(bool success,
                                               bool rebootRequired,
                                               const std::wstring& errorMsg) {
    CompletionMessageData* pData = new CompletionMessageData();

    pData->success = success;
    pData->rebootRequired = rebootRequired;

    size_t copyLen = errorMsg.length();
    if (copyLen >= 512) {
        copyLen = 511;
    }
    wcsncpy_s(pData->errorMessage, 512, errorMsg.c_str(), copyLen);
    pData->errorMessage[copyLen] = L'\0';

    PostOwnedWorkerMessage(m_hNotifyWindow, WM_INSTALLATION_COMPLETE, pData, "[GUI][Worker]");
}

// ============================================================================
// NOTE: Comment text normalized to avoid encoding mojibake.
// ============================================================================

void InstallationWorker::WorkerThreadFunc(const std::wstring& installPath) {
    bool success = false;
    bool rebootRequired = false;
    std::wstring errorMessage;
    ExtendedInstallationMetadata metadata;
    InstallerPathResolver pathResolver;
    auto startTime = std::chrono::steady_clock::now();
    auto logElapsed = [startTime](const char* label) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        logInstallerDebug(std::string("[GUI][timing] ") + label + " +" + std::to_string(ms) + "ms");
    };
    
    try {
        // NOTE: Comment text normalized to avoid encoding mojibake.
PostProgressMessage(
            GUIHelpers::GetLocalizedText(L"msg.progress.preparing", L""),
            0.0f);
        logInstallerInfo("[GUI] Installation started.");
        logElapsed("start");
        
        // NOTE: Comment text normalized to avoid encoding mojibake.
        MetadataParser parser;
        metadata = parser.parseExtendedEmbeddedMetadata();
        
        if (!parser.validateMetadata(metadata)) {
            throw std::runtime_error(WideToUtf8(
                GUIHelpers::GetLocalizedText(L"msg.error.metadata_invalid", L"")));
        }
        logInstallerInfo(std::string("[GUI] Metadata loaded. App=") + metadata.appName +
                         " folders=" + std::to_string(metadata.folderCount));
        logElapsed("metadata_loaded");

        metadata.installAutoStartup = m_autoRun;
        metadata.installDesktopIcon = m_desktopIcons;

        std::string installPathStr = WideToUtf8(installPath);
        if (requiresAdminForInstall(installPathStr, metadata, pathResolver) && !isRunningAsAdmin()) {
#ifdef _WIN32
            SetEnvironmentVariableW(L"MTINSTALLER_INSTALL_PATH", installPath.c_str());
#endif
            std::wstring adminError = relaunchSelfAsAdmin()
                ? GUIHelpers::GetLocalizedText(L"msg.error.require_admin_relaunch", L"")
                : GUIHelpers::GetLocalizedText(L"msg.error.require_admin", L"");
            throw std::runtime_error(WideToUtf8(adminError));
        }

        logElapsed("prechecks_complete");

        // NOTE: Comment text normalized to avoid encoding mojibake.
if (m_cancellationRequested) {
            throw std::runtime_error(WideToUtf8(
                GUIHelpers::GetLocalizedText(L"msg.error.cancelled", L"")));
        }
        
        // NOTE: Comment text normalized to avoid encoding mojibake.
        InstallServiceCallbacks serviceCallbacks;
        serviceCallbacks.onEvent = [this](const InstallServiceEvent& event) {
            switch (event.type) {
                case InstallServiceEventType::Progress: {
                    std::string detail = !event.currentFile.empty() ? event.currentFile : event.folder;
                    if (detail.empty()) {
                        detail = event.message;
                    }
                    if (detail.empty()) {
                        detail = "Installing";
                    }
                    const float progress = std::max(0.0f, std::min(100.0f, event.overallProgress * 100.0f));
                    PostProgressMessage(Utf8ToWide(detail), progress);
                    break;
                }
                case InstallServiceEventType::Info:
                    logInstallerInfo(std::string("[InstallService] ") + event.message);
                    break;
                case InstallServiceEventType::Warning:
                    logInstallerWarning(std::string("[InstallService] ") + event.message);
                    break;
                case InstallServiceEventType::Error:
                    logInstallerError(std::string("[InstallService] ") + event.message);
                    break;
                case InstallServiceEventType::Status:
                    if (!event.message.empty()) {
                        logInstallerInfo(std::string("[InstallService][Status] ") + event.message);
                    }
                    break;
                default:
                    break;
            }
        };
        InstallServiceOptions serviceOptions;
        serviceOptions.installPath = installPathStr;
        serviceOptions.installPathExplicit = true;
        serviceOptions.selectedComponentIds = m_selectedComponents;
        serviceOptions.languageCode = WideToUtf8(m_languageCode);
        serviceOptions.applyRegistryBeforeFinalize = true;
        serviceOptions.preRegistryInstallPath = installPathStr;
        serviceOptions.writeUninstallRegistry = true;
        serviceOptions.repairMode = m_repairMode;
        serviceOptions.cleanupOldInstallRequested = m_cleanupOldInstallRequested;
        serviceOptions.cancellationCallback = [this]() {
            return m_cancellationRequested.load();
        };

        logInstallerInfo("[GUI] Decompression engine initialized.");
        logElapsed("decompression_init");

        InstallServiceResult serviceResult = ExecuteInstallService(
            metadata,
            parser,
            pathResolver,
            serviceOptions,
            serviceCallbacks);

        logInstallerInfo(std::string("[GUI] Decompression complete. success=") +
                         (serviceResult.success ? "true" : "false"));
        logElapsed("decompression_complete");

        if (!serviceResult.success && !serviceResult.rebootRequired) {
            if (serviceResult.cancelled || m_cancellationRequested.load()) {
                throw std::runtime_error(WideToUtf8(
                    GUIHelpers::GetLocalizedText(L"msg.error.cancelled", L"")));
            }

            std::string allErrors;
            for (const auto& err : serviceResult.errors) {
                if (!allErrors.empty()) {
                    allErrors += "\n";
                }
                allErrors += err;
            }
            if (allErrors.empty()) {
                throw std::runtime_error(WideToUtf8(
                    GUIHelpers::GetLocalizedText(L"msg.error.install_failed", L"")));
            }
            throw std::runtime_error(allErrors);
        }

        success = serviceResult.success;
        rebootRequired = serviceResult.rebootRequired;
        errorMessage = L"";
        logInstallerInfo(std::string("[GUI] Installation completed. success=") +
                         (success ? "true" : "false") +
                         " rebootRequired=" + (rebootRequired ? "true" : "false"));
        logElapsed("success");
    } catch (const std::filesystem::filesystem_error& e) {
        success = false;
        std::wstring detail = GUIHelpers::GetLocalizedText(L"msg.error.install_failed", L"");
        detail += GUIHelpers::GetLocalizedText(L"msg.error.detail.error_code", L"");
        detail += std::to_wstring(static_cast<long long>(e.code().value()));
        if (!e.path1().empty()) {
            detail += GUIHelpers::GetLocalizedText(L"msg.error.detail.path1", L"");
            detail += e.path1().wstring();
        }
        if (!e.path2().empty()) {
            detail += GUIHelpers::GetLocalizedText(L"msg.error.detail.path2", L"");
            detail += e.path2().wstring();
        }
        errorMessage = detail;
        logInstallerError(std::string("[GUI] Installation failed (filesystem): ") + e.what());
        logElapsed("failed_fs");
    } catch (const std::exception& e) {
        // NOTE: Comment text normalized to avoid encoding mojibake.
success = false;
        if (m_cancellationRequested.load() || isCancellationText(e.what())) {
            errorMessage = GUIHelpers::GetLocalizedText(L"msg.error.cancelled", L"");
        } else {
            errorMessage = Utf8ToWide(e.what());
        }
        logInstallerError(std::string("[GUI] Installation failed: ") + e.what());
        logElapsed("failed");
    } catch (...) {
        // NOTE: Comment text normalized to avoid encoding mojibake.
        success = false;
        errorMessage = GUIHelpers::GetLocalizedText(L"msg.error.unknown", L"");
        logInstallerError("[GUI] Installation failed: unknown error.");
        logElapsed("failed_unknown");
    }
    
    m_running = false;
    PostCompletionMessage(success, rebootRequired, errorMessage);
}

void InstallationWorker::JoinFinishedThreadIfNeeded() {
    if (m_workerThread.joinable() && !m_running.load()) {
        m_workerThread.join();
    }
}

// ============================================================================
// NOTE: Comment text normalized to avoid encoding mojibake.
// ============================================================================


} // namespace MultiThreadedInstaller











