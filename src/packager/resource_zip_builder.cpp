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

std::string addScaleSuffix(const std::string& fileName, const std::string& suffix) {
    const size_t dot = fileName.rfind('.');
    if (dot == std::string::npos) {
        return fileName + suffix;
    }
    return fileName.substr(0, dot) + suffix + fileName.substr(dot);
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
        auto addImageEntry = [&](const std::string& name, const std::filesystem::path& path) {
            addEntry("images/" + name, path);
            addEntry("../images/" + name, path);
        };

        std::vector<std::pair<std::string, std::filesystem::path>> imageFiles;
        // 递归遍历，保留相对子目录（如 carousel/zh_CN/1.png），以支持按语言分目录的图片
        for (const auto& entry : std::filesystem::recursive_directory_iterator(imagesDir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string relName =
                Utf8FromPath(std::filesystem::relative(entry.path(), imagesDir));
            std::replace(relName.begin(), relName.end(), '\\', '/');  // zip 内统一用正斜杠
            imageFiles.emplace_back(relName, entry.path());
        }

        for (const auto& item : imageFiles) {
            addImageEntry(item.first, item.second);
        }
        for (const auto& item : imageFiles) {
            std::string alias;
            if (replaceScaleSuffix(item.first, "@1.5x", "@150", alias)) {
                addImageEntry(alias, item.second);
            } else if (replaceScaleSuffix(item.first, "@2x", "@200", alias)) {
                addImageEntry(alias, item.second);
            } else if (replaceScaleSuffix(item.first, "@3x", "@300", alias)) {
                addImageEntry(alias, item.second);
            }
        }
        for (const auto& item : imageFiles) {
            if (item.first.find('@') != std::string::npos) {
                continue;
            }
            addImageEntry(addScaleSuffix(item.first, "@125"), item.second);
            addImageEntry(addScaleSuffix(item.first, "@150"), item.second);
            addImageEntry(addScaleSuffix(item.first, "@200"), item.second);
            addImageEntry(addScaleSuffix(item.first, "@300"), item.second);
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
