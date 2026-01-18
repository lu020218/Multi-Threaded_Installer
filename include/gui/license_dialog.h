#pragma once

#ifdef GUI_ENABLED

#include <UIlib.h>
#include <string>

using namespace DuiLib;

namespace MultiThreadedInstaller {

class LicenseDialog : public WindowImplBase {
public:
    LicenseDialog();
    virtual ~LicenseDialog();
    
    // 显示对话框（模态）
    // 返回true表示用户同意，false表示不同意
    bool ShowModal(HWND hParent);
    
protected:
    // DuiLib虚函数重写
    virtual CDuiString GetSkinFolder();
    virtual CDuiString GetSkinFile();
    virtual LPCTSTR GetWindowClassName() const;
    virtual void Notify(TNotifyUI& msg);
    virtual void InitWindow();
    
private:
    bool m_agreed;
    bool m_modalResult;
    CRichEditUI* m_pLicenseText;
    
    // 加载许可协议文本
    std::wstring LoadLicenseText();
    
    // 处理按钮点击
    void OnAgreeButtonClick();
    void OnDisagreeButtonClick();
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
