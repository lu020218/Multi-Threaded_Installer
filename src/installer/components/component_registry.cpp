#include "installer/components/component_registry.h"

#include <algorithm>

namespace MultiThreadedInstaller {

const std::vector<ComponentSpec>& GetComponentRegistry() {
    // ── 组件表（唯一增删点）──────────────────────────────────────────────
    // 每个组件一段；增删组件只改这里（再配合 welcome_page.xml 的勾选框 + plugins 载荷）。
    // relativePath 相对 <安装目录>\plugins 解析。
    static const std::vector<ComponentSpec> kRegistry = []() {
        std::vector<ComponentSpec> registry;

        // 示例：浏览器组件（默认勾选，可选，失败仅记日志继续）。
        // 皮肤需有 <CheckBox userdata="component:chrome" ...>；安装程序位于 plugins\chrome\。
        {
            ComponentSpec chrome;
            chrome.id = "chrome";
            chrome.relativePath = "chrome/ChromeSetup.exe";
            chrome.args = "/silent /install";
            chrome.timeoutSec = 600;
            chrome.successExitCodes = {0};
            chrome.rebootExitCodes = {3010};
            chrome.defaultSelected = true;
            chrome.required = false;
            chrome.onFailureAbort = false;
            // 卸载：复用安装器的卸载开关（示例）；也可指向独立卸载器或脚本。
            chrome.uninstallRelativePath = "chrome/ChromeSetup.exe";
            chrome.uninstallArgs = "/uninstall /silent";
            chrome.uninstallSuccessExitCodes = {0};
            registry.push_back(std::move(chrome));
        }

        // 在此按需追加更多组件，例如：
        // {
        //     ComponentSpec vcredist;
        //     vcredist.id = "vcredist";
        //     vcredist.relativePath = "vcredist/vc_redist.x64.exe";
        //     vcredist.args = "/install /quiet /norestart";
        //     vcredist.successExitCodes = {0, 1638, 3010};
        //     vcredist.rebootExitCodes = {3010};
        //     vcredist.defaultSelected = true;
        //     vcredist.required = true;      // 必装：界面勾上禁用、静默强制
        //     vcredist.onFailureAbort = true;
        //     registry.push_back(std::move(vcredist));
        // }

        return registry;
    }();
    return kRegistry;
}

const ComponentSpec* FindComponentById(const std::string& id) {
    if (id.empty()) {
        return nullptr;
    }
    const auto& registry = GetComponentRegistry();
    auto it = std::find_if(registry.begin(), registry.end(),
                           [&id](const ComponentSpec& spec) { return spec.id == id; });
    return it == registry.end() ? nullptr : &(*it);
}

bool ComponentExitIsSuccess(const ComponentSpec& spec, unsigned long exitCode) {
    if (spec.successExitCodes.empty()) {
        return exitCode == 0;
    }
    return std::find(spec.successExitCodes.begin(), spec.successExitCodes.end(), exitCode) !=
           spec.successExitCodes.end();
}

bool ComponentExitNeedsReboot(const ComponentSpec& spec, unsigned long exitCode) {
    return std::find(spec.rebootExitCodes.begin(), spec.rebootExitCodes.end(), exitCode) !=
           spec.rebootExitCodes.end();
}

bool ComponentUninstallExitIsSuccess(const ComponentSpec& spec, unsigned long exitCode) {
    if (spec.uninstallSuccessExitCodes.empty()) {
        return exitCode == 0;
    }
    return std::find(spec.uninstallSuccessExitCodes.begin(), spec.uninstallSuccessExitCodes.end(),
                     exitCode) != spec.uninstallSuccessExitCodes.end();
}

} // namespace MultiThreadedInstaller
