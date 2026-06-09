#pragma once

#include <UIlib.h>
#include <string>
#include <vector>

#include "common/archive_types.h"

namespace MultiThreadedInstaller {

// 历史"组件勾选"绑定接口。单产品单载荷重构后已无组件机制：实现为空操作（见 .cpp），
// 仅保留签名以兼容调用点。页面结构/绑定改由 skin 固定承载（需求 §4.4）。

/// 初始化组件勾选 UI —— 现为空操作。
void InitializeComponentBindingsUI(DuiLib::CTabLayoutUI* tabPages,
                                   const ExtendedInstallationMetadata& metadata,
                                   std::vector<std::string>& warnings);

/// 收集 UI 勾选的组件 id —— 现恒返回空（一律全装）。
std::vector<std::string> CollectSelectedComponentIdsFromUI(DuiLib::CTabLayoutUI* tabPages,
                                                           const ExtendedInstallationMetadata& metadata,
                                                           std::vector<std::string>& warnings);

} // namespace MultiThreadedInstaller
