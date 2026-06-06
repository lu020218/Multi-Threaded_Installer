#include "../../include/gui/license_text_loader.h"

#include "../../include/gui/gui_helpers.h"
#include "installer/gui_resource_loader.h"
#include "common/utf8_utils.h"

#include <UIlib.h>
#include <string>
#include <unordered_set>
#include <vector>

using namespace DuiLib;

namespace MultiThreadedInstaller {
namespace {

std::vector<std::string> BuildLicenseCandidates(const std::wstring& languageCode) {
    std::vector<std::string> candidates;
    std::unordered_set<std::string> seen;
    auto addCandidate = [&](const std::string& value) {
        if (!value.empty() && seen.insert(value).second) {
            candidates.push_back(value);
        }
    };

    const std::string requested = WideToUtf8(languageCode);
    if (!requested.empty()) {
        addCandidate("license/" + requested + ".txt");
        addCandidate("../license/" + requested + ".txt");
    }

    addCandidate("license/en_US.txt");
    addCandidate("../license/en_US.txt");
    addCandidate("license.txt");
    addCandidate("../license.txt");
    return candidates;
}

} // namespace

std::wstring LoadLocalizedLicenseText(const std::wstring& languageCode) {
    const std::vector<std::string> candidates = BuildLicenseCandidates(languageCode);

    // 许可证文本从常驻内存的资源 zip 句柄读取。
    for (const auto& candidate : candidates) {
        const std::string utf8 = ReadActiveResourceZipEntry(candidate);
        if (!utf8.empty()) {
            return Utf8ToWide(utf8);
        }
    }

    return GUIHelpers::GetLocalizedText(L"msg.license.text_missing", L"");
}

} // namespace MultiThreadedInstaller
