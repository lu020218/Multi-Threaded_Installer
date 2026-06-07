#pragma once

#include <UIlib.h>
#include <string>


namespace MultiThreadedInstaller {

/**
 *
 *
 */
class CompletionPageController {
public:
    CompletionPageController();
    ~CompletionPageController();
    

    void Initialize(DuiLib::CPaintManagerUI* pManager);
    

    void SetInstallationResult(bool success, const std::wstring& message);
    

    bool ShouldRunApplication() const;
    

    bool ShouldOpenWebPage() const;
    

    bool IsInstallationSuccessful() const { return m_installSuccess; }
    

    void Reset();
    
private:

    DuiLib::CLabelUI* m_pResultMessageLabel;
    DuiLib::CCheckBoxUI* m_pRunAppCheckbox;
    DuiLib::CCheckBoxUI* m_pOpenWebCheckbox;
    

    bool m_installSuccess;
    

    void UpdateCheckboxVisibility();
};

} // namespace MultiThreadedInstaller

