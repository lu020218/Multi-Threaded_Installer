#pragma once

#ifdef GUI_ENABLED

#include <UIlib.h>
#include <string>

using namespace DuiLib;

namespace MultiThreadedInstaller {

/**
 * WelcomePageController类 - 欢迎页面控制器
 * 管理欢迎页面的业务逻辑，包括路径选择、磁盘空间验证和许可协议确认
 */
class WelcomePageController {
public:
    WelcomePageController();
    ~WelcomePageController();
    
    // 初始化控件指针
    void Initialize(CPaintManagerUI* pManager);
    
    // 更新磁盘空间信息
    void UpdateDiskSpaceInfo(const std::wstring& path);
    
    // 更新安装按钮状态（根据许可协议和磁盘空间）
    void UpdateInstallButtonState();
    
    // 获取安装路径
    std::wstring GetInstallPath() const;
    
    // 检查许可协议是否已同意
    bool IsLicenseAgreed() const;
    
    // 设置所需磁盘空间
    void SetRequiredDiskSpace(uint64_t bytes);
    
    // 设置默认安装路径
    void SetDefaultInstallPath(const std::wstring& path);
    
private:
    // 控件指针
    CEditUI* m_pInstallPathEdit;
    CCheckBoxUI* m_pLicenseCheckbox;
    CButtonUI* m_pInstallButton;
    CLabelUI* m_pDiskSpaceLabel;
    
    // 状态
    uint64_t m_requiredDiskSpace;
    uint64_t m_availableDiskSpace;
    bool m_hasEnoughSpace;
    
    // 辅助函数
    uint64_t GetAvailableDiskSpace(const std::wstring& path);
    std::wstring FormatBytes(uint64_t bytes);
    bool ValidatePath(const std::wstring& path);
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
