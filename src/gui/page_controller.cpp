#include "../../include/gui/page_controller.h"
#include "../../include/gui/license_dialog.h"

using namespace DuiLib;

namespace MultiThreadedInstaller {

PageController::PageController(CTabLayoutUI* pTabLayout)
    : m_pTabLayout(pTabLayout)
    , m_currentPage(PageType::Welcome) {

    if (m_pTabLayout) {
        m_pTabLayout->SelectItem(static_cast<int>(PageType::Welcome));
    }
}

PageController::~PageController() {
}

void PageController::NavigateToPage(PageType pageType) {
    if (!m_pTabLayout) {
        return;
    }
    

    m_currentPage = pageType;
    

    ApplyTransitionAnimation();
    

    m_pTabLayout->SelectItem(static_cast<int>(pageType));
}

PageType PageController::GetCurrentPage() const {
    return m_currentPage;
}

void PageController::ApplyTransitionAnimation() {
    if (!m_pTabLayout) {
        return;
    }
    



    


    CControlUI* pCurrentPage = m_pTabLayout->GetItemAt(m_pTabLayout->GetCurSel());
    if (pCurrentPage) {



    }
    


}

bool PageController::ShowLicenseDialog(HWND hParent) {

    LicenseDialog* pDialog = new LicenseDialog();
    

    bool agreed = pDialog->ShowModal(hParent);
    

    delete pDialog;
    
    return agreed;
}

} // namespace MultiThreadedInstaller

