#include "../../include/gui/license_text_loader.h"

#include "../../include/gui/gui_helpers.h"
#include "common/utf8_utils.h"

#include "Utils/unzip.h"

#include <UIlib.h>
#include <filesystem>
#include <fstream>
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

bool ReadZipTextFile(const CDuiString& zipPath,
                     const std::string& relativePath,
                     std::wstring& text) {
    HZIP hz = OpenZip(zipPath.GetData(), 0);
    if (hz == NULL) {
        return false;
    }

    bool found = false;
    ZIPENTRY ze;
    int index = 0;
    #ifdef UNICODE
    CDuiString zipItem(Utf8ToWide(relativePath).c_str());
    #else
    CDuiString zipItem(relativePath.c_str());
    #endif
    if (FindZipItem(hz, zipItem.GetData(), true, &index, &ze) == 0) {
        std::vector<char> buffer(static_cast<size_t>(ze.unc_size));
        if (UnzipItem(hz, index, buffer.data(), ze.unc_size) == 0) {
            std::string utf8(buffer.begin(), buffer.end());
            text = Utf8ToWide(utf8);
            found = true;
        }
    }

    CloseZip(hz);
    return found;
}

bool ReadDirectoryTextFile(const std::filesystem::path& basePath,
                           const std::string& relativePath,
                           std::wstring& text) {
    std::filesystem::path fullPath = basePath / PathFromUtf8(relativePath);
    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string utf8((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    text = Utf8ToWide(utf8);
    return true;
}

} // namespace

std::wstring LoadLocalizedLicenseText(const std::wstring& languageCode) {
    const std::vector<std::string> candidates = BuildLicenseCandidates(languageCode);

    if (CPaintManagerUI::GetResourceType() == UILIB_ZIP &&
        !CPaintManagerUI::GetResourceZip().IsEmpty()) {
        const CDuiString zipPath =
            CPaintManagerUI::GetResourcePath() + CPaintManagerUI::GetResourceZip();
        for (const auto& candidate : candidates) {
            std::wstring text;
            if (ReadZipTextFile(zipPath, candidate, text)) {
                return text;
            }
        }
    }

    const std::filesystem::path resourceBase = PathFromTChar(CPaintManagerUI::GetResourcePath().GetData());
    for (const auto& candidate : candidates) {
        std::wstring text;
        if (ReadDirectoryTextFile(resourceBase, candidate, text)) {
            return text;
        }
    }

    return GUIHelpers::GetLocalizedText(L"msg.license.text_missing", L"");
}

} // namespace MultiThreadedInstaller
