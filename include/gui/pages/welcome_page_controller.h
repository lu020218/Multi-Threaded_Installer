#pragma once

#include <UIlib.h>
#include <string>


namespace MultiThreadedInstaller {

/// 欢迎页控制器：管理安装路径输入、许可勾选、磁盘空间提示与"安装"按钮可用状态。
class WelcomePageController {
public:
    WelcomePageController();
    ~WelcomePageController();

    /// 绑定页内控件（从 paintManager 取得）。
    void Initialize(DuiLib::CPaintManagerUI* pManager);
    /// 按目标路径刷新可用磁盘空间显示，并更新"空间是否足够"判断。
    void UpdateDiskSpaceInfo(const std::wstring& path);
    /// 据许可勾选 + 空间是否足够，联动启用/禁用安装按钮。
    void UpdateInstallButtonState();
    /// 取当前安装路径。
    std::wstring GetInstallPath() const;
    /// 是否已勾选同意许可。
    bool IsLicenseAgreed() const;
    /// 设置安装所需空间（用于充足性判断）。
    void SetRequiredDiskSpace(uint64_t bytes);
    /// 设置默认安装路径并填入编辑框。
    void SetDefaultInstallPath(const std::wstring& path);

private:
    DuiLib::CEditUI* m_pInstallPathEdit;      ///< 安装路径编辑框。
    DuiLib::CCheckBoxUI* m_pLicenseCheckbox;  ///< 许可同意勾选框。
    DuiLib::CButtonUI* m_pInstallButton;      ///< 安装按钮。
    DuiLib::CLabelUI* m_pDiskSpaceLabel;      ///< 磁盘空间标签。

    uint64_t m_requiredDiskSpace;   ///< 所需空间（字节）。
    uint64_t m_availableDiskSpace;  ///< 目标卷可用空间（字节）。
    bool m_hasEnoughSpace;          ///< 空间是否足够。

    uint64_t GetAvailableDiskSpace(const std::wstring& path);  ///< 查目标卷可用空间。
    std::wstring FormatBytes(uint64_t bytes);                  ///< 字节数格式化为可读字符串。
    bool ValidatePath(const std::wstring& path);               ///< 校验路径是否可用。
};

} // namespace MultiThreadedInstaller

