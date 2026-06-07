#pragma once

#include <UIlib.h>

namespace MultiThreadedInstaller {


enum class PageType {
    Welcome = 0,
    License = 1,
    Progress = 2,
    Completion = 3
};

class PageController {
public:
    PageController(DuiLib::CTabLayoutUI* pTabLayout);
    ~PageController();
    

    void NavigateToPage(PageType pageType);
    

    PageType GetCurrentPage() const;
    

    bool ShowLicenseDialog(HWND hParent);
    

private:
    DuiLib::CTabLayoutUI* m_pTabLayout;
    PageType m_currentPage;

    void ApplyTransitionAnimation();
};

} // namespace MultiThreadedInstaller

