#include "packager/version_info_updater.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

namespace MultiThreadedInstaller {

static std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), size);
    return out;
}

static void AppendWord(std::vector<uint8_t>& out, WORD value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

static void AppendDword(std::vector<uint8_t>& out, DWORD value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

static void AppendWString(std::vector<uint8_t>& out, const std::wstring& value) {
    for (wchar_t ch : value) {
        AppendWord(out, static_cast<WORD>(ch));
    }
    AppendWord(out, 0);
}

static void AlignDword(std::vector<uint8_t>& out) {
    size_t pad = (4 - (out.size() % 4)) % 4;
    for (size_t i = 0; i < pad; ++i) {
        out.push_back(0);
    }
}

static size_t BeginBlock(std::vector<uint8_t>& out, const std::wstring& key, WORD valueLength, WORD type) {
    size_t start = out.size();
    AppendWord(out, 0);
    AppendWord(out, valueLength);
    AppendWord(out, type);
    AppendWString(out, key);
    AlignDword(out);
    return start;
}

static void EndBlock(std::vector<uint8_t>& out, size_t start) {
    WORD length = static_cast<WORD>(out.size() - start);
    out[start] = static_cast<uint8_t>(length & 0xFF);
    out[start + 1] = static_cast<uint8_t>((length >> 8) & 0xFF);
}

static std::vector<uint8_t> BuildStringBlock(const std::wstring& key, const std::wstring& value) {
    std::vector<uint8_t> out;
    WORD valueLen = static_cast<WORD>(value.size() + 1);
    size_t start = BeginBlock(out, key, valueLen, 1);
    AppendWString(out, value);
    AlignDword(out);
    EndBlock(out, start);
    return out;
}

static std::vector<uint8_t> BuildStringTable(const std::vector<std::pair<std::wstring, std::wstring>>& entries) {
    std::vector<uint8_t> out;
    size_t start = BeginBlock(out, L"040904B0", 0, 1);
    for (const auto& entry : entries) {
        auto block = BuildStringBlock(entry.first, entry.second);
        out.insert(out.end(), block.begin(), block.end());
    }
    AlignDword(out);
    EndBlock(out, start);
    return out;
}

static std::vector<uint8_t> BuildStringFileInfo(const std::vector<std::pair<std::wstring, std::wstring>>& entries) {
    std::vector<uint8_t> out;
    size_t start = BeginBlock(out, L"StringFileInfo", 0, 1);
    auto table = BuildStringTable(entries);
    out.insert(out.end(), table.begin(), table.end());
    AlignDword(out);
    EndBlock(out, start);
    return out;
}

static std::vector<uint8_t> BuildVarFileInfo() {
    std::vector<uint8_t> out;
    size_t start = BeginBlock(out, L"VarFileInfo", 0, 1);

    std::vector<uint8_t> value;
    AppendWord(value, 0x0409);
    AppendWord(value, 1200);

    size_t varStart = BeginBlock(out, L"Translation", 4, 0);
    out.insert(out.end(), value.begin(), value.end());
    AlignDword(out);
    EndBlock(out, varStart);

    AlignDword(out);
    EndBlock(out, start);
    return out;
}

static bool ParseVersion(const std::string& text, DWORD& ms, DWORD& ls) {
    std::vector<int> parts;
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, '.')) {
        if (token.empty()) {
            parts.push_back(0);
            continue;
        }
        parts.push_back(std::max(0, std::min(65535, std::stoi(token))));
    }
    while (parts.size() < 4) {
        parts.push_back(0);
    }
    ms = (static_cast<DWORD>(parts[0]) << 16) | static_cast<DWORD>(parts[1]);
    ls = (static_cast<DWORD>(parts[2]) << 16) | static_cast<DWORD>(parts[3]);
    return true;
}

bool UpdateInstallerVersionInfo(const std::string& exePath, const VersionInfoData& info, std::string& error) {
    std::vector<std::pair<std::wstring, std::wstring>> entries;
    if (!info.companyName.empty()) entries.emplace_back(L"CompanyName", Utf8ToWide(info.companyName));
    if (!info.fileDescription.empty()) entries.emplace_back(L"FileDescription", Utf8ToWide(info.fileDescription));
    if (!info.fileVersion.empty()) entries.emplace_back(L"FileVersion", Utf8ToWide(info.fileVersion));
    if (!info.productName.empty()) entries.emplace_back(L"ProductName", Utf8ToWide(info.productName));
    if (!info.productVersion.empty()) entries.emplace_back(L"ProductVersion", Utf8ToWide(info.productVersion));
    if (!info.copyright.empty()) entries.emplace_back(L"LegalCopyright", Utf8ToWide(info.copyright));
    if (!info.originalFilename.empty()) entries.emplace_back(L"OriginalFilename", Utf8ToWide(info.originalFilename));

    if (entries.empty()) {
        return true;
    }

    DWORD fileMs = 0, fileLs = 0;
    DWORD productMs = 0, productLs = 0;
    ParseVersion(info.fileVersion, fileMs, fileLs);
    ParseVersion(info.productVersion, productMs, productLs);

    VS_FIXEDFILEINFO fixed = {};
    fixed.dwSignature = 0xFEEF04BD;
    fixed.dwStrucVersion = 0x00010000;
    fixed.dwFileVersionMS = fileMs;
    fixed.dwFileVersionLS = fileLs;
    fixed.dwProductVersionMS = productMs;
    fixed.dwProductVersionLS = productLs;
    fixed.dwFileFlagsMask = 0x3F;
    fixed.dwFileFlags = 0;
    fixed.dwFileOS = 0x00040004;
    fixed.dwFileType = 0x00000001;
    fixed.dwFileSubtype = 0;
    fixed.dwFileDateMS = 0;
    fixed.dwFileDateLS = 0;

    std::vector<uint8_t> data;
    size_t start = BeginBlock(data, L"VS_VERSION_INFO", sizeof(VS_FIXEDFILEINFO), 0);
    data.insert(data.end(),
                reinterpret_cast<uint8_t*>(&fixed),
                reinterpret_cast<uint8_t*>(&fixed) + sizeof(VS_FIXEDFILEINFO));
    AlignDword(data);
    auto stringInfo = BuildStringFileInfo(entries);
    data.insert(data.end(), stringInfo.begin(), stringInfo.end());
    auto varInfo = BuildVarFileInfo();
    data.insert(data.end(), varInfo.begin(), varInfo.end());
    AlignDword(data);
    EndBlock(data, start);

    HANDLE update = BeginUpdateResourceA(exePath.c_str(), FALSE);
    if (!update) {
        error = "BeginUpdateResource failed";
        return false;
    }
    if (!UpdateResource(update,
                        RT_VERSION,
                        MAKEINTRESOURCE(1),
                        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                        data.data(),
                        static_cast<DWORD>(data.size()))) {
        EndUpdateResource(update, TRUE);
        error = "UpdateResource RT_VERSION failed";
        return false;
    }
    if (!EndUpdateResource(update, FALSE)) {
        error = "EndUpdateResource failed";
        return false;
    }
    return true;
}

} // namespace MultiThreadedInstaller
