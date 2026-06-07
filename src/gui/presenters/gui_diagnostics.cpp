#include "gui/presenters/gui_diagnostics.h"

#include "common/installer_logger.h"
#include "common/utf8_utils.h"

#include <algorithm>
#include <sstream>
#include <vector>

using namespace DuiLib;

namespace MultiThreadedInstaller {

namespace {

static constexpr int kPageWelcome = 0;
static constexpr int kPageLicense = 1;
static constexpr int kPageProgress = 2;
static constexpr int kPageCompletion = 3;

const char* GetInstallPageSkinByIndex(int index) {
    switch (index) {
        case kPageWelcome:
            return "skins/welcome_page.xml";
        case kPageLicense:
            return "skins/license_page.xml";
        case kPageProgress:
            return "skins/progress_page.xml";
        case kPageCompletion:
            return "skins/completion_page.xml";
        default:
            return nullptr;
    }
}

const char* GetUninstallPageSkinByIndex(int index) {
    switch (index) {
        case 0:
            return "skins/uninstall_confirm_page.xml";
        case 1:
            return "skins/uninstall_progress_page.xml";
        case 2:
            return "skins/uninstall_completion_page.xml";
        default:
            return nullptr;
    }
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

std::string JoinSampleListLocal(const std::vector<std::string>& values, size_t limit) {
    if (values.empty()) {
        return "(none)";
    }
    std::ostringstream oss;
    const size_t count = (std::min)(values.size(), limit);
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            oss << " | ";
        }
        oss << values[i];
    }
    if (values.size() > limit) {
        oss << " | ... +" << (values.size() - limit) << " more";
    }
    return oss.str();
}

void AppendImageProperty(std::vector<std::string>& images,
                         const std::string& key,
                         LPCTSTR value) {
    if (!value || value[0] == _T('\0')) {
        return;
    }
    images.push_back(key + "=" + WideToUtf8(TCharToWide(value)));
}

} // namespace

std::vector<std::string> BuildCurrentGuiXmlScope(const CTabLayoutUI* tabPages,
                                                 bool uninstallMode) {
    std::vector<std::string> xmlEntries;
    xmlEntries.push_back(uninstallMode ? "skins/uninstall_main.xml" : "skins/main.xml");

    if (!tabPages) {
        return xmlEntries;
    }

    const int currentIndex = tabPages->GetCurSel();
    const char* currentPage = uninstallMode ? GetUninstallPageSkinByIndex(currentIndex)
                                            : GetInstallPageSkinByIndex(currentIndex);
    if (currentPage) {
        xmlEntries.push_back(currentPage);
    }

    std::sort(xmlEntries.begin(), xmlEntries.end());
    xmlEntries.erase(std::unique(xmlEntries.begin(), xmlEntries.end()), xmlEntries.end());
    return xmlEntries;
}

void LogCurrentPageControlImageSnapshot(CTabLayoutUI* tabPages,
                                        bool uninstallMode,
                                        const char* stage) {
    if (!tabPages) {
        logInstallerWarning(std::string("[GUI][CTRLIMG] stage=") + (stage ? stage : "unknown") +
                            " pages=null");
        return;
    }

    const int currentIndex = tabPages->GetCurSel();
    CControlUI* pageRoot = tabPages->GetItemAt(currentIndex);
    if (!pageRoot) {
        logInstallerWarning(std::string("[GUI][CTRLIMG] stage=") + (stage ? stage : "unknown") +
                            " page_root=null index=" + std::to_string(currentIndex));
        return;
    }

    std::vector<CControlUI*> controls;
    CollectControlsRecursive(pageRoot, controls);

    size_t controlsWithImages = 0;
    std::vector<std::string> samples;
    for (CControlUI* control : controls) {
        if (!control) {
            continue;
        }

        std::vector<std::string> images;
        AppendImageProperty(images, "bk", control->GetBkImage());
        AppendImageProperty(images, "fore", control->GetForeImage());

        if (auto* button = static_cast<CButtonUI*>(control->GetInterface(_T("Button")))) {
            AppendImageProperty(images, "normal", button->GetNormalImage());
            AppendImageProperty(images, "hot", button->GetHotImage());
            AppendImageProperty(images, "pushed", button->GetPushedImage());
            AppendImageProperty(images, "disabled", button->GetDisabledImage());
        }
        if (auto* option = static_cast<COptionUI*>(control->GetInterface(_T("Option")))) {
            AppendImageProperty(images, "selected", option->GetSelectedImage());
        }
        if (auto* combo = static_cast<CComboUI*>(control->GetInterface(_T("Combo")))) {
            AppendImageProperty(images, "combo_normal", combo->GetNormalImage());
            AppendImageProperty(images, "combo_hot", combo->GetHotImage());
            AppendImageProperty(images, "combo_pushed", combo->GetPushedImage());
            AppendImageProperty(images, "combo_disabled", combo->GetDisabledImage());
        }
        if (auto* edit = static_cast<CEditUI*>(control->GetInterface(_T("Edit")))) {
            AppendImageProperty(images, "edit_normal", edit->GetNormalImage());
            AppendImageProperty(images, "edit_hot", edit->GetHotImage());
            AppendImageProperty(images, "edit_disabled", edit->GetDisabledImage());
        }
        if (auto* rich = static_cast<CRichEditUI*>(control->GetInterface(_T("RichEdit")))) {
            AppendImageProperty(images, "rich_normal", rich->GetNormalImage());
            AppendImageProperty(images, "rich_hot", rich->GetHotImage());
            AppendImageProperty(images, "rich_disabled", rich->GetDisabledImage());
        }

        if (images.empty()) {
            continue;
        }

        ++controlsWithImages;
        std::ostringstream sample;
        sample << "name=" << WideToUtf8(TCharToWide(control->GetName().GetData()))
               << " class=" << WideToUtf8(TCharToWide(control->GetClass()))
               << " images=";
        for (size_t i = 0; i < images.size(); ++i) {
            if (i > 0) {
                sample << " | ";
            }
            sample << images[i];
        }
        samples.push_back(sample.str());
    }

    const std::vector<std::string> xmlScope = BuildCurrentGuiXmlScope(tabPages, uninstallMode);
    logInstallerInfo(std::string("[GUI][CTRLIMG] stage=") + (stage ? stage : "unknown") +
                     " uninstall=" + (uninstallMode ? "true" : "false") +
                     " page_index=" + std::to_string(currentIndex) +
                     " controls=" + std::to_string(controls.size()) +
                     " controls_with_images=" + std::to_string(controlsWithImages) +
                     " xml_scope=" + JoinSampleListLocal(xmlScope, 4));
    if (!samples.empty()) {
        logInstallerDebug(std::string("[GUI][CTRLIMG] sample: ") + JoinSampleListLocal(samples, 10));
    }
}

} // namespace MultiThreadedInstaller
