#pragma once

#include <UIlib.h>
#include <string>


namespace MultiThreadedInstaller {

class LicenseDialog : public DuiLib::WindowImplBase {
public:
    LicenseDialog();
    virtual ~LicenseDialog();
    


    bool ShowModal(HWND hParent);
    
protected:

    virtual DuiLib::CDuiString GetSkinFolder();
    virtual DuiLib::CDuiString GetSkinFile();
    virtual LPCTSTR GetWindowClassName() const;
    virtual void Notify(DuiLib::TNotifyUI& msg);
    virtual void InitWindow();
    
private:
    bool m_agreed;
    bool m_modalResult;
    DuiLib::CRichEditUI* m_pLicenseText;
    

    std::wstring LoadLicenseText();
    

    void OnAgreeButtonClick();
    void OnDisagreeButtonClick();
};

} // namespace MultiThreadedInstaller

