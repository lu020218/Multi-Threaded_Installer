#pragma once

#ifdef GUI_ENABLED

#include <UIlib.h>
#include <string>
#include <memory>
#include "../common/types.h"

// Use DuiLib namespace
using namespace DuiLib;

namespace MultiThreadedInstaller {

// Forward declarations
class PageController;
class InstallationWorker;
class UninstallWorker;

// 安装配置结构
struct InstallConfig {
    std::wstring applicationName;      // 应用程序名称
    std::wstring version;               // 版本号
    std::wstring defaultInstallPath;    // 默认安装路径
    std::wstring registryPath;          // 注册表路径（读取安装目录）
    std::wstring registryKey;           // 注册表键名（读取安装目录）
    bool autoStartup;                   // 默认开机启动
    bool desktopIcons;                  // 默认创建桌面图标
    std::wstring logoResourceId;        // Logo资源ID
    std::wstring licenseText;           // 许可协议文本
    std::wstring webPageUrl;            // 介绍网页URL
    std::wstring executableName;        // 可执行文件名（用于启动）
    uint64_t requiredDiskSpace;         // 所需磁盘空间（字节）
    
    InstallConfig()
        : applicationName(L"Application"),
          version(L"1.0.0"),
          defaultInstallPath(L"C:\\Program Files\\MyApp"),
          registryPath(L""),
          registryKey(L""),
          autoStartup(false),
          desktopIcons(false),
          logoResourceId(L"logo.png"),
          webPageUrl(L"https://example.com"),
          executableName(L"app.exe"),
          requiredDiskSpace(100 * 1024 * 1024) {} // 100 MB default
};

// 自定义Windows消息
#define WM_INSTALLATION_PROGRESS (WM_USER + 1)
#define WM_INSTALLATION_COMPLETE (WM_USER + 2)
#define WM_UNINSTALL_COMPLETE (WM_USER + 3)

// 进度消息数据结构
struct ProgressMessageData {
    wchar_t currentFolder[MAX_PATH];
    float percentage;
    
    ProgressMessageData() : percentage(0.0f) {
        currentFolder[0] = L'\0';
    }
};

// 完成消息数据结构
struct CompletionMessageData {
    bool success;
    wchar_t errorMessage[512];
    
    CompletionMessageData() : success(false) {
        errorMessage[0] = L'\0';
    }
};

/**
 * GUIManager类 - 主窗口管理器
 * 继承自DuiLib::WindowImplBase，管理安装程序的GUI界面
 */
class GUIManager : public WindowImplBase {
public:
    GUIManager();
    virtual ~GUIManager();
    
    // 设置安装配置
    void SetInstallConfig(const InstallConfig& config);
    
    // 获取安装配置
    const InstallConfig& GetInstallConfig() const { return m_config; }
    
protected:
    // DuiLib虚函数重写
    virtual CDuiString GetSkinFolder();
    virtual CDuiString GetSkinFile();
    virtual LPCTSTR GetWindowClassName() const;
    
    // 消息处理
    virtual void Notify(TNotifyUI& msg);
    virtual void InitWindow();
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    
private:
    // 控件指针
    CTabLayoutUI* m_pTabPages;          // 页面容器
    CEditUI* m_pInstallPathEdit;        // 安装路径输入框
    CCheckBoxUI* m_pLicenseCheckbox;    // 许可协议复选框
    CButtonUI* m_pInstallButton;        // 安装按钮
    CLabelUI* m_pDiskSpaceLabel;        // 磁盘空间标签
    CContainerUI* m_pConfigBottom;      // 更多配置容器
    CContainerUI* m_pMoreInfo;          // 更多配置内容
    
    // 页面控制器
    PageController* m_pPageController;
    
    // 安装工作线程
    InstallationWorker* m_pWorker;
    UninstallWorker* m_pUninstallWorker;
    bool m_uninstallMode;
    
    // 配置
    InstallConfig m_config;
    int m_baseClientHeight;
    int m_baseClientWidth;
    int m_expandedClientHeight;
    int m_baseWindowWidth;
    
    // 初始化控件
    void InitControls();
    
    // 按钮点击处理
    void OnInstallButtonClick();
    void OnCancelButtonClick();
    void OnBrowseButtonClick();
    void OnLicenseLinkClick();
    void OnFinishButtonClick();
    void OnCancelProgressButtonClick();
    void OnShowMoreClick();
    void OnUninstallConfirmClick();
    void CollapseConfigIfExpanded();
    void ApplyLanguageByIndex(int index);
    void ApplyLanguageByCode(const std::wstring& code);
    
    // 复选框状态变化处理
    void OnLicenseCheckboxChanged();
    
    // 更新UI状态
    void UpdateInstallButtonState();
    void UpdateDiskSpaceInfo(const std::wstring& path);
    
    // 处理自定义消息
    void HandleProgressMessage(ProgressMessageData* pData);
    void HandleCompletionMessage(CompletionMessageData* pData);
    void HandleUninstallCompletionMessage(CompletionMessageData* pData);

public:
    void SetUninstallMode(bool enabled) { m_uninstallMode = enabled; }
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
