#pragma once

#include <UIlib.h>
#include <string>


namespace MultiThreadedInstaller {

/// 模态许可协议对话框（DuiLib 窗口）：显示协议全文，提供同意/不同意。
class LicenseDialog : public DuiLib::WindowImplBase {
public:
    LicenseDialog();
    virtual ~LicenseDialog();

    /// 模态显示，阻塞至关闭；返回用户是否点击了"同意"。
    bool ShowModal(HWND hParent);

protected:
    // DuiLib WindowImplBase 回调：皮肤定位、窗口类名、事件通知、初始化。
    virtual DuiLib::CDuiString GetSkinFolder();          ///< 皮肤目录。
    virtual DuiLib::CDuiString GetSkinFile();            ///< 皮肤 XML 文件。
    virtual LPCTSTR GetWindowClassName() const;          ///< 窗口类名。
    virtual void Notify(DuiLib::TNotifyUI& msg);         ///< 控件事件分发。
    virtual void InitWindow();                           ///< 窗口初始化（绑定控件、填文本）。

private:
    bool m_agreed;                          ///< 用户是否同意。
    bool m_modalResult;                     ///< 模态结果。
    DuiLib::CRichEditUI* m_pLicenseText;    ///< 协议文本控件。

    std::wstring LoadLicenseText();         ///< 加载本地化协议文本。
    void OnAgreeButtonClick();              ///< "同意"点击。
    void OnDisagreeButtonClick();           ///< "不同意"点击。
};

} // namespace MultiThreadedInstaller

