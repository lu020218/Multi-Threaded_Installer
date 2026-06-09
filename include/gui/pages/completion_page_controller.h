#pragma once

#include <UIlib.h>
#include <string>


namespace MultiThreadedInstaller {

/// 完成页控制器：展示安装结果，并提供"运行程序""打开网页"勾选项。
class CompletionPageController {
public:
    CompletionPageController();
    ~CompletionPageController();

    /// 绑定页内控件。
    void Initialize(DuiLib::CPaintManagerUI* pManager);
    /// 设置安装结果（成功/失败 + 结果文案），据此调整勾选项可见性。
    void SetInstallationResult(bool success, const std::wstring& message);
    /// 用户是否勾选"完成后运行程序"。
    bool ShouldRunApplication() const;
    /// 用户是否勾选"打开网页"。
    bool ShouldOpenWebPage() const;
    /// 安装是否成功。
    bool IsInstallationSuccessful() const { return m_installSuccess; }
    /// 重置到初始状态。
    void Reset();

private:
    DuiLib::CLabelUI* m_pResultMessageLabel;  ///< 结果文案标签。
    DuiLib::CCheckBoxUI* m_pRunAppCheckbox;   ///< "运行程序"勾选框。
    DuiLib::CCheckBoxUI* m_pOpenWebCheckbox;  ///< "打开网页"勾选框。

    bool m_installSuccess;  ///< 安装是否成功。

    void UpdateCheckboxVisibility();  ///< 据结果显示/隐藏勾选项。
};

} // namespace MultiThreadedInstaller

