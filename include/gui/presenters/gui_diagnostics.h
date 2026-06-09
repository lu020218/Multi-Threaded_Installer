#pragma once

#include <UIlib.h>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// GUI 资源/控件诊断：用于排查皮肤 XML、图片资源加载问题。

/// 收集当前界面涉及的皮肤 XML 条目集合（按是否卸载模式）。
std::vector<std::string> BuildCurrentGuiXmlScope(const DuiLib::CTabLayoutUI* tabPages,
                                                 bool uninstallMode);

/// 把当前页各控件的图片资源加载情况打成日志快照（stage 标识调用时机）。
void LogCurrentPageControlImageSnapshot(DuiLib::CTabLayoutUI* tabPages,
                                        bool uninstallMode,
                                        const char* stage);

} // namespace MultiThreadedInstaller
