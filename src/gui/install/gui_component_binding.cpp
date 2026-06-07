#include "gui/install/gui_component_binding.h"

using namespace DuiLib;

namespace MultiThreadedInstaller {

// 单产品单载荷：已无组件勾选。页面结构与"勾选框↔组件"绑定由 skin 承载，引擎固定加载
// （需求 §4.4）。这两个入口保留为空实现，安装一律视为安装全部 payload。

void InitializeComponentBindingsUI(CTabLayoutUI* tabPages,
                                   const ExtendedInstallationMetadata& metadata,
                                   std::vector<std::string>& warnings) {
    (void)tabPages;
    (void)metadata;
    (void)warnings;
}

std::vector<std::string> CollectSelectedComponentIdsFromUI(CTabLayoutUI* tabPages,
                                                           const ExtendedInstallationMetadata& metadata,
                                                           std::vector<std::string>& warnings) {
    (void)tabPages;
    (void)metadata;
    (void)warnings;
    return {};
}

} // namespace MultiThreadedInstaller
