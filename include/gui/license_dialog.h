#pragma once

#include <UIlib.h>
#include <string>

using namespace DuiLib;

namespace MultiThreadedInstaller {

class LicenseDialog : public WindowImplBase {
public:
    LicenseDialog();
    virtual ~LicenseDialog();
    


    bool ShowModal(HWND hParent);
    
protected:

    virtual CDuiString GetSkinFolder();
    virtual CDuiString GetSkinFile();
    virtual LPCTSTR GetWindowClassName() const;
    virtual void Notify(TNotifyUI& msg);
    virtual void InitWindow();
    
private:
    bool m_agreed;
    bool m_modalResult;
    CRichEditUI* m_pLicenseText;
    

    std::wstring LoadLicenseText();
    

    void OnAgreeButtonClick();
    void OnDisagreeButtonClick();
};

} // namespace MultiThreadedInstaller

