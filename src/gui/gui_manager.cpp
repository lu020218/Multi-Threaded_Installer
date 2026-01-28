#ifdef GUI_ENABLED

#include "../../include/gui/gui_manager.h"
#include "../../include/gui/page_controller.h"
#include "../../include/gui/gui_helpers.h"
#include "../../include/gui/uninstall_worker.h"
#include "../../include/installer/metadata_parser.h"
#include "../../include/installer/path_resolver.h"
#include "../../include/installer/installer_helpers.h"
#include "../../include/installer/uninstall_manager.h"
#include "../../include/installer/registry_utils.h"
#include "Utils/unzip.h"
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cwctype>
#ifdef _WIN32
#include <Windows.h>
#endif

using namespace DuiLib;

namespace MultiThreadedInstaller {

static constexpr int kPageWelcome = static_cast<int>(PageType::Welcome);
static constexpr int kPageLicense = static_cast<int>(PageType::License);
static constexpr int kPageProgress = static_cast<int>(PageType::Progress);
static constexpr int kPageCompletion = static_cast<int>(PageType::Completion);
static constexpr UINT_PTR kProgressTimerId = 1001;
static constexpr UINT kProgressTimerIntervalMs = 33;

// Convert wstring to LPCTSTR for Unicode/MBCS builds.
// Returns a static buffer that's valid until next call.
static LPCTSTR WStringToTStr(const std::wstring& wstr) {
#ifdef UNICODE
    static thread_local std::vector<std::wstring> stringPool;
#else
    static thread_local std::vector<std::string> stringPool;
#endif
    static thread_local size_t poolIndex = 0;

    if (stringPool.size() < 10) {
        stringPool.resize(10);
    }

#ifdef UNICODE
    std::wstring& result = stringPool[poolIndex];
#else
    std::string& result = stringPool[poolIndex];
#endif
    poolIndex = (poolIndex + 1) % stringPool.size();

#ifdef UNICODE
    result = wstr;
    return result.c_str();
#else
    if (wstr.empty()) {
        result.clear();
    } else {
        int size = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (size > 0) {
            result.resize(size - 1);
            WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
        } else {
            result.clear();
        }
    }
    return result.c_str();
#endif
}

static std::string WStringToUtf8(const std::wstring& wstr);
static std::wstring Utf8ToWString(const std::string& str);
static std::wstring ToLowerString(const std::wstring& value);
static int GetDefaultLanguageComboIndex();
static std::wstring GetLanguageCodeForIndex(int index);
static int GetLanguageIndexForCode(const std::wstring& code);
static std::wstring GetLanguageFilePath(const std::wstring& code);
static std::string NormalizePathForCompare(const std::string& path);
static bool HandleRunningApplicationDialog(HWND hWnd, const std::string& appName);
static bool RequestPreviousInstallCleanup(HWND hWnd,
                                          const ExtendedInstallationMetadata& metadata,
                                          const std::wstring& installPath);

static UINT GetDpiForWindowSafe(HWND hwnd) {
#ifdef _WIN32
    typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (fn && hwnd) {
            return fn(hwnd);
        }
        if (!hwnd) {
            auto getSystemDpi = reinterpret_cast<UINT(WINAPI*)(void)>(
                GetProcAddress(user32, "GetDpiForSystem"));
            if (getSystemDpi) {
                return getSystemDpi();
            }
        }
    }

    if (!hwnd) {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            typedef HRESULT(WINAPI* GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
            auto getDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(
                GetProcAddress(shcore, "GetDpiForMonitor"));
            if (getDpiForMonitor) {
                POINT pt = {0, 0};
                HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
                UINT dpiX = 96;
                UINT dpiY = 96;
                if (SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY))) {
                    FreeLibrary(shcore);
                    return dpiX ? dpiX : 96;
                }
            }
            FreeLibrary(shcore);
        }
    }
    HDC screen = GetDC(NULL);
    if (!screen) {
        return 96;
    }
    int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
#else
    return 96;
#endif
}

static std::wstring ExpandEnvVars(const std::wstring& value) {
#ifdef _WIN32
    if (value.find(L'%') == std::wstring::npos) {
        return value;
    }
    wchar_t buffer[MAX_PATH];
    DWORD len = ExpandEnvironmentStringsW(value.c_str(), buffer, MAX_PATH);
    if (len == 0 || len > MAX_PATH) {
        return value;
    }
    return std::wstring(buffer);
#else
    return value;
#endif
}

static std::wstring NormalizeInstallPath(const std::wstring& basePath,
                                         const std::wstring& appName) {
    if (basePath.empty() || appName.empty()) {
        return basePath;
    }

    std::filesystem::path selectedFs = basePath;
    std::wstring appNameLower = ToLowerString(appName);
    std::wstring selectedNameLower = ToLowerString(selectedFs.filename().wstring());

    if (selectedNameLower == appNameLower) {
        return selectedFs.wstring();
    }

    std::filesystem::path childPath = selectedFs / appName;
    return childPath.wstring();
}

static std::wstring LoadLicenseTextFromResources() {
    if (CPaintManagerUI::GetResourceType() == UILIB_ZIP &&
        !CPaintManagerUI::GetResourceZip().IsEmpty()) {
        CDuiString basePath = CPaintManagerUI::GetResourcePath();
        CDuiString zipName = CPaintManagerUI::GetResourceZip();
        CDuiString zipPath = basePath + zipName;

        HZIP hz = OpenZip(zipPath.GetData(), 0);
        if (hz != NULL) {
            ZIPENTRY ze;
            int index = 0;
            if (FindZipItem(hz, _T("license.txt"), true, &index, &ze) == 0) {
                std::vector<char> buffer(static_cast<size_t>(ze.unc_size));
                if (UnzipItem(hz, index, buffer.data(), ze.unc_size) == 0) {
                    CloseZip(hz);
                    std::string text(buffer.begin(), buffer.end());
                    return Utf8ToWString(text);
                }
            }
            CloseZip(hz);
        }
    }

    CDuiString resourcePath = CPaintManagerUI::GetResourcePath();
    std::filesystem::path licensePath = std::filesystem::path(resourcePath.GetData()) / "license.txt";
    std::ifstream file(licensePath, std::ios::binary);
    if (!file.is_open()) {
        std::filesystem::path fallback = std::filesystem::path(resourcePath.GetData()) / ".." / "license.txt";
        file.open(fallback, std::ios::binary);
    }
    if (!file.is_open()) {
        return GUIHelpers::GetLocalizedText(
            L"msg.dialog.license_not_impl",
            L"License text not found.");
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return Utf8ToWString(content);
}

int GUIManager::GetWelcomePageIndex() const {
    return m_uninstallMode ? 0 : kPageWelcome;
}

int GUIManager::GetProgressPageIndex() const {
    return m_uninstallMode ? 1 : kPageProgress;
}

int GUIManager::GetCompletionPageIndex() const {
    return m_uninstallMode ? 2 : kPageCompletion;
}

GUIManager::GUIManager()
      : m_pTabPages(nullptr),
        m_pInstallPathEdit(nullptr),
        m_pLicenseCheckbox(nullptr),
        m_pInstallButton(nullptr),
        m_pDiskSpaceLabel(nullptr),
        m_pConfigBottom(nullptr),
        m_pMoreInfo(nullptr),
        m_pPageController(nullptr),
        m_pWorker(nullptr),
        m_pUninstallWorker(nullptr),
        m_baseClientHeight(0),
        m_baseClientWidth(0),
        m_expandedClientHeight(0),
        m_baseWindowWidth(0),
        m_uninstallMode(false),
        m_progressTarget(0.0f),
        m_progressDisplayed(0.0f),
        m_progressTimerActive(false),
        m_progressLastTick(0) {
      m_pm.GetDPIObj()->SetScale(static_cast<int>(GetDpiForWindowSafe(nullptr)));
  }

GUIManager::~GUIManager() {
    if (m_pPageController) {
        delete m_pPageController;
        m_pPageController = nullptr;
    }
    
    if (m_pWorker) {
        delete m_pWorker;
        m_pWorker = nullptr;
    }

    if (m_pUninstallWorker) {
        delete m_pUninstallWorker;
        m_pUninstallWorker = nullptr;
    }
}

void GUIManager::SetInstallConfig(const InstallConfig& config) {
    m_config = config;
}

CDuiString GUIManager::GetSkinFolder() {
    return _T("skins\\");
}

CDuiString GUIManager::GetSkinFile() {
    if (m_uninstallMode) {
        return _T("skins\\uninstall_main.xml");
    }
    return _T("skins\\main.xml");
}

LPCTSTR GUIManager::GetWindowClassName() const {
    return _T("InstallerMainWindow");
}

void GUIManager::InitWindow() {
    int windowDpi = static_cast<int>(GetDpiForWindowSafe(m_hWnd));
    if (windowDpi > 0 && windowDpi != m_pm.GetDPIObj()->GetScale()) {
        m_pm.SetDPI(windowDpi);
        m_pm.ReloadImages();
        m_pm.Invalidate();
    }
    InitControls();
    
    if (m_pTabPages && !m_uninstallMode) {
        m_pPageController = new PageController(m_pTabPages);
    }
    if (m_uninstallMode && m_pTabPages) {
        m_pTabPages->SelectItem(GetWelcomePageIndex());
    }

    std::cout << "GUI mode uninstall=" << (m_uninstallMode ? "true" : "false") << std::endl;
    
    std::wstring installPath = ExpandEnvVars(m_config.defaultInstallPath);
#ifdef _WIN32
    wchar_t envPath[MAX_PATH];
    DWORD envLen = GetEnvironmentVariableW(L"MTINSTALLER_INSTALL_PATH", envPath, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH) {
        installPath = envPath;
        SetEnvironmentVariableW(L"MTINSTALLER_INSTALL_PATH", nullptr);
    }
#endif
    if (!m_config.registryPath.empty() && !m_config.registryKey.empty()) {
        std::string regPath = WStringToUtf8(m_config.registryPath);
        std::string regKey = WStringToUtf8(m_config.registryKey);
        std::string regValue;
        if (readRegistryStringValue(regPath, regKey, regValue)) {
            std::wstring regPathW = Utf8ToWString(regValue);
            if (!regPathW.empty()) {
                installPath = regPathW;
            }
        }
    }

    installPath = NormalizeInstallPath(installPath, m_config.applicationName);

    if (m_pInstallPathEdit) {
        m_pInstallPathEdit->SetText(WStringToTStr(installPath));
    }

    UpdateDiskSpaceInfo(installPath);
    
    UpdateInstallButtonState();
    
    if (m_pInstallPathEdit) {
        m_pInstallPathEdit->SetFocus();
    }
    

    if (m_baseClientHeight == 0 || m_baseClientWidth == 0 || m_baseWindowWidth == 0) {
        RECT rcClient;
        ::GetClientRect(m_hWnd, &rcClient);
        m_baseClientHeight = rcClient.bottom - rcClient.top;
        m_baseClientWidth = rcClient.right - rcClient.left;

        RECT rcWindow;
        ::GetWindowRect(m_hWnd, &rcWindow);
        m_baseWindowWidth = rcWindow.right - rcWindow.left;
    }
    CenterWindow();
}

void GUIManager::InitControls() {
    m_pTabPages = static_cast<CTabLayoutUI*>(
        m_pm.FindControl(_T("pages")));
    
    m_pInstallPathEdit = static_cast<CEditUI*>(
        m_pm.FindControl(_T("install_path")));
    if (!m_pInstallPathEdit) {
        m_pInstallPathEdit = static_cast<CEditUI*>(
            m_pm.FindControl(_T("editDir")));
    }
    
    m_pLicenseCheckbox = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("license_checkbox")));
    
    m_pInstallButton = static_cast<CButtonUI*>(
        m_pm.FindControl(_T("install_button")));
    
    m_pDiskSpaceLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("disk_space_info")));

    m_pConfigBottom = static_cast<CContainerUI*>(
        m_pm.FindControl(_T("config-bottom")));

    m_pMoreInfo = static_cast<CContainerUI*>(
        m_pm.FindControl(_T("moreinfo")));

    CComboUI* pLangCombo = static_cast<CComboUI*>(
        m_pm.FindControl(_T("comboLanguageSelect")));
    std::wstring languageCode = m_config.languageCode;
    if (languageCode.empty()) {
        languageCode = GetLanguageCodeForIndex(GetDefaultLanguageComboIndex());
    }
    if (pLangCombo) {
        int selectedIndex = GetLanguageIndexForCode(languageCode);
        pLangCombo->SelectItem(selectedIndex, false);
        ApplyLanguageByIndex(selectedIndex);
    } else {
        ApplyLanguageByCode(languageCode);
    }

    CCheckBoxUI* pAutoRun = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("chkAutoRun")));
    if (pAutoRun) {
        pAutoRun->SetCheck(m_config.autoStartup);
    }

    CCheckBoxUI* pShortcut = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("chkShotcut")));
    if (pShortcut) {
        pShortcut->SetCheck(m_config.desktopIcons);
    }
    
    CLabelUI* pAppName = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_name")));
    if (pAppName) {
        pAppName->SetText(WStringToTStr(m_config.applicationName));
    }
    
    CLabelUI* pAppVersion = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version")));
    if (pAppVersion) {
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.version.prefix",
            L"Version ");
        std::wstring versionText = prefix + m_config.version;
        pAppVersion->SetText(WStringToTStr(versionText));
    }
    
    CLabelUI* pAppNameProgress = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_name_progress")));
    if (pAppNameProgress) {
        pAppNameProgress->SetText(WStringToTStr(m_config.applicationName));
    }
    
    CLabelUI* pAppVersionProgress = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version_progress")));
    if (pAppVersionProgress) {
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.version.prefix",
            L"Version ");
        std::wstring versionText = prefix + m_config.version;
        pAppVersionProgress->SetText(WStringToTStr(versionText));
    }
    
    CLabelUI* pAppNameCompletion = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_name_completion")));
    if (pAppNameCompletion) {
        pAppNameCompletion->SetText(WStringToTStr(m_config.applicationName));
    }
    
    CLabelUI* pAppVersionCompletion = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version_completion")));
    if (pAppVersionCompletion) {
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.version.prefix",
            L"Version ");
        std::wstring versionText = prefix + m_config.version;
        pAppVersionCompletion->SetText(WStringToTStr(versionText));
    }

    CLabelUI* pAppNameUninstall = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_name_uninstall")));
    if (pAppNameUninstall) {
        pAppNameUninstall->SetText(WStringToTStr(m_config.applicationName));
    }

    CLabelUI* pAppVersionUninstall = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version_uninstall")));
    if (pAppVersionUninstall) {
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.version.prefix",
            L"Version ");
        std::wstring versionText = prefix + m_config.version;
        pAppVersionUninstall->SetText(WStringToTStr(versionText));
    }
    
    if (m_pTabPages) {
        m_pTabPages->SelectItem(GetWelcomePageIndex());
    }
}

void GUIManager::Notify(TNotifyUI& msg) {
    if (msg.sType == _T("click")) {
        CDuiString senderName = msg.pSender->GetName();
        
        if (senderName == _T("install_button") || senderName == _T("btnInstall")) {
            OnInstallButtonClick();
        }
        else if (senderName == _T("cancel_button")) {
            OnCancelButtonClick();
        }
        else if (senderName == _T("browse_button")) {
            OnBrowseButtonClick();
        }
        else if (senderName == _T("btnSelectDir")) {
            OnBrowseButtonClick();
        }
        else if (senderName == _T("finish_button")) {
            OnFinishButtonClick();
        }
        else if (senderName == _T("btnRun")) {
            OnFinishButtonClick();
        }
        else if (senderName == _T("btnClose")) {
            OnCancelButtonClick();
        }
        else if (senderName == _T("cancel_progress_button")) {
            OnCancelProgressButtonClick();
        }
        else if (senderName == _T("closebtn") || senderName == _T("close_button")) {
            OnCancelButtonClick();
        }
        else if (senderName == _T("minbtn")) {
            SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
        }
        else if (senderName == _T("btnShowMore")) {
            OnShowMoreClick();
        }
        else if (senderName == _T("btnAgreement")) {
            OnLicenseLinkClick();
        }
        else if (senderName == _T("btnBack")) {
            OnLicenseBackClick();
        }
        else if (senderName == _T("btnUninstallConfirm")) {
            OnUninstallConfirmClick();
        }
        else if (senderName == _T("btnUninstallCancel")) {
            Close();
        }
        else if (senderName == _T("btnUninstallFinish")) {
            Close();
        }
    }
    else if (msg.sType == _T("selectchanged")) {
        CDuiString senderName = msg.pSender->GetName();
        
        if (senderName == _T("license_checkbox")) {
            OnLicenseCheckboxChanged();
        } else if (senderName == _T("chkAgree1")) {
            SyncLicenseAgreementFromPage();
        }
    }
    else if (msg.sType == _T("itemselect")) {
        CDuiString senderName = msg.pSender->GetName();
        if (senderName == _T("comboLanguageSelect")) {
            int index = static_cast<int>(msg.wParam);
            ApplyLanguageByIndex(index);
        }
    }
    else if (msg.sType == _T("link")) {
        CDuiString senderName = msg.pSender->GetName();
        
        if (senderName == _T("license_link")) {
            OnLicenseLinkClick();
        }
    }
}

LRESULT GUIManager::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
#ifdef _WIN32
    if (uMsg == WM_DPICHANGED) {
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) {
            ::SetWindowPos(m_hWnd, NULL,
                           suggested->left,
                           suggested->top,
                           suggested->right - suggested->left,
                           suggested->bottom - suggested->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
        }
        UINT dpi = LOWORD(wParam);
        if (dpi == 0) {
            dpi = GetDpiForWindowSafe(m_hWnd);
        }
        m_pm.SetDPI(static_cast<int>(dpi));
        m_pm.ResetDPIAssets();
        m_pm.NeedUpdate();
        m_pm.Invalidate();
        RECT rcClient;
        ::GetClientRect(m_hWnd, &rcClient);
        m_baseClientHeight = rcClient.bottom - rcClient.top;
        m_baseClientWidth = rcClient.right - rcClient.left;
        RECT rcWindow;
        ::GetWindowRect(m_hWnd, &rcWindow);
        m_baseWindowWidth = rcWindow.right - rcWindow.left;
        return 0;
    }
#endif
    if (uMsg == WM_CLOSE) {
        DestroyWindow(m_hWnd);
        PostQuitMessage(0);
        return 0;
    }
    if (uMsg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (uMsg == WM_TIMER && wParam == kProgressTimerId) {
        TickProgressAnimation();
        return 0;
    }
    if (uMsg == WM_INSTALLATION_PROGRESS) {
        ProgressMessageData* pData = reinterpret_cast<ProgressMessageData*>(lParam);
        if (pData) {
            HandleProgressMessage(pData);
            delete pData;
        }
        return 0;
    }
    else if (uMsg == WM_INSTALLATION_COMPLETE) {
        CompletionMessageData* pData = reinterpret_cast<CompletionMessageData*>(lParam);
        if (pData) {
            HandleCompletionMessage(pData);
            delete pData;
        }
        return 0;
    }
    else if (uMsg == WM_UNINSTALL_COMPLETE) {
        CompletionMessageData* pData = reinterpret_cast<CompletionMessageData*>(lParam);
        if (pData) {
            HandleUninstallCompletionMessage(pData);
            delete pData;
        }
        return 0;
    }
    else if (uMsg == WM_KEYDOWN) {
        int currentPage = 0;
        if (m_pTabPages) {
            currentPage = m_pTabPages->GetCurSel();
        }
        
        bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        
        if (altPressed) {
            if (wParam == 'I' && currentPage == GetWelcomePageIndex()) {
                if (m_pInstallButton && m_pInstallButton->IsEnabled()) {
                    OnInstallButtonClick();
                    return 0;
                }
            }
            else if (wParam == 'C' && (currentPage == GetWelcomePageIndex() ||
                                       (!m_uninstallMode && currentPage == kPageLicense) ||
                                       currentPage == GetProgressPageIndex())) {
                if (currentPage == GetWelcomePageIndex() ||
                    (!m_uninstallMode && currentPage == kPageLicense)) {
                    OnCancelButtonClick();
                } else {
                    OnCancelProgressButtonClick();
                }
                return 0;
            }
            else if (wParam == 'F' && currentPage == GetCompletionPageIndex()) {
                OnFinishButtonClick();
                return 0;
            }
        }
        else {
            if (wParam == VK_RETURN) {
                if (currentPage == GetWelcomePageIndex()) {
                    if (m_pInstallButton && m_pInstallButton->IsEnabled()) {
                        OnInstallButtonClick();
                        return 0;
                    }
                }
                else if (currentPage == GetCompletionPageIndex()) {
                    OnFinishButtonClick();
                    return 0;
                }
            }
            else if (wParam == VK_ESCAPE) {
                if (currentPage == GetWelcomePageIndex() ||
                    (!m_uninstallMode && currentPage == kPageLicense)) {
                    OnCancelButtonClick();
                    return 0;
                }
                else if (currentPage == GetProgressPageIndex()) {
                    OnCancelProgressButtonClick();
                    return 0;
                }
                else if (currentPage == GetCompletionPageIndex()) {
                    OnFinishButtonClick();
                    return 0;
                }
            }
        }
    }
    
    return WindowImplBase::HandleMessage(uMsg, wParam, lParam);
}

void GUIManager::OnInstallButtonClick() {
    CCheckBoxUI* pAgree = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("chkAgree")));
    if (pAgree && !pAgree->GetCheck()) {
        GUIHelpers::ShowWarningDialog(
            m_hWnd,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L"Prompt"),
            GUIHelpers::GetLocalizedText(L"msg.dialog.agree_required",
                                         L"Please accept the license agreement first."));
        return;
    }

    if (!m_pPageController) {
        GUIHelpers::ShowErrorDialog(
            m_hWnd,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L"Error"),
            GUIHelpers::GetLocalizedText(L"msg.dialog.install_controller_missing",
                                         L"Installation controller unavailable."));
        return;
    }

    std::wstring installPath;
    if (m_pInstallPathEdit) {
        installPath = m_pInstallPathEdit->GetText().GetData();
    }
    if (installPath.empty()) {
        GUIHelpers::ShowWarningDialog(
            m_hWnd,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.warning", L"Warning"),
            GUIHelpers::GetLocalizedText(L"msg.dialog.select_install_dir",
                                         L"Please select an installation directory."));
        return;
    }

    StopProgressTimer();
    m_progressTarget = 0.0f;
    m_progressDisplayed = 0.0f;
    m_progressFolder.clear();
    UpdateProgressDisplay(0.0f);

    bool autoRun = false;
    bool desktopIcons = false;
    if (auto* pAutoRun = static_cast<CCheckBoxUI*>(m_pm.FindControl(_T("chkAutoRun")))) {
        autoRun = pAutoRun->GetCheck();
    }
    if (auto* pShortcut = static_cast<CCheckBoxUI*>(m_pm.FindControl(_T("chkShotcut")))) {
        desktopIcons = pShortcut->GetCheck();
    }

    CollapseConfigIfExpanded();
    std::wstring languageCode = GetLanguageCodeForIndex(GetDefaultLanguageComboIndex());
    if (auto* pLangCombo = static_cast<CComboUI*>(m_pm.FindControl(_T("comboLanguageSelect")))) {
        int index = pLangCombo->GetCurSel();
        if (index >= 0) {
            languageCode = GetLanguageCodeForIndex(index);
        }
    } else if (!m_config.languageCode.empty()) {
        languageCode = m_config.languageCode;
    }

    ExtendedInstallationMetadata metadata;
    try {
        MetadataParser parser;
        metadata = parser.parseExtendedEmbeddedMetadata();
        if (!parser.validateMetadata(metadata)) {
            GUIHelpers::ShowErrorDialog(
                m_hWnd,
                GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L"Error"),
                GUIHelpers::GetLocalizedText(L"msg.dialog.metadata_invalid",
                                             L"Installer metadata is invalid or corrupted."));
            return;
        }
    } catch (const std::exception& ex) {
        std::cout << "Failed to parse installer metadata: " << ex.what() << std::endl;
        GUIHelpers::ShowErrorDialog(
            m_hWnd,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L"Error"),
            GUIHelpers::GetLocalizedText(L"msg.dialog.metadata_read_failed",
                                         L"Failed to read installer metadata."));
        return;
    }

    if (!HandleRunningApplicationDialog(m_hWnd, metadata.applicationName)) {
        return;
    }

    bool cleanupOldInstall = RequestPreviousInstallCleanup(m_hWnd, metadata, installPath);

    m_pPageController->StartInstallation(installPath, autoRun, desktopIcons, languageCode,
                                         cleanupOldInstall, m_hWnd);

    if (m_pTabPages) {
        m_pTabPages->SelectItem(GetProgressPageIndex());
    }
}

void GUIManager::OnUninstallConfirmClick() {
    if (!m_pTabPages) {
        return;
    }

    m_pTabPages->SelectItem(GetProgressPageIndex());

    if (!m_pUninstallWorker) {
        m_pUninstallWorker = new UninstallWorker(m_hWnd);
    }

    std::string appName = WStringToUtf8(m_config.applicationName);
    if (appName.empty()) {
        CompletionMessageData* pData = new CompletionMessageData();
        pData->success = false;
        std::wstring text = GUIHelpers::GetLocalizedText(
            L"msg.uninstall.appname_missing",
            L"Application name missing; cannot uninstall.");
        wcsncpy_s(pData->errorMessage, text.c_str(), _TRUNCATE);
        ::PostMessage(m_hWnd, WM_UNINSTALL_COMPLETE, 0, reinterpret_cast<LPARAM>(pData));
        return;
    }

    m_pUninstallWorker->StartUninstall(appName);
}

void GUIManager::OnCancelButtonClick() {
    std::wstring title = GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L"Exit");
    std::wstring message = GUIHelpers::GetLocalizedText(L"msg.dialog.exit_confirm",
                                                        L"Exit the installer?");
    if (GUIHelpers::ShowConfirmDialog(m_hWnd, title, message)) {
        Close();
    }
}

void GUIManager::OnBrowseButtonClick() {
    std::wstring currentPath;
    if (m_pInstallPathEdit) {
        currentPath = m_pInstallPathEdit->GetText().GetData();
    }
    
    std::wstring selectedPath;
    if (GUIHelpers::ShowFolderBrowserDialog(
        m_hWnd,
        GUIHelpers::GetLocalizedText(L"msg.dialog.select_dir_title",
                                     L"Select installation directory"),
        currentPath,
        selectedPath)) {
        
        std::wstring finalPath = selectedPath;
        if (!m_config.applicationName.empty()) {
            std::filesystem::path selectedFs = selectedPath;
            std::wstring appNameLower = ToLowerString(m_config.applicationName);
            std::wstring selectedNameLower = ToLowerString(selectedFs.filename().wstring());

            if (selectedNameLower == appNameLower) {
                finalPath = selectedFs.wstring();
            } else {
                std::filesystem::path childPath = selectedFs / m_config.applicationName;
                std::error_code ec;
                if (std::filesystem::exists(childPath, ec) &&
                    std::filesystem::is_directory(childPath, ec)) {
                    finalPath = childPath.wstring();
                } else {
                    finalPath = childPath.wstring();
                }
            }
        }

        if (m_pInstallPathEdit) {
            m_pInstallPathEdit->SetText(WStringToTStr(finalPath));
        }
        
        UpdateDiskSpaceInfo(finalPath);
        
        UpdateInstallButtonState();
    }
}

void GUIManager::OnLicenseLinkClick() {
    ShowLicensePage();
}

void GUIManager::OnLicenseBackClick() {
    SyncLicenseAgreementFromPage();
    if (m_pTabPages) {
        m_pTabPages->SelectItem(kPageWelcome);
    }
}

void GUIManager::ShowLicensePage() {
    if (!m_pTabPages) {
        return;
    }

    CRichEditUI* pLicenseText = static_cast<CRichEditUI*>(
        m_pm.FindControl(_T("editLicense")));
    if (pLicenseText) {
        std::wstring text = LoadLicenseTextFromResources();
        pLicenseText->SetText(WStringToTStr(text));
        pLicenseText->SetReadOnly(true);
    }

    CCheckBoxUI* pAgreeInline = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("chkAgree1")));
    if (pAgreeInline) {
        bool checked = false;
        if (auto* pAgree = static_cast<CCheckBoxUI*>(m_pm.FindControl(_T("chkAgree")))) {
            checked = pAgree->GetCheck();
        } else if (m_pLicenseCheckbox) {
            checked = m_pLicenseCheckbox->GetCheck();
        }
        pAgreeInline->SetCheck(checked);
    }

    m_pTabPages->SelectItem(kPageLicense);
}

void GUIManager::SyncLicenseAgreementFromPage() {
    CCheckBoxUI* pAgreeInline = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("chkAgree1")));
    if (!pAgreeInline) {
        return;
    }

    bool checked = pAgreeInline->GetCheck();
    if (auto* pAgree = static_cast<CCheckBoxUI*>(m_pm.FindControl(_T("chkAgree")))) {
        pAgree->SetCheck(checked);
    }
    if (m_pLicenseCheckbox) {
        m_pLicenseCheckbox->SetCheck(checked);
    }
    UpdateInstallButtonState();
}

void GUIManager::OnFinishButtonClick() {
    CCheckBoxUI* pRunAppCheckbox = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("run_app_checkbox")));
    bool shouldRun = true;
    if (pRunAppCheckbox) {
        shouldRun = pRunAppCheckbox->GetCheck();
    }
    if (shouldRun) {
        std::wstring installPath;
        if (m_pInstallPathEdit) {
            installPath = m_pInstallPathEdit->GetText().GetData();
        }
        
        if (!installPath.empty() && !m_config.executableName.empty()) {
            std::wstring exePath = installPath;
            if (exePath.back() != L'\\' && exePath.back() != L'/') {
                exePath += L"\\";
            }
            exePath += m_config.executableName;
            if (!GUIHelpers::LaunchApplication(exePath, installPath)) {
                GUIHelpers::ShowWarningDialog(
                    m_hWnd,
                    GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L"Prompt"),
                    GUIHelpers::GetLocalizedText(L"msg.dialog.launch_failed",
                                                 L"Unable to launch application, please run manually."));
            }
        }
    }
    
    CCheckBoxUI* pOpenWebCheckbox = static_cast<CCheckBoxUI*>(
        m_pm.FindControl(_T("open_web_checkbox")));
    if (pOpenWebCheckbox && pOpenWebCheckbox->GetCheck()) {
        if (!m_config.webPageUrl.empty()) {
            if (!GUIHelpers::OpenWebPage(m_config.webPageUrl)) {
                GUIHelpers::ShowWarningDialog(
                    m_hWnd,
                    GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L"Prompt"),
                    GUIHelpers::GetLocalizedText(L"msg.dialog.web_failed",
                                                 L"Unable to open webpage, please visit manually."));
            }
        }
    }
    
    Close();
}

void GUIManager::OnCancelProgressButtonClick() {
    std::wstring title = GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L"Cancel");
    std::wstring message = GUIHelpers::GetLocalizedText(L"msg.dialog.cancel_install_confirm",
                                                        L"Cancel installation and exit?");
    if (GUIHelpers::ShowConfirmDialog(m_hWnd, title, message)) {
        // TODO: request cancellation from worker.
        // if (m_pWorker) {
        //     m_pWorker->RequestCancellation();
        // }
    }
}

void GUIManager::OnShowMoreClick() {
    if (!m_pConfigBottom || !m_pMoreInfo) {
        return;
    }

    bool show = !m_pConfigBottom->IsVisible();
    m_pConfigBottom->SetVisible(show);
    m_pMoreInfo->SetVisible(show);

    int targetHeight = m_baseClientHeight;
    if (show) {
        int fixedHeight = m_pConfigBottom->GetFixedHeight();
        if (fixedHeight > 0) {
            RECT pad = m_pConfigBottom->GetPadding();
            targetHeight = m_baseClientHeight + fixedHeight + pad.top + pad.bottom;
        }
    }

    RECT rcWindow = {};
    RECT rcClient = {};
    ::GetWindowRect(m_hWnd, &rcWindow);
    ::GetClientRect(m_hWnd, &rcClient);

    int windowWidth = m_baseWindowWidth > 0 ? m_baseWindowWidth : (rcWindow.right - rcWindow.left);
    int nonClientHeight = (rcWindow.bottom - rcWindow.top) - (rcClient.bottom - rcClient.top);
    int targetWindowHeight = targetHeight + nonClientHeight;

    ::SetWindowPos(
        m_hWnd,
        NULL,
        rcWindow.left,
        rcWindow.top,
        windowWidth,
        targetWindowHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);
    std::cout << "btnShowMore toggled=" << (show ? "true" : "false")
               << " base=" << m_baseClientWidth << "x" << m_baseClientHeight
               << " targetWindow=" << windowWidth << "x" << targetWindowHeight
               << std::endl;
    m_pm.NeedUpdate();
    m_pm.Invalidate();
}

void GUIManager::CollapseConfigIfExpanded() {
    if (!m_pConfigBottom || !m_pMoreInfo) {
        return;
    }

    if (!m_pConfigBottom->IsVisible() && !m_pMoreInfo->IsVisible()) {
        return;
    }

    m_pConfigBottom->SetVisible(false);
    m_pMoreInfo->SetVisible(false);

    RECT rcWindow = {};
    RECT rcClient = {};
    ::GetWindowRect(m_hWnd, &rcWindow);
    ::GetClientRect(m_hWnd, &rcClient);

    int windowWidth = m_baseWindowWidth > 0 ? m_baseWindowWidth : (rcWindow.right - rcWindow.left);
    int nonClientHeight = (rcWindow.bottom - rcWindow.top) - (rcClient.bottom - rcClient.top);
    int targetWindowHeight = m_baseClientHeight + nonClientHeight;

    ::SetWindowPos(
        m_hWnd,
        NULL,
        rcWindow.left,
        rcWindow.top,
        windowWidth,
        targetWindowHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);
    m_pm.NeedUpdate();
    m_pm.Invalidate();
}

static std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

static std::wstring Utf8ToWString(const std::string& str) {
    if (str.empty()) {
        return {};
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstrTo[0], size_needed);
    return wstrTo;
}

static std::wstring ToLowerString(const std::wstring& value) {
    std::wstring result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return result;
}

static int GetDefaultLanguageComboIndex() {
#ifdef _WIN32
    LANGID langId = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(langId)) {
        case LANG_CHINESE:
            return 0;
        case LANG_ENGLISH:
            return 1;
        case LANG_JAPANESE:
            return 2;
        case LANG_KOREAN:
            return 3;
        case LANG_SPANISH:
            return 4;
        case LANG_FRENCH:
            return 5;
        default:
            return 1;
    }
#else
    return 1;
#endif
}

static std::wstring GetLanguageCodeForIndex(int index) {
    switch (index) {
        case 0:
            return L"zh_CN";
        case 1:
            return L"en_US";
        case 2:
            return L"ja_JP";
        case 3:
            return L"ko_KR";
        case 4:
            return L"es_ES";
        case 5:
            return L"fr_FR";
        default:
            return L"en_US";
    }
}

static int GetLanguageIndexForCode(const std::wstring& code) {
    std::wstring lower = ToLowerString(code);
    if (lower == L"zh_cn" || lower == L"zh-cn" || lower == L"zh") {
        return 0;
    }
    if (lower == L"en_us" || lower == L"en-us" || lower == L"en") {
        return 1;
    }
    if (lower == L"ja_jp" || lower == L"ja-jp" || lower == L"ja") {
        return 2;
    }
    if (lower == L"ko_kr" || lower == L"ko-kr" || lower == L"ko") {
        return 3;
    }
    if (lower == L"es_es" || lower == L"es-es" || lower == L"es") {
        return 4;
    }
    if (lower == L"fr_fr" || lower == L"fr-fr" || lower == L"fr") {
        return 5;
    }
    return 1;
}

static std::wstring GetLanguageFilePath(const std::wstring& code) {
    CDuiString resourcePath = CPaintManagerUI::GetResourcePath();
    if (resourcePath.IsEmpty()) {
        return L"";
    }
    std::filesystem::path resPath(resourcePath.GetData());
    if (resPath.filename().empty()) {
        resPath = resPath.parent_path();
    }
    std::wstring tail = ToLowerString(resPath.filename().wstring());
    bool inSkins = (tail == L"skins");

    std::filesystem::path langPath;
    if (inSkins) {
        langPath = std::filesystem::path(L"..") / L"lang";
    } else {
        langPath = std::filesystem::path(L"lang");
    }
    langPath /= code + L".xml";
    return langPath.wstring();
}

static std::string NormalizePathForCompare(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '/', '\\');
    while (!result.empty() && (result.back() == '\\' || result.back() == '/')) {
        result.pop_back();
    }
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

static bool HandleRunningApplicationDialog(HWND hWnd, const std::string& appName) {
    if (appName.empty()) {
        return true;
    }

    std::string processName = appName;
    std::string lower = processName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".exe") {
        processName += ".exe";
    }

    while (isProcessRunningByName(processName)) {
        std::wstring title = GUIHelpers::GetLocalizedText(
            L"msg.dialog.running_app.title",
            L"Warning");
        std::wstring okText = GUIHelpers::GetLocalizedText(
            L"msg.dialog.running_app.retry",
            L"Retry");
        std::wstring cancelText = GUIHelpers::GetLocalizedText(
            L"msg.dialog.running_app.cancel",
            L"Cancel");
        std::wstring altText = GUIHelpers::GetLocalizedText(
            L"msg.dialog.running_app.terminate",
            L"Terminate");
        std::wstring message = GUIHelpers::GetLocalizedText(
            L"msg.dialog.running_app.message",
            L"Application is running.\n\nRetry: check again\nCancel: stop installation\nTerminate: close the app and continue");

        DialogResult result = GUIHelpers::ShowCustomDialog(
            hWnd,
            title,
            message,
            okText,
            cancelText,
            altText);
        if (result == DialogResult::Cancel) {
            return false;
        }
        if (result == DialogResult::Alt) {
            terminateProcessByName(processName);
            Sleep(500);
        }
    }

    return true;
}

static bool RequestPreviousInstallCleanup(HWND hWnd,
                                          const ExtendedInstallationMetadata& metadata,
                                          const std::wstring& installPath) {
    if (metadata.autoCleanOldInstall) {
        return true;
    }

    InstallerPathResolver pathResolver;
    std::string installPathUtf8 = WStringToUtf8(installPath);
    std::string resolvedInstallRoot = pathResolver.resolveFinalPath(
        installPathUtf8,
        SpecialDirectoryType::INSTALL_DIRECTORY,
        metadata.applicationName
    );

    std::string previousManifest;
    std::string previousInstallDir;
    if (!resolveExistingInstallInfo(metadata.applicationName, pathResolver,
                                    previousManifest, previousInstallDir)) {
        return false;
    }

    std::string newPath = resolvedInstallRoot.empty() ? installPathUtf8 : resolvedInstallRoot;
    std::string normalizedOld = NormalizePathForCompare(previousInstallDir);
    std::string normalizedNew = NormalizePathForCompare(newPath);
    if (normalizedOld.empty() || normalizedNew.empty() || normalizedOld == normalizedNew) {
        return false;
    }

    if (previousManifest.empty()) {
        std::cout << "Old install manifest not found; skipping cleanup prompt." << std::endl;
        return false;
    }

    std::wstring title = GUIHelpers::GetLocalizedText(
        L"msg.dialog.cleanup_old.title",
        L"Confirm");
    std::wstring yesText = GUIHelpers::GetLocalizedText(
        L"msg.dialog.cleanup_old.yes",
        L"Yes");
    std::wstring noText = GUIHelpers::GetLocalizedText(
        L"msg.dialog.cleanup_old.no",
        L"No");
    std::wstring message = GUIHelpers::GetLocalizedText(
        L"msg.dialog.cleanup_old.message",
        L"Previous install was detected in a different path. Clean it now?");
    DialogResult result = GUIHelpers::ShowCustomDialog(
        hWnd,
        title,
        message,
        yesText,
        noText,
        L"");
    return result == DialogResult::Ok;
}

void GUIManager::OnLicenseCheckboxChanged() {
    UpdateInstallButtonState();
}

void GUIManager::UpdateInstallButtonState() {
    if (!m_pInstallButton || !m_pLicenseCheckbox || !m_pInstallPathEdit) {
        return;
    }

    bool licenseAgreed = m_pLicenseCheckbox->GetCheck();

    std::wstring installPath = m_pInstallPathEdit->GetText().GetData();
    uint64_t availableSpace;
    bool spaceEnough = GUIHelpers::CheckDiskSpace(
        installPath,
        m_config.requiredDiskSpace,
        availableSpace);
    m_pInstallButton->SetEnabled(licenseAgreed && spaceEnough);
}

void GUIManager::UpdateDiskSpaceInfo(const std::wstring& path) {
    if (!m_pDiskSpaceLabel) {
        return;
    }

    uint64_t totalSpace = GUIHelpers::GetTotalDiskSpace(path);
    uint64_t availableSpace = GUIHelpers::GetAvailableDiskSpace(path);

    std::wstring totalStr = GUIHelpers::FormatBytes(totalSpace);
    std::wstring freeStr = GUIHelpers::FormatBytes(availableSpace);
    std::wstring requiredStr = GUIHelpers::FormatBytes(m_config.requiredDiskSpace);

    std::wstring totalLabel = GUIHelpers::GetLocalizedText(
        L"msg.space.total",
        L"Disk size: ");
    std::wstring freeLabel = GUIHelpers::GetLocalizedText(
        L"msg.space.free",
        L"Free space: ");
    std::wstring requiredLabel = GUIHelpers::GetLocalizedText(
        L"msg.space.required",
        L"Required: ");
    std::wstringstream ss;

    if (totalSpace > 0) {
        ss << totalLabel << totalStr << L" | " << freeLabel << freeStr;
    } else {
        ss << freeLabel << freeStr;
    }
    ss << L" | " << requiredLabel << requiredStr;
    
    m_pDiskSpaceLabel->SetText(ss.str().c_str());
    
    if (availableSpace < m_config.requiredDiskSpace) {
        m_pDiskSpaceLabel->SetTextColor(0xFFFF0000);
    } else {
        m_pDiskSpaceLabel->SetTextColor(0xFF666666);
    }
}

void GUIManager::RefreshLocalizedText() {
    CLabelUI* pAppVersion = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version")));
    if (pAppVersion) {
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.version.prefix",
            L"Version ");
        std::wstring versionText = prefix + m_config.version;
        pAppVersion->SetText(WStringToTStr(versionText));
    }

    CLabelUI* pAppVersionProgress = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version_progress")));
    if (pAppVersionProgress) {
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.version.prefix",
            L"Version ");
        std::wstring versionText = prefix + m_config.version;
        pAppVersionProgress->SetText(WStringToTStr(versionText));
    }

    CLabelUI* pAppVersionCompletion = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version_completion")));
    if (pAppVersionCompletion) {
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.version.prefix",
            L"Version ");
        std::wstring versionText = prefix + m_config.version;
        pAppVersionCompletion->SetText(WStringToTStr(versionText));
    }

    CLabelUI* pAppVersionUninstall = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("app_version_uninstall")));
    if (pAppVersionUninstall) {
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.version.prefix",
            L"Version ");
        std::wstring versionText = prefix + m_config.version;
        pAppVersionUninstall->SetText(WStringToTStr(versionText));
    }

    std::wstring currentPath;
    if (m_pInstallPathEdit) {
        currentPath = m_pInstallPathEdit->GetText().GetData();
    }
    UpdateDiskSpaceInfo(currentPath);
}

void GUIManager::HandleProgressMessage(ProgressMessageData* pData) {
    if (!pData) {
        return;
    }

    if (m_pTabPages && m_pTabPages->GetCurSel() != GetProgressPageIndex()) {
        m_pTabPages->SelectItem(GetProgressPageIndex());
    }

    float target = pData->percentage;
    if (target < 0.0f) {
        target = 0.0f;
    }
    if (target > 100.0f) {
        target = 100.0f;
    }

    if (pData->currentFolder[0] != L'\0') {
        m_progressFolder = pData->currentFolder;
    }

    m_progressTarget = target;
    if (m_progressDisplayed > m_progressTarget) {
        m_progressDisplayed = m_progressTarget;
    }

    if (m_progressDisplayed >= m_progressTarget) {
        UpdateProgressDisplay(m_progressDisplayed);
        StopProgressTimer();
        return;
    }

    StartProgressTimer();
}

void GUIManager::StartProgressTimer() {
    if (m_progressTimerActive || !m_hWnd) {
        return;
    }
    m_progressLastTick = static_cast<uint64_t>(GetTickCount64());
    ::SetTimer(m_hWnd, kProgressTimerId, kProgressTimerIntervalMs, nullptr);
    m_progressTimerActive = true;
}

void GUIManager::StopProgressTimer() {
    if (!m_progressTimerActive || !m_hWnd) {
        return;
    }
    ::KillTimer(m_hWnd, kProgressTimerId);
    m_progressTimerActive = false;
}

void GUIManager::TickProgressAnimation() {
    if (!m_progressTimerActive) {
        return;
    }

    uint64_t now = static_cast<uint64_t>(GetTickCount64());
    uint64_t deltaMs = now > m_progressLastTick ? (now - m_progressLastTick) : 0;
    m_progressLastTick = now;

    float delta = m_progressTarget - m_progressDisplayed;
    if (delta <= 0.01f) {
        m_progressDisplayed = m_progressTarget;
        UpdateProgressDisplay(m_progressDisplayed);
        StopProgressTimer();
        return;
    }

    float t = (std::min)(1.0f, static_cast<float>(deltaMs) / 200.0f);
    float step = (std::max)(delta * t, 0.05f);
    if (step > delta) {
        step = delta;
    }
    m_progressDisplayed += step;
    UpdateProgressDisplay(m_progressDisplayed);
}

void GUIManager::UpdateProgressDisplay(float percentage) {
    CLabelUI* pCurrentFolderLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("current_folder")));
    if (pCurrentFolderLabel && !m_progressFolder.empty()) {
        std::wstring prefix = GUIHelpers::GetLocalizedText(
            L"msg.progress.processing",
            L"Processing: ");
        std::wstring folderText = prefix + m_progressFolder;
        pCurrentFolderLabel->SetText(WStringToTStr(folderText));
    }

    CProgressUI* pProgressBar = static_cast<CProgressUI*>(
        m_pm.FindControl(_T("progress_bar")));
    if (pProgressBar) {
        int progressValue = static_cast<int>(percentage);
        pProgressBar->SetValue(progressValue);
    }

    CLabelUI* pProgressPercentLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("progress_percent")));
    if (pProgressPercentLabel) {
        wchar_t percentText[32];
        swprintf_s(percentText, L"%.1f%%", percentage);
        pProgressPercentLabel->SetText(percentText);
    }

    CLabelUI* pEstimatedTimeLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("estimated_time")));
    if (pEstimatedTimeLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(
            L"msg.progress.eta",
            L"Estimated time remaining: Calculating...");
        pEstimatedTimeLabel->SetText(WStringToTStr(text));
    }
}

void GUIManager::HandleCompletionMessage(CompletionMessageData* pData) {
    if (!pData) {
        return;
    }

    StopProgressTimer();
    
    if (m_pTabPages) {
        m_pTabPages->SelectItem(GetCompletionPageIndex());
    }
    
    CLabelUI* pResultMessageLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("result_message")));
    if (pResultMessageLabel) {
        if (pData->success) {
            std::wstring text = GUIHelpers::GetLocalizedText(
                L"msg.install.success",
                L"Installation successful!");
            pResultMessageLabel->SetText(WStringToTStr(text));
            pResultMessageLabel->SetTextColor(0xFF4CAF50);
        } else {
            std::wstring prefix = GUIHelpers::GetLocalizedText(
                L"msg.install.failed",
                L"Installation failed: ");
            std::wstring errorText = prefix + pData->errorMessage;
            pResultMessageLabel->SetText(WStringToTStr(errorText));
            pResultMessageLabel->SetTextColor(0xFFFF0000);
        }
    }
    
    if (!pData->success) {
        CCheckBoxUI* pRunAppCheckbox = static_cast<CCheckBoxUI*>(
            m_pm.FindControl(_T("run_app_checkbox")));
        if (pRunAppCheckbox) {
            pRunAppCheckbox->SetVisible(false);
        }
        
        CCheckBoxUI* pOpenWebCheckbox = static_cast<CCheckBoxUI*>(
            m_pm.FindControl(_T("open_web_checkbox")));
        if (pOpenWebCheckbox) {
            pOpenWebCheckbox->SetVisible(false);
        }
    } else if (m_config.webPageUrl.empty()) {
        CCheckBoxUI* pOpenWebCheckbox = static_cast<CCheckBoxUI*>(
            m_pm.FindControl(_T("open_web_checkbox")));
        if (pOpenWebCheckbox) {
            pOpenWebCheckbox->SetVisible(false);
        }
    }
}

void GUIManager::ApplyLanguageByIndex(int index) {
    ApplyLanguageByCode(GetLanguageCodeForIndex(index));
}

void GUIManager::ApplyLanguageByCode(const std::wstring& code) {
    std::wstring langPath = GetLanguageFilePath(code);
    if (langPath.empty()) {
        return;
    }

    std::cout << "Language resource path: " << WStringToUtf8(CPaintManagerUI::GetResourcePath().GetData())
              << " file=" << WStringToUtf8(langPath) << std::endl;

    if (!CResourceManager::GetInstance()->LoadLanguage(langPath.c_str())) {
        if (code != L"en_US") {
            std::wstring fallbackPath = GetLanguageFilePath(L"en_US");
            if (!fallbackPath.empty() &&
                CResourceManager::GetInstance()->LoadLanguage(fallbackPath.c_str())) {
                CResourceManager::GetInstance()->SetLanguage(L"en_US");
                std::cout << "Language fallback loaded: " << WStringToUtf8(fallbackPath) << std::endl;
            } else {
                std::cout << "Failed to load language file: " << WStringToUtf8(langPath) << std::endl;
                return;
            }
        } else {
            std::cout << "Failed to load language file: " << WStringToUtf8(langPath) << std::endl;
            return;
        }
    } else {
        CResourceManager::GetInstance()->SetLanguage(code.c_str());
    }

    CResourceManager::GetInstance()->ReloadText();
    m_pm.NeedUpdate();
    m_pm.Invalidate();
    RefreshLocalizedText();
}

void GUIManager::HandleUninstallCompletionMessage(CompletionMessageData* pData) {
    if (!pData) {
        return;
    }

    StopProgressTimer();

    if (m_pTabPages) {
        m_pTabPages->SelectItem(GetCompletionPageIndex());
    }

    CLabelUI* pResultMessageLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("uninstall_result_message")));
    if (pResultMessageLabel) {
        if (pData->success) {
            std::wstring text = GUIHelpers::GetLocalizedText(
                L"msg.uninstall.success",
                L"Uninstall completed.");
            pResultMessageLabel->SetText(WStringToTStr(text));
            pResultMessageLabel->SetTextColor(0xFF4CAF50);
        } else {
            std::wstring prefix = GUIHelpers::GetLocalizedText(
                L"msg.uninstall.failed",
                L"Uninstall failed: ");
            std::wstring errorText = prefix + pData->errorMessage;
            pResultMessageLabel->SetText(WStringToTStr(errorText));
            pResultMessageLabel->SetTextColor(0xFFFF0000);
        }
    }
}

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
