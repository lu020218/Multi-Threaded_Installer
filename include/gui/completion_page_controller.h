#pragma once

#ifdef GUI_ENABLED

#include <UIlib.h>
#include <string>

using namespace DuiLib;

namespace MultiThreadedInstaller {

/**
 * CompletionPageController类 - 完成页面控制器
 * 管理安装完成页面的业务逻辑，包括结果显示和后续操作选项
 */
class CompletionPageController {
public:
    CompletionPageController();
    ~CompletionPageController();
    
    // 初始化控件指针
    void Initialize(CPaintManagerUI* pManager);
    
    // 设置安装结果（成功或失败）
    void SetInstallationResult(bool success, const std::wstring& message);
    
    // 检查是否应该运行应用程序
    bool ShouldRunApplication() const;
    
    // 检查是否应该打开网页
    bool ShouldOpenWebPage() const;
    
    // 获取安装结果
    bool IsInstallationSuccessful() const { return m_installSuccess; }
    
    // 重置状态
    void Reset();
    
private:
    // 控件指针
    CLabelUI* m_pResultMessageLabel;
    CCheckBoxUI* m_pRunAppCheckbox;
    CCheckBoxUI* m_pOpenWebCheckbox;
    
    // 状态
    bool m_installSuccess;
    
    // 辅助函数
    void UpdateCheckboxVisibility();
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
