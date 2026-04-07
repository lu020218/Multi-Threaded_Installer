#pragma once

#include <UIlib.h>
#include <string>
#include <memory>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include "../common/archive_types.h"

// Use DuiLib namespace
using namespace DuiLib;

namespace MultiThreadedInstaller {

// Forward declarations
class PageController;
class InstallationWorker;
class UninstallWorker;


struct InstallConfig {
    std::wstring applicationName;
    std::wstring appId;
    std::wstring directoryName;
    std::vector<std::wstring> legacyAppIds;
    bool installDirectoryAppendName;
    std::wstring version;
    std::wstring defaultInstallPath;
    std::wstring registryPath;
    std::wstring registryKey;
    bool autoStartup;
    bool desktopIcons;
    bool repairMode;
    std::wstring logoResourceId;
    std::wstring licenseText;
    std::wstring webPageUrl;
    std::wstring executableName;
    std::wstring languageCode;
    std::wstring postSetupStatePath;
    uint64_t requiredDiskSpace;
    
    InstallConfig()
        : applicationName(L"Application"),
          appId(L""),
          directoryName(L""),
          installDirectoryAppendName(true),
          version(L"1.0.0"),
          defaultInstallPath(L"C:\\Program Files\\MyApp"),
          registryPath(L""),
          registryKey(L""),
          autoStartup(false),
          desktopIcons(false),
          repairMode(false),
          logoResourceId(L"logo.png"),
          webPageUrl(L"https://example.com"),
          executableName(L"app.exe"),
          languageCode(L""),
          postSetupStatePath(L""),
          requiredDiskSpace(100 * 1024 * 1024) {} // 100 MB default
};


#define WM_INSTALLATION_PROGRESS (WM_USER + 1)
#define WM_INSTALLATION_COMPLETE (WM_USER + 2)
#define WM_UNINSTALL_COMPLETE (WM_USER + 3)


struct ProgressMessageData {
    wchar_t currentFolder[MAX_PATH];
    float percentage;
    
    ProgressMessageData() : percentage(0.0f) {
        currentFolder[0] = L'\0';
    }
};


struct CompletionMessageData {
    bool success;
    bool rebootRequired;
    wchar_t errorMessage[512];
    
    CompletionMessageData() : success(false), rebootRequired(false) {
        errorMessage[0] = L'\0';
    }
};

/**
 *
 *
 */
class GUIManager : public WindowImplBase {
public:
    GUIManager();
    virtual ~GUIManager();
    

    void SetInstallConfig(const InstallConfig& config);
    void SetInstallMetadata(const ExtendedInstallationMetadata& metadata);
    void PrepareInitialDpi(unsigned int dpi);
    

    const InstallConfig& GetInstallConfig() const { return m_config; }
    
protected:

    virtual CDuiString GetSkinFolder();
    virtual CDuiString GetSkinFile();
    virtual LPCTSTR GetWindowClassName() const;
    

    virtual void Notify(TNotifyUI& msg);
    virtual void InitWindow();
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    
private:

    CTabLayoutUI* m_pTabPages;
    CEditUI* m_pInstallPathEdit;
    CCheckBoxUI* m_pLicenseCheckbox;
    CButtonUI* m_pInstallButton;
    CLabelUI* m_pDiskSpaceLabel;
    CContainerUI* m_pConfigBottom;
    CContainerUI* m_pMoreInfo;
    

    PageController* m_pPageController;
    

    InstallationWorker* m_pWorker;
    UninstallWorker* m_pUninstallWorker;
    bool m_uninstallMode;
    bool m_repairMode;
    

    InstallConfig m_config;
    int m_baseClientHeight;
    int m_baseClientWidth;
    int m_expandedClientHeight;
    int m_baseWindowWidth;
    ExtendedInstallationMetadata m_installMetadata;
    bool m_installMetadataLoaded;
    std::unordered_map<std::string, std::wstring> m_uiLinks;
    

    void InitControls();
    bool EnsureInstallMetadataLoaded();
    void InitializeComponentSelectionUi();
    std::vector<std::string> CollectSelectedComponentsFromUi();
    

    void OnInstallButtonClick();
    void OnCancelButtonClick();
    void OnBrowseButtonClick();
    void OnLicenseLinkClick();
    void OnLicenseAgreeClick();
    void OnLicenseDisagreeClick();
    void OnFinishButtonClick();
    void OnCancelProgressButtonClick();
    void OnShowMoreClick();
    void OnUninstallConfirmClick();
    void CollapseConfigIfExpanded();
    void ApplyLanguageByIndex(int index);
    void ApplyLanguageByCode(const std::wstring& code);
    void ShowLicensePage();
    void RefreshLicenseText();
    int GetWelcomePageIndex() const;
    int GetProgressPageIndex() const;
    int GetCompletionPageIndex() const;
    

    void OnLicenseCheckboxChanged();
    

    void UpdateInstallButtonState();
    void UpdateDiskSpaceInfo(const std::wstring& path);
    

    void HandleProgressMessage(ProgressMessageData* pData);
    void HandleCompletionMessage(CompletionMessageData* pData);
    void HandleUninstallCompletionMessage(CompletionMessageData* pData);
    void RefreshLocalizedText();
    void StartProgressTimer();
    void StopProgressTimer();
    void TickProgressAnimation();
    void UpdateProgressDisplay(float percentage);

public:
    void SetUninstallMode(bool enabled) { m_uninstallMode = enabled; }
    void SetRepairMode(bool enabled) { m_repairMode = enabled; }

private:
    float m_progressTarget;
    float m_progressDisplayed;
    bool m_progressTimerActive;
    uint64_t m_progressLastTick;
    std::wstring m_progressFolder;
};

} // namespace MultiThreadedInstaller

