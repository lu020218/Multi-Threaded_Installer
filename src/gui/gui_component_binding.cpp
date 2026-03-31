#include "../../include/gui/gui_component_binding.h"

#include "common/utf8_utils.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

using namespace DuiLib;

namespace MultiThreadedInstaller {

namespace {

struct ComponentControlBinding {
    CCheckBoxUI* checkBox = nullptr;
    std::string componentId;
    std::string controlName;
};

struct ComponentPageScope {
    CControlUI* root = nullptr;
    std::unordered_set<std::string> allowedControls;
};

std::string TrimAsciiCopy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(start, end - start);
}

std::string ToLowerAsciiCopy(const std::string& text) {
    std::string lowered = text;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

void CollectControlsRecursive(CControlUI* root, std::vector<CControlUI*>& controls) {
    if (!root) {
        return;
    }
    controls.push_back(root);
    CContainerUI* container = static_cast<CContainerUI*>(root->GetInterface(_T("Container")));
    if (!container) {
        return;
    }
    const int count = container->GetCount();
    for (int i = 0; i < count; ++i) {
        CollectControlsRecursive(container->GetItemAt(i), controls);
    }
}

std::string NormalizeSkinName(const std::string& skin) {
    std::string normalized = ToLowerAsciiCopy(skin);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    size_t slashPos = normalized.find_last_of('/');
    if (slashPos != std::string::npos) {
        normalized = normalized.substr(slashPos + 1);
    }
    return normalized;
}

int ResolveInstallPageIndex(const std::string& skin) {
    const std::string normalized = NormalizeSkinName(skin);
    if (normalized == "welcome_page.xml") {
        return 0;
    }
    if (normalized == "license_page.xml") {
        return 1;
    }
    if (normalized == "progress_page.xml") {
        return 2;
    }
    if (normalized == "completion_page.xml") {
        return 3;
    }
    return -1;
}

bool IsEmbeddedSelectionMode(const std::string& mode) {
    if (mode.empty()) {
        return false;
    }
    const std::string lowered = ToLowerAsciiCopy(mode);
    return lowered == "embeddedinexistingpages" || lowered == "hybrid";
}

std::vector<ComponentControlBinding> CollectComponentBindings(CTabLayoutUI* tabPages,
                                                              const ExtendedInstallationMetadata& metadata,
                                                              std::vector<std::string>* warnings) {
    std::vector<ComponentControlBinding> bindings;
    if (!tabPages || metadata.layoutComponents.empty()) {
        return bindings;
    }

    const UiComponentSelectionConfig& ui = metadata.uiComponentSelection;
    if (!IsEmbeddedSelectionMode(ui.mode)) {
        return bindings;
    }

    const std::string strategy = ToLowerAsciiCopy(ui.strategy);
    if (!strategy.empty() && strategy != "xml_userdata") {
        if (warnings) {
            warnings->push_back("Unsupported component selection strategy: " + ui.strategy);
        }
        return bindings;
    }

    std::string tokenPrefix = ui.tokenPrefix;
    if (tokenPrefix.empty()) {
        tokenPrefix = "component:";
    }

    std::vector<ComponentPageScope> scopes;
    if (!ui.pages.empty()) {
        for (const auto& page : ui.pages) {
            const int index = ResolveInstallPageIndex(page.skin);
            if (index < 0 || index >= tabPages->GetCount()) {
                if (warnings) {
                    warnings->push_back("Component binding page not found in installer tabs: " + page.skin);
                }
                continue;
            }
            CControlUI* root = tabPages->GetItemAt(index);
            if (!root) {
                continue;
            }
            ComponentPageScope scope;
            scope.root = root;
            for (const auto& controlName : page.controls) {
                if (!controlName.empty()) {
                    scope.allowedControls.insert(ToLowerAsciiCopy(controlName));
                }
            }
            scopes.push_back(std::move(scope));
        }
    } else {
        const int maxIndex = (std::min)(tabPages->GetCount(), 4);
        for (int i = 0; i < maxIndex; ++i) {
            ComponentPageScope scope;
            scope.root = tabPages->GetItemAt(i);
            scopes.push_back(std::move(scope));
        }
    }

    std::unordered_set<CCheckBoxUI*> seenControls;
    for (const auto& scope : scopes) {
        if (!scope.root) {
            continue;
        }

        std::vector<CControlUI*> controls;
        controls.reserve(64);
        CollectControlsRecursive(scope.root, controls);

        for (CControlUI* control : controls) {
            if (!control) {
                continue;
            }
            CCheckBoxUI* checkBox = static_cast<CCheckBoxUI*>(control->GetInterface(_T("CheckBox")));
            if (!checkBox || !seenControls.insert(checkBox).second) {
                continue;
            }

            const std::string controlName =
                ToLowerAsciiCopy(WideToUtf8(TCharToWide(control->GetName().GetData())));
            if (!scope.allowedControls.empty() &&
                scope.allowedControls.find(controlName) == scope.allowedControls.end()) {
                continue;
            }

            const std::string userData = WideToUtf8(TCharToWide(checkBox->GetUserData().GetData()));
            if (userData.size() < tokenPrefix.size() ||
                userData.compare(0, tokenPrefix.size(), tokenPrefix) != 0) {
                continue;
            }

            std::string componentId = TrimAsciiCopy(userData.substr(tokenPrefix.size()));
            if (componentId.empty()) {
                if (warnings) {
                    warnings->push_back("Empty component id in checkbox userdata: " +
                                        WideToUtf8(TCharToWide(checkBox->GetName().GetData())));
                }
                continue;
            }

            ComponentControlBinding binding;
            binding.checkBox = checkBox;
            binding.componentId = componentId;
            binding.controlName = controlName;
            bindings.push_back(std::move(binding));
        }
    }

    return bindings;
}

void ApplyComponentBindingConstraints(const ExtendedInstallationMetadata& metadata,
                                      const std::vector<ComponentControlBinding>& bindings,
                                      bool applyDefaults,
                                      std::vector<std::string>* warnings) {
    std::unordered_map<std::string, const ComponentConfig*> componentIndex;
    componentIndex.reserve(metadata.layoutComponents.size());
    for (const auto& component : metadata.layoutComponents) {
        componentIndex[component.id] = &component;
    }

    for (const auto& binding : bindings) {
        auto it = componentIndex.find(binding.componentId);
        if (it == componentIndex.end()) {
            if (warnings) {
                warnings->push_back("UI checkbox references unknown component id: " + binding.componentId);
            }
            continue;
        }
        const ComponentConfig* component = it->second;
        if (!component) {
            continue;
        }
        if (component->required) {
            binding.checkBox->SetCheck(true);
            binding.checkBox->SetEnabled(false);
            binding.checkBox->SetMouseEnabled(false);
            continue;
        }
        if (applyDefaults) {
            binding.checkBox->SetCheck(component->defaultSelected);
        }
    }
}

std::vector<std::string> CollectSelectedComponentIds(const ExtendedInstallationMetadata& metadata,
                                                     const std::vector<ComponentControlBinding>& bindings,
                                                     std::vector<std::string>* warnings) {
    std::unordered_map<std::string, const ComponentConfig*> componentIndex;
    componentIndex.reserve(metadata.layoutComponents.size());
    for (const auto& component : metadata.layoutComponents) {
        componentIndex[component.id] = &component;
    }

    std::unordered_set<std::string> selected;
    selected.reserve(metadata.layoutComponents.size());
    for (const auto& component : metadata.layoutComponents) {
        if (component.required) {
            selected.insert(component.id);
        }
    }

    for (const auto& binding : bindings) {
        if (componentIndex.find(binding.componentId) == componentIndex.end()) {
            if (warnings) {
                warnings->push_back("Skipping unknown component id from UI: " + binding.componentId);
            }
            continue;
        }
        if (binding.checkBox->GetCheck()) {
            selected.insert(binding.componentId);
        }
    }

    std::vector<std::string> ordered;
    ordered.reserve(selected.size());
    for (const auto& component : metadata.layoutComponents) {
        if (selected.find(component.id) != selected.end()) {
            ordered.push_back(component.id);
        }
    }
    return ordered;
}

} // namespace

void InitializeComponentBindingsUI(CTabLayoutUI* tabPages,
                                   const ExtendedInstallationMetadata& metadata,
                                   std::vector<std::string>& warnings) {
    std::vector<ComponentControlBinding> bindings =
        CollectComponentBindings(tabPages, metadata, &warnings);
    ApplyComponentBindingConstraints(metadata, bindings, true, &warnings);
}

std::vector<std::string> CollectSelectedComponentIdsFromUI(CTabLayoutUI* tabPages,
                                                           const ExtendedInstallationMetadata& metadata,
                                                           std::vector<std::string>& warnings) {
    std::vector<ComponentControlBinding> bindings =
        CollectComponentBindings(tabPages, metadata, &warnings);
    ApplyComponentBindingConstraints(metadata, bindings, false, &warnings);
    return CollectSelectedComponentIds(metadata, bindings, &warnings);
}

} // namespace MultiThreadedInstaller
