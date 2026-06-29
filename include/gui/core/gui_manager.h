#pragma once

#include <UIlib.h>
#include <string>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include "common/archive_types.h"
#include "installer/uninstall/uninstall_manager.h"

// Use DuiLib namespace

namespace MultiThreadedInstaller {

// Forward declarations
class PageController;
class InstallationWorker;
class UninstallWorker;


/// GUI 展示/交互所需的配置（多由运行期元数据派生）。
struct InstallConfig {
    std::wstring applicationName;     ///< 产品显示名（窗口标题等）。
    std::wstring appId;               ///< 应用标识（=产品名）。
    std::wstring directoryName;       ///< 目录名（=产品名）。
    std::wstring version;             ///< 版本号。
    std::wstring defaultInstallPath;  ///< 默认安装路径。
    std::wstring registryPath;        ///< 注册表路径（GUI 展示用，通常空）。
    std::wstring registryKey;         ///< 注册表键（同上）。
    bool autoStartup;                 ///< 开机自启默认勾选。
    bool desktopIcons;                ///< 桌面快捷方式默认勾选。
    bool overwriteMode;               ///< 是否覆盖安装（检出旧安装）。
    std::wstring logoResourceId;      ///< Logo 资源 id。
    std::wstring licenseText;         ///< 许可协议文本（或资源引用）。
    std::wstring webPageUrl;          ///< 完成页可打开的网址。
    std::wstring executableName;      ///< 主程序文件名。
    std::wstring languageCode;        ///< 界面语言。
    uint64_t requiredDiskSpace;       ///< 安装所需磁盘空间（字节）。

    InstallConfig()
        : applicationName(L"Application"),
          appId(L""),
          directoryName(L""),
          version(L"1.0.0"),
          defaultInstallPath(L"C:\\Program Files\\MyApp"),
          registryPath(L""),
          registryKey(L""),
          autoStartup(false),
          desktopIcons(false),
          overwriteMode(false),
          logoResourceId(L"logo.png"),
          webPageUrl(L"https://example.com"),
          executableName(L"app.exe"),
          languageCode(L""),
          requiredDiskSpace(100 * 1024 * 1024) {} // 100 MB default
};


// 安装 worker 在后台线程产生、通过 PostMessage 投递给 GUI 线程的窗口消息（WM_USER+n）。
#define WM_INSTALLATION_PROGRESS (WM_USER + 1)  ///< 安装进度。
#define WM_INSTALLATION_COMPLETE (WM_USER + 2)  ///< 安装完成。
#define WM_UNINSTALL_COMPLETE (WM_USER + 3)     ///< 卸载完成。
#define WM_UNINSTALL_PROGRESS (WM_USER + 4)     ///< 卸载进度。

constexpr size_t kProgressItemTextMax = 1024;  ///< 进度文本字段最大长度。

/// 进度消息负载（堆分配，经 PostMessage 传给 GUI 线程，由接收方释放）。
struct ProgressMessageData {
    wchar_t progressPrefix[64];                 ///< 进度前缀（阶段名）。
    wchar_t currentFolder[kProgressItemTextMax];///< 当前文件夹/文件。
    float percentage;                           ///< 进度百分比 [0..100]。

    ProgressMessageData() : percentage(0.0f) {
        progressPrefix[0] = L'\0';
        currentFolder[0] = L'\0';
    }
};

/// 完成消息负载（同样经 PostMessage 传递，接收方释放）。
struct CompletionMessageData {
    bool success;                 ///< 是否成功。
    bool rebootRequired;          ///< 是否需重启。
    wchar_t errorMessage[512];    ///< 失败信息。

    CompletionMessageData() : success(false), rebootRequired(false) {
        errorMessage[0] = L'\0';
    }
};

/// 主窗口/总控制器：基于 DuiLib，负责建窗、页面切换、事件路由、启动安装/卸载 worker，
/// 并处理 worker 经 PostMessage 回传的进度/完成消息。
class GUIManager : public DuiLib::WindowImplBase {
public:
    GUIManager();
    virtual ~GUIManager();
    

    void SetInstallConfig(const InstallConfig& config);
    void SetInstallMetadata(const ExtendedInstallationMetadata& metadata);
    void SetUninstallContext(const UninstallContext& context);
    void SetUninstallManifestPath(const std::string& manifestPath);
    void SetAutoStartInstallRequest(const std::wstring& installPath,
                                    bool autoRun,
                                    bool desktopIcons,
                                    const std::wstring& languageCode,
                                    bool upgradeMode = false);
    void PrepareInitialDpi(unsigned int dpi);
    

    const InstallConfig& GetInstallConfig() const { return m_config; }
    
protected:

    virtual DuiLib::CDuiString GetSkinFolder();
    virtual DuiLib::CDuiString GetSkinFile();
    virtual LPCTSTR GetWindowClassName() const;
    

    virtual void Notify(DuiLib::TNotifyUI& msg);
    virtual void InitWindow();
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    
private:
    // DuiLib 控件指针（由 DuiLib 框架管理生命周期，InitWindow 时 FindControl 取得，不手工 delete）。
    DuiLib::CTabLayoutUI* m_pTabPages;        ///< 页面容器（欢迎/许可/进度/完成）。
    DuiLib::CEditUI* m_pInstallPathEdit;      ///< 安装路径编辑框。
    DuiLib::CCheckBoxUI* m_pLicenseCheckbox;  ///< 许可同意勾选框。
    DuiLib::CButtonUI* m_pInstallButton;      ///< 安装按钮。
    DuiLib::CLabelUI* m_pDiskSpaceLabel;      ///< 磁盘空间提示标签。
    DuiLib::CContainerUI* m_pConfigBottom;    ///< 配置区底部容器。
    DuiLib::CContainerUI* m_pMoreInfo;        ///< "更多信息"容器。

    std::unique_ptr<PageController> m_pPageController;  ///< 页面切换控制器（本类拥有）。

    std::unique_ptr<InstallationWorker> m_pWorker;          ///< 安装后台 worker（本类拥有）。
    std::unique_ptr<UninstallWorker> m_pUninstallWorker;    ///< 卸载后台 worker（本类拥有）。
    bool m_uninstallMode;     ///< 当前是否卸载模式。
    bool m_overwriteMode;     ///< 是否覆盖安装。

    InstallConfig m_config;               ///< GUI 配置。
    int m_baseClientHeight;               ///< 基准客户区高度（DPI 缩放基准）。
    int m_baseClientWidth;                ///< 基准客户区宽度。
    int m_expandedClientHeight;           ///< 展开"更多"后的高度。
    int m_baseWindowWidth;                ///< 基准窗口宽度。
    ExtendedInstallationMetadata m_installMetadata;  ///< 运行期元数据。
    bool m_installMetadataLoaded;         ///< 元数据是否已加载。
    std::unordered_map<std::string, std::wstring> m_uiLinks;  ///< 控件名→外链 URL。
    UninstallContext m_uninstallContext;  ///< 卸载上下文（卸载模式用）。
    std::string m_uninstallManifestPath;  ///< 卸载清单路径。
    bool m_autoStartInstall;              ///< 是否进入即自动开始安装（升级/静默驱动）。
    std::wstring m_autoStartInstallPath;  ///< 自动安装目标路径。
    bool m_autoStartAutoRun;              ///< 自动安装：开机自启。
    bool m_autoStartDesktopIcons;         ///< 自动安装：桌面快捷方式。
    std::wstring m_autoStartLanguageCode; ///< 自动安装：语言。
    bool m_autoStartUpgradeMode;

    void InitControls();
    bool EnsureInstallMetadataLoaded();

    void OnInstallButtonClick();
    /// 应用组件勾选框默认态（按引擎注册表 defaultSelected/required；皮肤有而注册表无的禁用）。
    void ApplyComponentCheckboxDefaults();
    bool StartInstallationWithOptions(const std::wstring& installPath,
                                      bool autoRun,
                                      bool desktopIcons,
                                      const std::wstring& languageCode,
                                      bool upgradeMode,
                                      const std::vector<std::string>& selectedComponentIds);
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
    void ResizeForProgressPage();
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
    void UpdateWindowTitle();
    void StartProgressTimer();
    void StopProgressTimer();
    void TickProgressAnimation();
    void UpdateProgressDisplay(float percentage);
    void StartCarousel();
    void StopCarousel();
    void TickCarousel();
    void ShowCarouselItem(int index);
    void OnCarouselDotClick(int index);
    void ApplyCarouselImagesForLanguage();

public:
    void SetUninstallMode(bool enabled) { m_uninstallMode = enabled; }
    void SetOverwriteMode(bool enabled) { m_overwriteMode = enabled; }

private:
    float m_progressTarget;
    float m_progressDisplayed;
    bool m_progressTimerActive;
    uint64_t m_progressLastTick;
    std::wstring m_progressPrefix;
    std::wstring m_progressFolder;
    bool m_carouselActive;
    int m_carouselIndex;
};

} // namespace MultiThreadedInstaller

