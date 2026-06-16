#include "packager/resource_zip_builder.h"

#include "common/utf8_utils.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

struct ZipFileEntry {
    std::string name;
    std::filesystem::path path;
};

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void appendUint16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void appendUint32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size)) {
        return false;
    }
    return true;
}

bool replaceScaleSuffix(const std::string& fileName,
                        const std::string& from,
                        const std::string& to,
                        std::string& out) {
    const size_t pos = fileName.find(from);
    if (pos == std::string::npos) {
        return false;
    }
    out = fileName;
    out.replace(pos, from.size(), to);
    return true;
}

void collectResourceFiles(const std::filesystem::path& resourceDir,
                          std::vector<ZipFileEntry>& outFiles) {
    std::unordered_set<std::string> seen;
    auto addEntry = [&](const std::string& name, const std::filesystem::path& path) {
        if (seen.insert(name).second) {
            outFiles.push_back({name, path});
        }
    };

    auto addDir = [&](const std::filesystem::path& dir,
                      const std::string& prefix,
                      const std::vector<std::string>& extraPrefixes) {
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            return;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string fileName = Utf8FromPath(entry.path().filename());
            addEntry(prefix + fileName, entry.path());
            for (const auto& extra : extraPrefixes) {
                addEntry(extra + fileName, entry.path());
            }
        }
    };

    addDir(resourceDir / "skins", "skins/", {""});

    const std::filesystem::path imagesDir = resourceDir / "images";
    if (std::filesystem::exists(imagesDir) && std::filesystem::is_directory(imagesDir)) {
        // 每个真实图片文件在 zip 中只存一份，避免体积爆炸。命名规则与 DuiLib 取图一致：
        //   · 旧式 @1.5x/@2x/@3x 高清图 → 改名为 DuiLib 实际请求的 @150/@200/@300（百分比）；
        //   · 其余（基图、或已是 @NNN）按原相对名存。
        // 高 DPI 下 DuiLib 请求 images/x@<scale>.png，命中真高清图即用之；若该尺寸不存在，
        // DuiLib(AddImage) 会自动去掉 @后缀回退加载基图 images/x.png，故无需再合成 @NNN 副本。
        // XML 引用统一为 images/...（已确认无 ../images/），故不再生成 ../images/ 别名。
        for (const auto& entry : std::filesystem::recursive_directory_iterator(imagesDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string relName =
                Utf8FromPath(std::filesystem::relative(entry.path(), imagesDir));
            std::replace(relName.begin(), relName.end(), '\\', '/');  // zip 内统一用正斜杠

            std::string storeName;
            std::string alias;
            if (replaceScaleSuffix(relName, "@1.5x", "@150", alias)) {
                storeName = alias;
            } else if (replaceScaleSuffix(relName, "@2x", "@200", alias)) {
                storeName = alias;
            } else if (replaceScaleSuffix(relName, "@3x", "@300", alias)) {
                storeName = alias;
            } else {
                storeName = relName;
            }
            addEntry("images/" + storeName, entry.path());
        }
    }

    addDir(resourceDir / "lang", "lang/", {"../lang/"});
    addDir(resourceDir / "license", "license/", {"../license/"});

    const std::filesystem::path licensePath = resourceDir / "license.txt";
    if (std::filesystem::exists(licensePath) && std::filesystem::is_regular_file(licensePath)) {
        addEntry("license.txt", licensePath);
        addEntry("../license.txt", licensePath);
    }

    std::sort(outFiles.begin(), outFiles.end(),
              [](const ZipFileEntry& a, const ZipFileEntry& b) { return a.name < b.name; });
}

} // namespace

bool BuildResourceZip(const std::filesystem::path& resourceDir,
                      std::vector<uint8_t>& outZip,
                      std::string& error) {
    error.clear();
    std::vector<ZipFileEntry> files;
    collectResourceFiles(resourceDir, files);
    if (files.empty()) {
        error = "No resource files found for zip packaging in: " + Utf8FromPath(resourceDir);
        return false;
    }

    struct CentralEntry {
        std::string name;
        uint32_t crc;
        uint32_t size;
        uint32_t offset;
    };

    std::vector<CentralEntry> central;
    outZip.clear();

    for (const auto& entry : files) {
        std::vector<uint8_t> data;
        if (!readFileBytes(entry.path, data)) {
            error = "Failed to read resource file for zip packaging: " + Utf8FromPath(entry.path);
            outZip.clear();
            return false;
        }

        const uint32_t crc = crc32(data.data(), data.size());
        const uint32_t size = static_cast<uint32_t>(data.size());
        const uint32_t offset = static_cast<uint32_t>(outZip.size());

        appendUint32(outZip, 0x04034b50);
        appendUint16(outZip, 20);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint32(outZip, crc);
        appendUint32(outZip, size);
        appendUint32(outZip, size);
        appendUint16(outZip, static_cast<uint16_t>(entry.name.size()));
        appendUint16(outZip, 0);
        outZip.insert(outZip.end(), entry.name.begin(), entry.name.end());
        outZip.insert(outZip.end(), data.begin(), data.end());

        central.push_back({entry.name, crc, size, offset});
    }

    const uint32_t centralOffset = static_cast<uint32_t>(outZip.size());
    for (const auto& entry : central) {
        appendUint32(outZip, 0x02014b50);
        appendUint16(outZip, 20);
        appendUint16(outZip, 20);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint32(outZip, entry.crc);
        appendUint32(outZip, entry.size);
        appendUint32(outZip, entry.size);
        appendUint16(outZip, static_cast<uint16_t>(entry.name.size()));
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint16(outZip, 0);
        appendUint32(outZip, 0);
        appendUint32(outZip, entry.offset);
        outZip.insert(outZip.end(), entry.name.begin(), entry.name.end());
    }

    const uint32_t centralSize = static_cast<uint32_t>(outZip.size() - centralOffset);
    appendUint32(outZip, 0x06054b50);
    appendUint16(outZip, 0);
    appendUint16(outZip, 0);
    appendUint16(outZip, static_cast<uint16_t>(central.size()));
    appendUint16(outZip, static_cast<uint16_t>(central.size()));
    appendUint32(outZip, centralSize);
    appendUint32(outZip, centralOffset);
    appendUint16(outZip, 0);
    return true;
}

} // namespace MultiThreadedInstaller
