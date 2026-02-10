#include "packager/icon_updater.h"
#include "common/utf8_utils.h"
#include <Windows.h>
#include <fstream>
#include <vector>
#include <string>

namespace MultiThreadedInstaller {

#pragma pack(push, 1)
struct IconDirHeader {
    WORD reserved;
    WORD type;
    WORD count;
};

struct IconDirEntry {
    BYTE width;
    BYTE height;
    BYTE colorCount;
    BYTE reserved;
    WORD planes;
    WORD bitCount;
    DWORD bytesInRes;
    DWORD imageOffset;
};

struct GrpIconDirEntry {
    BYTE width;
    BYTE height;
    BYTE colorCount;
    BYTE reserved;
    WORD planes;
    WORD bitCount;
    DWORD bytesInRes;
    WORD id;
};
#pragma pack(pop)

static bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(PathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

bool UpdateInstallerIcon(const std::string& exePath, const std::string& iconPath, std::string& error) {
    std::vector<uint8_t> iconData;
    if (!ReadFileBytes(iconPath, iconData)) {
        error = "Failed to read icon file: " + iconPath;
        return false;
    }
    if (iconData.size() < sizeof(IconDirHeader)) {
        error = "Invalid icon file (too small): " + iconPath;
        return false;
    }

    const auto* header = reinterpret_cast<const IconDirHeader*>(iconData.data());
    if (header->type != 1 || header->count == 0) {
        error = "Invalid icon file header: " + iconPath;
        return false;
    }

    size_t entryOffset = sizeof(IconDirHeader);
    size_t entrySize = sizeof(IconDirEntry);
    size_t requiredSize = entryOffset + entrySize * header->count;
    if (iconData.size() < requiredSize) {
        error = "Invalid icon file (entries truncated): " + iconPath;
        return false;
    }

    std::wstring exePathW = Utf8ToWide(exePath);
    if (exePathW.empty()) {
        error = "Invalid installer path: " + exePath;
        return false;
    }
    HANDLE update = BeginUpdateResourceW(exePathW.c_str(), FALSE);
    if (!update) {
        error = "BeginUpdateResource failed: " + exePath;
        return false;
    }

    std::vector<GrpIconDirEntry> groupEntries;
    groupEntries.reserve(header->count);

    for (WORD i = 0; i < header->count; ++i) {
        const auto* entry = reinterpret_cast<const IconDirEntry*>(iconData.data() + entryOffset + entrySize * i);
        size_t imageStart = entry->imageOffset;
        size_t imageEnd = imageStart + entry->bytesInRes;
        if (imageEnd > iconData.size()) {
            EndUpdateResource(update, TRUE);
            error = "Invalid icon file (image out of range): " + iconPath;
            return false;
        }

        WORD iconId = static_cast<WORD>(i + 1);
        if (!UpdateResource(update,
                            RT_ICON,
                            MAKEINTRESOURCE(iconId),
                            MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                            reinterpret_cast<LPVOID>(iconData.data() + imageStart),
                            entry->bytesInRes)) {
            EndUpdateResource(update, TRUE);
            error = "UpdateResource RT_ICON failed";
            return false;
        }

        GrpIconDirEntry grp = {};
        grp.width = entry->width;
        grp.height = entry->height;
        grp.colorCount = entry->colorCount;
        grp.reserved = entry->reserved;
        grp.planes = entry->planes;
        grp.bitCount = entry->bitCount;
        grp.bytesInRes = entry->bytesInRes;
        grp.id = iconId;
        groupEntries.push_back(grp);
    }

    std::vector<uint8_t> groupData;
    groupData.resize(sizeof(IconDirHeader) + sizeof(GrpIconDirEntry) * groupEntries.size());
    auto* groupHeader = reinterpret_cast<IconDirHeader*>(groupData.data());
    groupHeader->reserved = 0;
    groupHeader->type = 1;
    groupHeader->count = static_cast<WORD>(groupEntries.size());
    memcpy(groupData.data() + sizeof(IconDirHeader),
           groupEntries.data(),
           sizeof(GrpIconDirEntry) * groupEntries.size());

    if (!UpdateResource(update,
                        RT_GROUP_ICON,
                        MAKEINTRESOURCE(1),
                        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                        groupData.data(),
                        static_cast<DWORD>(groupData.size()))) {
        EndUpdateResource(update, TRUE);
        error = "UpdateResource RT_GROUP_ICON failed";
        return false;
    }

    if (!EndUpdateResource(update, FALSE)) {
        error = "EndUpdateResource failed";
        return false;
    }

    return true;
}

} // namespace MultiThreadedInstaller
