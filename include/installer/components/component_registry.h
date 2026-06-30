#pragma once

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// ── 组件注册表（引擎内置，唯一增删点）────────────────────────────────────────
//
// 「组件」= 随产品一起分发、安装时按需运行的第三方安装程序（浏览器、运行库、驱动等），
// 安装程序位于 <安装目录>\plugins\<relativePath>。组件与 hook 脚本是两套独立机制。
//
// 设计（方案 B）：
//   · 本表（C++）只描述「安装行为」：路径/参数/超时/成功码/重启码/默认勾选/必装/失败策略；
//   · 「是否在界面出现 + 显示名」由皮肤 welcome_page.xml 里的
//     <CheckBox userdata="component:<id>" text="..."> 决定；
//   · GUI 只安装界面上「被勾选且 id 命中本表」的组件；本表里有、但皮肤没有对应勾选框的
//     组件，默认跳过不安装。
//   · 静默模式无界面，改由「本表 defaultSelected/required + CLI 开关」决定。
//
// 增删组件（维护流程）：
//   1) 把安装程序放进 --input/plugins/<...>（解压后即 <安装目录>\plugins\<...>）；
//   2) 在 component_registry.cpp 的表里加/删一个 ComponentSpec 块；
//   3)（GUI 需要展示时）在 welcome_page.xml 加/删对应的 <CheckBox userdata="component:<id>">；
//   4) 重新编译 installer。
//
struct ComponentSpec {
    std::string id;                              ///< 稳定标识；皮肤 userdata="component:<id>"、CLI、日志都用它。
    std::string relativePath;                    ///< 相对 plugins 的安装程序路径，如 "chrome/ChromeSetup.exe"。
    std::string args;                            ///< 传给安装程序的参数（静默开关等）。
    unsigned int timeoutSec = 600;               ///< 超时（秒），0 = 无限等待。
    std::vector<unsigned long> successExitCodes; ///< 视为成功的退出码；为空时按 {0} 处理。
    std::vector<unsigned long> rebootExitCodes;  ///< 命中即「需重启」（如 {3010}）。
    bool defaultSelected = true;                 ///< GUI 默认勾选 / 静默默认选中。
    bool required = false;                        ///< 必装：GUI 勾上且禁用；静默强制选中（不受 --skip 影响）。
    bool onFailureAbort = false;                 ///< 失败处理：true=中止安装并回滚；false=记日志继续。

    // ── 卸载（产品卸载时执行；best-effort，失败仅记日志，不阻断产品卸载）──────────
    // 卸载程序位于 <安装目录>\plugins\<uninstallRelativePath>（与组件文件一同解压）。
    // 可指向组件自带卸载器，或带卸载开关的安装器，或 bat/cmd/ps1/msi（用于 `reg delete` 等清理）。
    std::string uninstallRelativePath;           ///< 相对 plugins 的卸载程序路径；空=无卸载步骤。
    std::string uninstallArgs;                   ///< 卸载参数（如 /uninstall /silent）。
    unsigned int uninstallTimeoutSec = 600;      ///< 卸载超时（秒），0 = 无限等待。
    std::vector<unsigned long> uninstallSuccessExitCodes;  ///< 视为成功的退出码；为空时按 {0}。
};

/// 引擎内置组件表（增删组件只改本函数返回的列表）。
const std::vector<ComponentSpec>& GetComponentRegistry();

/// 按 id 查找组件；未找到返回 nullptr。
const ComponentSpec* FindComponentById(const std::string& id);

/// 退出码判定（successExitCodes 为空时按 {0}）。
bool ComponentExitIsSuccess(const ComponentSpec& spec, unsigned long exitCode);
/// 退出码是否表示「需重启」。
bool ComponentExitNeedsReboot(const ComponentSpec& spec, unsigned long exitCode);
/// 卸载退出码判定（uninstallSuccessExitCodes 为空时按 {0}）。
bool ComponentUninstallExitIsSuccess(const ComponentSpec& spec, unsigned long exitCode);

} // namespace MultiThreadedInstaller
