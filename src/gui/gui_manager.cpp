#include "../../include/gui/gui_manager.h"
#include "../../include/gui/page_controller.h"
#include "../../include/gui/gui_helpers.h"
#include "../../include/gui/gui_install_actions.h"
#include "../../include/gui/gui_component_binding.h"
#include "../../include/gui/gui_diagnostics.h"
#include "../../include/gui/gui_event_router.h"
#include "../../include/gui/gui_install_flow_utils.h"
#include "../../include/gui/gui_interaction_helpers.h"
#include "../../include/gui/gui_runtime_utils.h"
#include "../../include/gui/gui_startup_initializer.h"
#include "../../include/gui/gui_status_presenter.h"
#include "../../include/gui/gui_text_presenter.h"
#include "../../include/gui/license_text_loader.h"
#include "../../include/gui/installation_worker.h"
#include "../../include/gui/uninstall_worker.h"
#include "../../include/installer/metadata_parser.h"
#include "../../include/installer/path_resolver.h"
#include "../../include/installer/installer_helpers.h"
#include "../../include/installer/uninstall_manager.h"
#include "../../include/installer/registry_utils.h"
#include "../../include/installer/gui_resource_loader.h"
#include "common/utf8_utils.h"
#include "common/installer_logger.h"
#include "Utils/unzip.h"
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
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
static std::string ToLowerAsciiCopy(const std::string& text) {
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

static std::string NormalizeSkinName(const std::string& skin) {
    std::string normalized = ToLowerAsciiCopy(skin);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    size_t slashPos = normalized.find_last_of('/');
    if (slashPos != std::string::npos) {
        normalized = normalized.substr(slashPos + 1);
    }
    return normalized;
}

static int ResolveInstallPageIndex(const std::string& skin) {
    const std::string normalized = NormalizeSkinName(skin);
    if (normalized == "welcome_page.xml") {
        return kPageWelcome;
    }
    if (normalized == "license_page.xml") {
        return kPageLicense;
    }
    if (normalized == "progress_page.xml") {
        return kPageProgress;
    }
    if (normalized == "completion_page.xml") {
        return kPageCompletion;
    }
    return -1;
}

template <typename T>
bool PostOwnedGuiMessage(HWND hwnd, UINT message, T* payload, const char* tag) {
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

static const char* GetInstallPageSkinByIndex(int index) {
    switch (index) {
        case kPageWelcome:
            return "skins/welcome_page.xml";
        case kPageLicense:
            return "skins/license_page.xml";
        case kPageProgress:
            return "skins/progress_page.xml";
        case kPageCompletion:
            return "skins/completion_page.xml";
        default:
            return nullptr;
    }
}

static const char* GetUninstallPageSkinByIndex(int index) {
    switch (index) {
        case 0:
            return "skins/uninstall_confirm_page.xml";
        case 1:
            return "skins/uninstall_progress_page.xml";
        case 2:
            return "skins/uninstall_completion_page.xml";
        default:
            return nullptr;
    }
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
        m_repairMode(false),
        m_baseClientHeight(0),
        m_baseClientWidth(0),
        m_expandedClientHeight(0),
        m_baseWindowWidth(0),
        m_installMetadataLoaded(false),
        m_uninstallMode(false),
        m_progressTarget(0.0f),
        m_progressDisplayed(0.0f),
        m_progressTimerActive(false),
        m_progressLastTick(0) {
      m_pm.GetDPIObj()->SetScale(static_cast<int>(GetDpiForWindowSafe(nullptr)));
  }

GUIManager::~GUIManager() {
    if (m_pWorker) {
        if (m_pWorker->IsRunning()) {
            m_pWorker->RequestCancellation();
        }
        delete m_pWorker;
        m_pWorker = nullptr;
    }

    if (m_pUninstallWorker) {
        delete m_pUninstallWorker;
        m_pUninstallWorker = nullptr;
    }

    if (m_pPageController) {
        delete m_pPageController;
        m_pPageController = nullptr;
    }
}

void GUIManager::SetInstallConfig(const InstallConfig& config) {
    m_config = config;
    m_repairMode = config.repairMode;
}

void GUIManager::SetInstallMetadata(const ExtendedInstallationMetadata& metadata) {
    m_installMetadata = metadata;
    m_uiLinks.clear();
    for (const auto& link : m_installMetadata.uiLinkBindings) {
        if (!link.control.empty() && !link.url.empty()) {
            m_uiLinks[link.control] = Utf8ToWide(link.url);
        }
    }
    m_installMetadataLoaded = true;
}

void GUIManager::PrepareInitialDpi(unsigned int dpi) {
    if (dpi == 0) {
        return;
    }
    if (m_pm.GetDPIObj()->GetScale() != dpi) {
        m_pm.GetDPIObj()->SetScale(dpi);
    }
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

    logInstallerInfo(std::string("[GUI] GUI mode uninstall=") +
                     (m_uninstallMode ? "true" : "false"));

    std::wstring installPath = ResolveInitialInstallPath(m_config);
    ApplyInitialInstallPathUi(m_pm, m_pInstallPathEdit, installPath, m_repairMode);

    UpdateDiskSpaceInfo(installPath);
    
    UpdateInstallButtonState();
    
    if (m_pInstallPathEdit) {
        m_pInstallPathEdit->SetFocus();
    }

    if (!m_uninstallMode) {
        InitializeComponentSelectionUi();
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
    if (!m_pInstallButton) {
        m_pInstallButton = static_cast<CButtonUI*>(
            m_pm.FindControl(_T("btnInstall")));
    }
    
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
    
    GUITextPresenter::BindStaticAppTexts(m_pm, m_config);

    if (m_repairMode) {
        const bool isChinese = m_config.languageCode.find(L"zh") != std::wstring::npos;
        const std::wstring repairText = isChinese ? L"修复" : L"Repair";
        const std::wstring repairTitle = isChinese ? L"修复向导" : L"Repair Wizard";
        if (m_pInstallButton) {
            m_pInstallButton->SetText(WStringToTStr(repairText));
        }
        if (auto* title = static_cast<CLabelUI*>(m_pm.FindControl(_T("title")))) {
            title->SetText(WStringToTStr(repairTitle));
        }
    }
    
    if (m_pTabPages) {
        m_pTabPages->SelectItem(GetWelcomePageIndex());
    }
}

bool GUIManager::EnsureInstallMetadataLoaded() {
    if (m_uninstallMode) {
        return false;
    }
    if (m_installMetadataLoaded) {
        return true;
    }

    try {
        MetadataParser parser;
        ExtendedInstallationMetadata metadata = parser.parseExtendedEmbeddedMetadata();
        if (!parser.validateMetadata(metadata)) {
            return false;
        }
        m_installMetadata = std::move(metadata);
        m_uiLinks.clear();
        for (const auto& link : m_installMetadata.uiLinkBindings) {
            if (!link.control.empty() && !link.url.empty()) {
                m_uiLinks[link.control] = Utf8ToWide(link.url);
            }
        }
        m_installMetadataLoaded = true;
        return true;
    } catch (const std::exception& ex) {
        logInstallerError(std::string("[GUI] Failed to load install metadata: ") + ex.what());
        return false;
    } catch (...) {
        logInstallerError("[GUI] Failed to load install metadata: unknown error.");
        return false;
    }
}

void GUIManager::InitializeComponentSelectionUi() {
    if (!EnsureInstallMetadataLoaded() || !m_pTabPages) {
        return;
    }

    std::vector<std::string> warnings;
    InitializeComponentBindingsUI(m_pTabPages, m_installMetadata, warnings);

    for (const auto& warning : warnings) {
        if (!warning.empty()) {
            logInstallerWarning(std::string("[GUI] ") + warning);
        }
    }
}

std::vector<std::string> GUIManager::CollectSelectedComponentsFromUi() {
    std::vector<std::string> selected;
    if (!EnsureInstallMetadataLoaded() || !m_pTabPages) {
        return selected;
    }

    std::vector<std::string> warnings;
    selected = CollectSelectedComponentIdsFromUI(m_pTabPages, m_installMetadata, warnings);

    for (const auto& warning : warnings) {
        if (!warning.empty()) {
            logInstallerWarning(std::string("[GUI] ") + warning);
        }
    }

    return selected;
}

void GUIManager::Notify(TNotifyUI& msg) {
    RouteGuiNotify(
        msg,
        m_uiLinks,
        GuiNotifyCallbacks{
            [this]() { OnInstallButtonClick(); },
            [this]() { OnCancelButtonClick(); },
            [this]() { OnBrowseButtonClick(); },
            [this]() { OnFinishButtonClick(); },
            [this]() { OnCancelProgressButtonClick(); },
            [this]() { SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0); },
            [this]() { OnShowMoreClick(); },
            [this]() { OnLicenseLinkClick(); },
            [this]() { OnLicenseAgreeClick(); },
            [this]() { OnLicenseDisagreeClick(); },
            [this]() { OnUninstallConfirmClick(); },
            [this]() { Close(); },
            [this]() { OnLicenseCheckboxChanged(); },
            [this](int index) { ApplyLanguageByIndex(index); },
            [](const std::wstring& url) { GUIHelpers::OpenWebPage(url); }});
}

LRESULT GUIManager::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
#ifdef _WIN32
    if (uMsg == WM_DPICHANGED) {
        (void)lParam;
        UINT dpi = LOWORD(wParam);
        if (dpi == 0) {
            dpi = GetDpiForWindowSafe(m_hWnd);
        }
        const unsigned int oldDpi = static_cast<unsigned int>(m_pm.GetDPIObj()->GetScale());
        logInstallerInfo(std::string("[GUI][DPI] WM_DPICHANGED begin old_dpi=") +
                         std::to_string(oldDpi) + " new_dpi=" + std::to_string(dpi));
        m_pm.SetDPI(static_cast<int>(dpi));
        const std::vector<std::string> xmlScope = BuildCurrentGuiXmlScope(m_pTabPages, m_uninstallMode);
        LogActiveGuiResourceDiagnosticsForXmlEntries(dpi, "GUIManager::WM_DPICHANGED", xmlScope);
        LogCurrentPageControlImageSnapshot(m_pTabPages, m_uninstallMode, "GUIManager::WM_DPICHANGED");
        logInstallerInfo(std::string("[GUI][DPI] WM_DPICHANGED end current_dpi=") +
                         std::to_string(dpi));
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
        if (m_pWorker && m_pWorker->IsRunning()) {
            m_pWorker->RequestCancellation();
        }
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
    else if (uMsg == WM_UNINSTALL_PROGRESS) {
        ProgressMessageData* pData = reinterpret_cast<ProgressMessageData*>(lParam);
        if (pData) {
            HandleProgressMessage(pData);
            delete pData;
        }
        return 0;
    }
    else if (uMsg == WM_KEYDOWN) {
        int currentPage = 0;
        if (m_pTabPages) {
            currentPage = m_pTabPages->GetCurSel();
        }

        const bool installEnabled = m_pInstallButton && m_pInstallButton->IsEnabled();
        if (RouteGuiKeyDown(
                wParam,
                currentPage,
                m_uninstallMode,
                installEnabled,
                GetWelcomePageIndex(),
                GetProgressPageIndex(),
                GetCompletionPageIndex(),
                kPageLicense,
                GuiKeydownCallbacks{
                    [this]() { OnInstallButtonClick(); },
                    [this]() { OnCancelButtonClick(); },
                    [this]() { OnCancelProgressButtonClick(); },
                    [this]() { OnFinishButtonClick(); }})) {
            return 0;
        }
    }
    
    return WindowImplBase::HandleMessage(uMsg, wParam, lParam);
}

void GUIManager::OnInstallButtonClick() {
    GUIInstallActions::InstallStartRequest request;
    if (!GUIInstallActions::TryBuildInstallStartRequest(
            m_hWnd, m_pm, m_pInstallPathEdit, m_config, request)) {
        return;
    }

    StopProgressTimer();
    m_progressTarget = 0.0f;
    m_progressDisplayed = 0.0f;
    m_progressFolder.clear();
    UpdateProgressDisplay(0.0f);

    CollapseConfigIfExpanded();

    if (!EnsureInstallMetadataLoaded()) {
        GUIHelpers::ShowErrorDialog(
            m_hWnd,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L""),
            GUIHelpers::GetLocalizedText(L"msg.dialog.metadata_read_failed", L""));
        return;
    }
    const ExtendedInstallationMetadata& metadata = m_installMetadata;
    std::vector<std::string> selectedComponents = CollectSelectedComponentsFromUi();

    bool cleanupOldInstall = m_repairMode
                                 ? true
                                 : GUIInstallFlowUtils::ConfirmCleanupOldInstall(
                                       m_hWnd, metadata, request.installPath);

    std::vector<std::string> processNames = buildKillProcessList(
        metadata.appName,
        metadata.installKillProcesses);
    if (!HandleRunningApplicationDialog(m_hWnd, processNames)) {
        return;
    }

    if (!m_pWorker) {
        m_pWorker = new InstallationWorker(m_hWnd);
    }
    m_pWorker->StartInstallation(request.installPath,
                                 request.autoRun,
                                 request.desktopIcons,
                                 request.languageCode,
                                 m_repairMode,
                                 cleanupOldInstall,
                                 selectedComponents);

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

    std::vector<std::string> identityCandidates =
        GUIInstallActions::BuildIdentityCandidatesFromConfig(m_config);
    if (identityCandidates.empty()) {
        CompletionMessageData* pData = new CompletionMessageData();
        pData->success = false;
        std::wstring text = GUIHelpers::GetLocalizedText(L"msg.uninstall.appname_missing", L"");
        wcsncpy_s(pData->errorMessage, text.c_str(), _TRUNCATE);
        PostOwnedGuiMessage(m_hWnd, WM_UNINSTALL_COMPLETE, pData, "[GUI]");
        return;
    }

    m_pUninstallWorker->StartUninstall(identityCandidates);
}

void GUIManager::OnCancelButtonClick() {
    std::wstring title = GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L"");
    std::wstring message = GUIHelpers::GetLocalizedText(L"msg.dialog.exit_confirm", L"");
    if (GUIHelpers::ShowConfirmDialog(m_hWnd, title, message)) {
        Close();
    }
}

void GUIManager::OnBrowseButtonClick() {
    if (m_repairMode) {
        return;
    }

    std::wstring currentPath;
    if (m_pInstallPathEdit) {
        currentPath = m_pInstallPathEdit->GetText().GetData();
    }
    
    std::wstring selectedPath;
    if (GUIHelpers::ShowFolderBrowserDialog(
        m_hWnd,
        GUIHelpers::GetLocalizedText(L"msg.dialog.select_dir_title", L""),
        currentPath,
        selectedPath)) {
        
        std::wstring finalPath =
            GUIInstallFlowUtils::ResolveSelectedInstallPath(m_config, selectedPath);

        if (m_pInstallPathEdit) {
            m_pInstallPathEdit->SetText(WStringToTStr(finalPath));
        }
        
        UpdateDiskSpaceInfo(finalPath);
        
        UpdateInstallButtonState();
    }
}

void GUIManager::OnLicenseLinkClick() {
    CollapseConfigIfExpanded();
    MultiThreadedInstaller::ShowLicensePage(m_pm, m_pTabPages, m_pLicenseCheckbox, m_config, kPageLicense);
}

void GUIManager::OnLicenseAgreeClick() {
    MultiThreadedInstaller::ApplyLicenseAgreementSelection(m_pLicenseCheckbox,
                                                           true,
                                                           m_pInstallButton,
                                                           m_config.requiredDiskSpace,
                                                           m_pInstallPathEdit);
    if (m_pTabPages) {
        m_pTabPages->SelectItem(kPageWelcome);
    }
}

void GUIManager::OnLicenseDisagreeClick() {
    MultiThreadedInstaller::ApplyLicenseAgreementSelection(m_pLicenseCheckbox,
                                                           false,
                                                           m_pInstallButton,
                                                           m_config.requiredDiskSpace,
                                                           m_pInstallPathEdit);
    if (m_pTabPages) {
        m_pTabPages->SelectItem(kPageWelcome);
    }
}

void GUIManager::ShowLicensePage() {
    CollapseConfigIfExpanded();
    MultiThreadedInstaller::ShowLicensePage(m_pm, m_pTabPages, m_pLicenseCheckbox, m_config, kPageLicense);
}

void GUIManager::RefreshLicenseText() {
    MultiThreadedInstaller::RefreshLicenseText(m_pm, m_config);
}

void GUIManager::OnFinishButtonClick() {
    GUIInstallActions::RunPostInstallActions(m_hWnd, m_pm, m_pInstallPathEdit, m_config);
    Close();
}

void GUIManager::OnCancelProgressButtonClick() {
    std::wstring title = GUIHelpers::GetLocalizedText(L"msg.dialog.title.prompt", L"");
    std::wstring message = GUIHelpers::GetLocalizedText(L"msg.dialog.cancel_install_confirm", L"");
    if (!GUIHelpers::ShowConfirmDialog(m_hWnd, title, message)) {
        return;
    }

    if (m_pWorker && m_pWorker->IsRunning()) {
        m_pWorker->RequestCancellation();
    }

    CButtonUI* pCancelProgressButton = static_cast<CButtonUI*>(
        m_pm.FindControl(_T("cancel_progress_button")));
    if (pCancelProgressButton) {
        pCancelProgressButton->SetEnabled(false);
    }

    CLabelUI* pEstimatedTimeLabel = static_cast<CLabelUI*>(
        m_pm.FindControl(_T("estimated_time")));
    if (pEstimatedTimeLabel) {
        std::wstring text = GUIHelpers::GetLocalizedText(L"msg.progress.cancelling", L"");
        pEstimatedTimeLabel->SetText(WStringToTStr(text));
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
    logInstallerDebug(std::string("[GUI] btnShowMore toggled=") + (show ? "true" : "false") +
                      " base=" + std::to_string(m_baseClientWidth) + "x" +
                      std::to_string(m_baseClientHeight) +
                      " targetWindow=" + std::to_string(windowWidth) + "x" +
                      std::to_string(targetWindowHeight));
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

void GUIManager::OnLicenseCheckboxChanged() {
    UpdateInstallButtonState();
}

void GUIManager::UpdateInstallButtonState() {
    GUIInstallFlowUtils::UpdateInstallButtonEnabled(
        m_pInstallButton, m_pLicenseCheckbox, m_pInstallPathEdit, m_config.requiredDiskSpace);
}

void GUIManager::UpdateDiskSpaceInfo(const std::wstring& path) {
    GUIInstallFlowUtils::UpdateDiskSpaceLabel(m_pDiskSpaceLabel, path, m_config.requiredDiskSpace);
}

void GUIManager::RefreshLocalizedText() {
    GUITextPresenter::RefreshVersionTexts(m_pm, m_config);

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
    GUIStatusPresenter::UpdateProgressDisplay(m_pm, m_progressFolder, percentage);
}

void GUIManager::HandleCompletionMessage(CompletionMessageData* pData) {
    if (!pData) {
        return;
    }

    StopProgressTimer();
    
    if (m_pTabPages) {
        m_pTabPages->SelectItem(GetCompletionPageIndex());
    }
    
    GUIStatusPresenter::ShowInstallCompletion(m_pm, m_config, *pData);
}

void GUIManager::ApplyLanguageByIndex(int index) {
    ApplyLanguageByCode(GetLanguageCodeForIndex(index));
}

void GUIManager::ApplyLanguageByCode(const std::wstring& code) {
    if (!GUITextPresenter::ApplyLanguage(m_pm, m_config, code)) {
        return;
    }
    RefreshLicenseText();
}

void GUIManager::HandleUninstallCompletionMessage(CompletionMessageData* pData) {
    if (!pData) {
        return;
    }

    StopProgressTimer();

    if (m_pTabPages) {
        m_pTabPages->SelectItem(GetCompletionPageIndex());
    }

    GUIStatusPresenter::ShowUninstallCompletion(m_pm, *pData);
}

} // namespace MultiThreadedInstaller
