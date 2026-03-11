#pragma once

#include <UIlib.h>
#include <string>

using namespace DuiLib;

namespace MultiThreadedInstaller {

/**
 *
 *
 */
class CompletionPageController {
public:
    CompletionPageController();
    ~CompletionPageController();
    

    void Initialize(CPaintManagerUI* pManager);
    

    void SetInstallationResult(bool success, const std::wstring& message);
    

    bool ShouldRunApplication() const;
    

    bool ShouldOpenWebPage() const;
    

    bool IsInstallationSuccessful() const { return m_installSuccess; }
    

    void Reset();
    
private:

    CLabelUI* m_pResultMessageLabel;
    CCheckBoxUI* m_pRunAppCheckbox;
    CCheckBoxUI* m_pOpenWebCheckbox;
    

    bool m_installSuccess;
    

    void UpdateCheckboxVisibility();
};

} // namespace MultiThreadedInstaller

