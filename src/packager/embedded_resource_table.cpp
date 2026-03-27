#include "packager/embedded_resource_table.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

namespace {

bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    return file.read(reinterpret_cast<char*>(out.data()), size).good();
}

void AppendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

} // namespace

bool HasEmbeddedResourceTable(const std::vector<uint8_t>& installerTemplate) {
#ifdef _WIN32
    if (installerTemplate.size() < sizeof(IMAGE_DOS_HEADER) + sizeof(uint32_t)) {
        return false;
    }

    auto readAt = [&](uint64_t offset, void* out, size_t bytes) -> bool {
        if (offset + bytes > installerTemplate.size()) {
            return false;
        }
        std::memcpy(out, installerTemplate.data() + offset, bytes);
        return true;
    };

    IMAGE_DOS_HEADER dosHeader{};
    if (!readAt(0, &dosHeader, sizeof(dosHeader)) || dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    const uint64_t ntOffset = static_cast<uint64_t>(dosHeader.e_lfanew);
    uint32_t peSignature = 0;
    if (!readAt(ntOffset, &peSignature, sizeof(peSignature)) || peSignature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    IMAGE_FILE_HEADER fileHeader{};
    if (!readAt(ntOffset + sizeof(uint32_t), &fileHeader, sizeof(fileHeader))) {
        return false;
    }

    const uint64_t sectionOffset = ntOffset + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                                   static_cast<uint64_t>(fileHeader.SizeOfOptionalHeader);
    uint64_t peEnd = sectionOffset;
    for (uint16_t i = 0; i < fileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!readAt(sectionOffset + static_cast<uint64_t>(i) * sizeof(IMAGE_SECTION_HEADER),
                    &section,
                    sizeof(section))) {
            return false;
        }
        const uint64_t sectionEnd = static_cast<uint64_t>(section.PointerToRawData) +
                                    static_cast<uint64_t>(section.SizeOfRawData);
        if (sectionEnd > peEnd) {
            peEnd = sectionEnd;
        }
    }

    if (peEnd >= installerTemplate.size()) {
        return false;
    }

    const uint32_t magic = 0x52534D45;
    auto parseTable = [&](uint64_t magicOffset) -> bool {
        if (magicOffset <= peEnd || magicOffset > installerTemplate.size()) {
            return false;
        }

        uint64_t offset = peEnd;
        while (offset < magicOffset) {
            if (offset + sizeof(uint32_t) + sizeof(uint64_t) > magicOffset) {
                return false;
            }

            uint32_t nameLen = 0;
            if (!readAt(offset, &nameLen, sizeof(nameLen))) {
                return false;
            }
            offset += sizeof(nameLen);
            if (nameLen == 0 || offset + nameLen + sizeof(uint64_t) > magicOffset) {
                return false;
            }

            offset += nameLen;

            uint64_t dataLen = 0;
            if (!readAt(offset, &dataLen, sizeof(dataLen))) {
                return false;
            }
            offset += sizeof(dataLen);
            if (dataLen == 0 || offset + dataLen > magicOffset) {
                return false;
            }
            offset += dataLen;
        }

        return offset == magicOffset;
    };

    for (uint64_t i = installerTemplate.size() - sizeof(uint32_t);
         i + sizeof(uint32_t) <= installerTemplate.size();
         --i) {
        uint32_t candidate = 0;
        if (!readAt(i, &candidate, sizeof(candidate))) {
            return false;
        }
        if (candidate == magic && parseTable(i)) {
            return true;
        }
        if (i == 0) {
            break;
        }
    }
#endif
    return false;
}

bool AppendEmbeddedFileEntry(std::vector<uint8_t>& installerTemplate,
                             const std::string& name,
                             const std::filesystem::path& filePath) {
    std::vector<uint8_t> data;
    if (!ReadFileBytes(filePath, data) || data.empty()) {
        return false;
    }
    return AppendEmbeddedRawEntry(installerTemplate, name, data);
}

bool AppendEmbeddedRawEntry(std::vector<uint8_t>& installerTemplate,
                            const std::string& name,
                            const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return false;
    }

    const uint32_t nameLen = static_cast<uint32_t>(name.size());
    const uint64_t dataLen = static_cast<uint64_t>(data.size());
    AppendBytes(installerTemplate, &nameLen, sizeof(nameLen));
    AppendBytes(installerTemplate, name.data(), name.size());
    AppendBytes(installerTemplate, &dataLen, sizeof(dataLen));
    AppendBytes(installerTemplate, data.data(), data.size());
    return true;
}

void AppendEmbeddedResourceMagic(std::vector<uint8_t>& installerTemplate) {
    const uint32_t magic = 0x52534D45;
    AppendBytes(installerTemplate, &magic, sizeof(magic));
}

} // namespace MultiThreadedInstaller
