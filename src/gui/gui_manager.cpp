#include "../../include/gui/gui_manager.h"
#include "../../include/gui/page_controller.h"
#include "../../include/gui/gui_helpers.h"
#include "../../include/gui/gui_install_actions.h"
#include "../../include/gui/gui_install_flow_utils.h"
#include "../../include/gui/gui_runtime_utils.h"
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
struct ComponentControlBinding {
    CCheckBoxUI* checkBox = nullptr;
    std::string componentId;
    std::string controlName;
};

struct ComponentPageScope {
    CControlUI* root = nullptr;
    std::unordered_set<std::string> allowedControls;
};

static std::string TrimAsciiCopy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(start, end - start);
}

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

static void CollectControlsRecursive(CControlUI* root, std::vector<CControlUI*>& controls) {
    if (!root) {
        return;
    }
    controls.push_back(root);
    CContainerUI* container = static_cast<CContainerUI*>(root->GetInterface(_T("Container")));
    if (!container) {
        return;
    }
    const int count = container->GetCount();
    for (int i = 0; i < count; ++i) {
        CollectControlsRecursive(container->GetItemAt(i), controls);
    }
}

static bool IsEmbeddedSelectionMode(const std::string& mode) {
    if (mode.empty()) {
        return false;
    }
    const std::string lowered = ToLowerAsciiCopy(mode);
    return lowered == "embeddedinexistingpages" || lowered == "hybrid";
}

static std::vector<ComponentControlBinding> CollectComponentBindings(CTabLayoutUI* tabPages,
                                                                     const ExtendedInstallationMetadata& metadata,
                                                                     std::vector<std::string>* warnings) {
    std::vector<ComponentControlBinding> bindings;
    if (!tabPages || metadata.components.empty()) {
        return bindings;
    }

    const UiComponentSelectionConfig& ui = metadata.componentUi;
    if (!IsEmbeddedSelectionMode(ui.mode)) {
        return bindings;
    }

    const std::string strategy = ToLowerAsciiCopy(ui.strategy);
    if (!strategy.empty() && strategy != "xml_userdata") {
        if (warnings) {
            warnings->push_back("Unsupported component selection strategy: " + ui.strategy);
        }
        return bindings;
    }

    std::string tokenPrefix = ui.tokenPrefix;
    if (tokenPrefix.empty()) {
        tokenPrefix = "component:";
    }

    std::vector<ComponentPageScope> scopes;
    if (!ui.pages.empty()) {
        for (const auto& page : ui.pages) {
            const int index = ResolveInstallPageIndex(page.skin);
            if (index < 0 || index >= tabPages->GetCount()) {
                if (warnings) {
                    warnings->push_back("Component binding page not found in installer tabs: " + page.skin);
                }
                continue;
            }
            CControlUI* root = tabPages->GetItemAt(index);
            if (!root) {
                continue;
            }
            ComponentPageScope scope;
            scope.root = root;
            for (const auto& controlName : page.controls) {
                if (!controlName.empty()) {
                    scope.allowedControls.insert(ToLowerAsciiCopy(controlName));
                }
            }
            scopes.push_back(std::move(scope));
        }
    } else {
        const int maxIndex = (std::min)(tabPages->GetCount(), kPageCompletion + 1);
        for (int i = kPageWelcome; i < maxIndex; ++i) {
            ComponentPageScope scope;
            scope.root = tabPages->GetItemAt(i);
            scopes.push_back(std::move(scope));
        }
    }

    std::unordered_set<CCheckBoxUI*> seenControls;
    for (const auto& scope : scopes) {
        if (!scope.root) {
            continue;
        }

        std::vector<CControlUI*> controls;
        controls.reserve(64);
        CollectControlsRecursive(scope.root, controls);

        for (CControlUI* control : controls) {
            if (!control) {
                continue;
            }
            CCheckBoxUI* checkBox = static_cast<CCheckBoxUI*>(control->GetInterface(_T("CheckBox")));
            if (!checkBox || !seenControls.insert(checkBox).second) {
                continue;
            }

            const std::string controlName =
                ToLowerAsciiCopy(WideToUtf8(TCharToWide(control->GetName().GetData())));
            if (!scope.allowedControls.empty() &&
                scope.allowedControls.find(controlName) == scope.allowedControls.end()) {
                continue;
            }

            const std::string userData = WideToUtf8(TCharToWide(checkBox->GetUserData().GetData()));
            if (userData.size() < tokenPrefix.size() ||
                userData.compare(0, tokenPrefix.size(), tokenPrefix) != 0) {
                continue;
            }

            std::string componentId = TrimAsciiCopy(userData.substr(tokenPrefix.size()));
            if (componentId.empty()) {
                if (warnings) {
                    warnings->push_back("Empty component id in checkbox userdata: " +
                                        WideToUtf8(TCharToWide(checkBox->GetName().GetData())));
                }
                continue;
            }

            ComponentControlBinding binding;
            binding.checkBox = checkBox;
            binding.componentId = componentId;
            binding.controlName = controlName;
            bindings.push_back(std::move(binding));
        }
    }

    return bindings;
}

static void ApplyComponentBindingConstraints(const ExtendedInstallationMetadata& metadata,
                                             const std::vector<ComponentControlBinding>& bindings,
                                             bool applyDefaults,
                                             std::vector<std::string>* warnings) {
    std::unordered_map<std::string, const ComponentConfig*> componentIndex;
    componentIndex.reserve(metadata.components.size());
    for (const auto& component : metadata.components) {
        componentIndex[component.id] = &component;
    }

    for (const auto& binding : bindings) {
        auto it = componentIndex.find(binding.componentId);
        if (it == componentIndex.end()) {
            if (warnings) {
                warnings->push_back("UI checkbox references unknown component id: " + binding.componentId);
            }
            continue;
        }
        const ComponentConfig* component = it->second;
        if (!component) {
            continue;
        }
        if (component->required) {
            binding.checkBox->SetCheck(true);
            binding.checkBox->SetEnabled(false);
            binding.checkBox->SetMouseEnabled(false);
            continue;
        }
        if (applyDefaults) {
            binding.checkBox->SetCheck(component->defaultSelected);
        }
    }
}

static std::vector<std::string> CollectSelectedComponentIds(
    const ExtendedInstallationMetadata& metadata,
    const std::vector<ComponentControlBinding>& bindings,
    std::vector<std::string>* warnings) {
    std::unordered_map<std::string, const ComponentConfig*> componentIndex;
    componentIndex.reserve(metadata.components.size());
    for (const auto& component : metadata.components) {
        componentIndex[component.id] = &component;
    }

    std::unordered_set<std::string> selected;
    selected.reserve(metadata.components.size());
    for (const auto& component : metadata.components) {
        if (component.required) {
            selected.insert(component.id);
        }
    }

    for (const auto& binding : bindings) {
        if (componentIndex.find(binding.componentId) == componentIndex.end()) {
            if (warnings) {
                warnings->push_back("Skipping unknown component id from UI: " + binding.componentId);
            }
            continue;
        }
        if (binding.checkBox->GetCheck()) {
            selected.insert(binding.componentId);
        }
    }

    std::vector<std::string> ordered;
    ordered.reserve(selected.size());
    for (const auto& component : metadata.components) {
        if (selected.find(component.id) != selected.end()) {
            ordered.push_back(component.id);
        }
    }
    return ordered;
}

static bool HandleRunningApplicationDialog(HWND hWnd, const std::vector<std::string>& processNames) {
    (void)hWnd;
    if (processNames.empty()) {
        return true;
    }

    std::vector<std::string> running = getRunningProcessesByName(processNames);
    if (running.empty()) {
        return true;
    }

    auto joinNames = [](const std::vector<std::string>& names) {
        std::string joined;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                joined += ", ";
            }
            joined += names[i];
        }
        return joined;
    };

    logInstallerInfo(std::string("[GUI] Terminating processes: ") + joinNames(running));
    terminateProcessesByName(running);
    Sleep(500);

    std::vector<std::string> remaining = getRunningProcessesByName(processNames);
    if (!remaining.empty()) {
        std::wstring title = GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L"");
        std::wstring message = GUIHelpers::GetLocalizedText(L"msg.dialog.running_app.failed", L"");
        GUIHelpers::ShowErrorDialog(hWnd, title, message);
        return false;
    }

    return true;
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

static std::wstring ResolveEffectiveDirectoryNameFromConfig(const InstallConfig& config) {
    const std::string directoryName = resolveEffectiveDirectoryName(
        WideToUtf8(config.directoryName),
        WideToUtf8(config.applicationName));
    return Utf8ToWide(directoryName);
}

static std::wstring ApplyInstallDirectoryRule(const std::wstring& basePath, const InstallConfig& config) {
    if (!config.installDirectoryAppendName) {
        return basePath;
    }
    return NormalizeInstallPath(basePath, ResolveEffectiveDirectoryNameFromConfig(config));
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
        m_installMetadataLoaded(false),
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

    logInstallerInfo(std::string("[GUI] GUI mode uninstall=") +
                     (m_uninstallMode ? "true" : "false"));
    
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
        std::string regPath = WideToUtf8(m_config.registryPath);
        std::string regKey = WideToUtf8(m_config.registryKey);
        std::string regValue;
        if (readRegistryStringValue(regPath, regKey, regValue)) {
            std::wstring regPathW = Utf8ToWide(regValue);
            if (!regPathW.empty()) {
                installPath = regPathW;
            }
        }
    }

    InstallerPathResolver identityResolver;
    std::string previousManifest;
    std::string previousInstallDir;
    std::vector<std::string> identityCandidates =
        GUIInstallActions::BuildIdentityCandidatesFromConfig(m_config);
    if (resolveExistingInstallInfo(identityCandidates,
                                   identityResolver,
                                   previousManifest,
                                   previousInstallDir) &&
        !previousInstallDir.empty()) {
        installPath = Utf8ToWide(previousInstallDir);
    } else {
        installPath = ApplyInstallDirectoryRule(installPath, m_config);
    }

    if (m_pInstallPathEdit) {
        m_pInstallPathEdit->SetText(WStringToTStr(installPath));
    }

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
    
    GUITextPresenter::BindStaticAppTexts(m_pm, m_config);
    
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
        for (const auto& link : m_installMetadata.uiLinks) {
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
    std::vector<ComponentControlBinding> bindings =
        CollectComponentBindings(m_pTabPages, m_installMetadata, &warnings);
    ApplyComponentBindingConstraints(m_installMetadata, bindings, true, &warnings);

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
    std::vector<ComponentControlBinding> bindings =
        CollectComponentBindings(m_pTabPages, m_installMetadata, &warnings);
    ApplyComponentBindingConstraints(m_installMetadata, bindings, false, &warnings);
    selected = CollectSelectedComponentIds(m_installMetadata, bindings, &warnings);

    for (const auto& warning : warnings) {
        if (!warning.empty()) {
            logInstallerWarning(std::string("[GUI] ") + warning);
        }
    }

    return selected;
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
        else {
            std::string controlName = WideToUtf8(TCharToWide(senderName.GetData()));
            auto it = m_uiLinks.find(controlName);
            if (it != m_uiLinks.end() && !it->second.empty()) {
                GUIHelpers::OpenWebPage(it->second);
            }
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
    if (!m_pPageController) {
        GUIHelpers::ShowErrorDialog(
            m_hWnd,
            GUIHelpers::GetLocalizedText(L"msg.dialog.title.error", L""),
            GUIHelpers::GetLocalizedText(L"msg.dialog.install_controller_missing", L""));
        return;
    }

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

    bool cleanupOldInstall =
        GUIInstallFlowUtils::ConfirmCleanupOldInstall(m_hWnd, metadata, request.installPath);

    std::vector<std::string> processNames = buildKillProcessList(
        metadata.applicationName,
        metadata.installKillProcesses);
    if (!HandleRunningApplicationDialog(m_hWnd, processNames)) {
        return;
    }

    m_pPageController->StartInstallation(request.installPath,
                                         request.autoRun,
                                         request.desktopIcons,
                                         request.languageCode,
                                         cleanupOldInstall, selectedComponents, m_hWnd);

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
        ::PostMessage(m_hWnd, WM_UNINSTALL_COMPLETE, 0, reinterpret_cast<LPARAM>(pData));
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

    RefreshLicenseText();

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

void GUIManager::RefreshLicenseText() {
    CRichEditUI* pLicenseText = static_cast<CRichEditUI*>(
        m_pm.FindControl(_T("editLicense")));
    if (!pLicenseText) {
        return;
    }

    std::wstring languageCode = m_config.languageCode;
    if (languageCode.empty()) {
        languageCode = GetLanguageCodeForIndex(GetDefaultLanguageComboIndex());
    }

    const std::wstring text = LoadLocalizedLicenseText(languageCode);
    pLicenseText->SetText(WStringToTStr(text));
    pLicenseText->SetReadOnly(true);
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






