#pragma once

#include <UIlib.h>

namespace MultiThreadedInstaller {


/// 安装向导的页面类型（对应 TabLayout 的页索引）。
enum class PageType {
    Welcome = 0,     ///< 欢迎/路径选择页。
    License = 1,     ///< 许可协议页。
    Progress = 2,    ///< 安装进度页。
    Completion = 3   ///< 完成页。
};

/// 页面切换控制器：封装 TabLayout 的选页与过渡，并提供许可对话框入口。
class PageController {
public:
    /// @param pTabLayout 承载各页的 Tab 容器（外部持有，不接管生命周期）。
    PageController(DuiLib::CTabLayoutUI* pTabLayout);
    ~PageController();

    /// 切换到指定页（带过渡动画）。
    void NavigateToPage(PageType pageType);
    /// 当前所在页。
    PageType GetCurrentPage() const;
    /// 弹出模态许可协议对话框，返回用户是否同意。
    bool ShowLicenseDialog(HWND hParent);

private:
    DuiLib::CTabLayoutUI* m_pTabLayout;  ///< Tab 容器（外部持有）。
    PageType m_currentPage;              ///< 当前页。

    void ApplyTransitionAnimation();     ///< 应用切页过渡动画。
};

} // namespace MultiThreadedInstaller

