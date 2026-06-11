#pragma once

#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

// 单例启动守护（按 instanceKey 区分，如 "<产品>|installer" / "<产品>|uninstaller"）。
//
// 构造时尝试创建该 key 的命名互斥量：
//   acquired()==true  → 本进程是首个实例，持有互斥量直至析构（一般直到进程退出）。
//   acquired()==false → 已有同类实例在运行；调用方应在 activateExistingWindow() 后退出本进程。
//
// 互斥量只判定“是否已有实例”；“已有实例是否 GUI、其窗口句柄”由共享内存
// （PublishGuiInstanceWindow 发布）提供，从而区分“置顶已有 GUI”与“静默实例直接退出”。
class SingleInstanceGuard {
public:
    explicit SingleInstanceGuard(const std::string& instanceKey);
    ~SingleInstanceGuard();
    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    /// 本进程是否为首个实例。
    bool acquired() const { return acquired_; }

    /// 已有实例运行时调用：若其为 GUI 实例（已发布窗口句柄）则还原并置顶其窗口。
    /// @return true  = 已有实例是 GUI，且其窗口已被置顶；
    ///         false = 已有实例是静默实例（无窗口可置顶），调用方可据此决定是否弹提示。
    bool activateExistingWindow() const;

private:
    std::string key_;
    bool acquired_ = false;
#ifdef _WIN32
    HANDLE mutex_ = nullptr;
#endif
};

#ifdef _WIN32
/// GUI 主窗口创建后调用：把窗口句柄发布到 instanceKey 对应的共享内存，
/// 供后续实例查找并置顶。共享内存在进程内保持打开，直到 ClearGuiInstanceWindow。
void PublishGuiInstanceWindow(const std::string& instanceKey, HWND hwnd);
/// GUI 退出时调用：关闭已发布的窗口句柄共享内存（之后其它实例将其视为“非 GUI”）。
void ClearGuiInstanceWindow();
#endif

} // namespace MultiThreadedInstaller
