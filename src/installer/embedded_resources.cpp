#include "../../include/installer/embedded_resources.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

namespace MultiThreadedInstaller {

EmbeddedResourceManager::EmbeddedResourceManager()
    : m_extracted(false) {
}

EmbeddedResourceManager::~EmbeddedResourceManager() {
    cleanup();
}

std::string EmbeddedResourceManager::extractResources() {
    if (m_extracted && !m_resourcePath.empty()) {
        return m_resourcePath;
    }
    

    m_resourcePath = createTempDirectory();
    if (m_resourcePath.empty()) {
        logInstallerError("[GUI][RES] Failed to create temporary directory for resources.");
        return "";
    }
    
    logInstallerInfo(std::string("[GUI][RES] Extracting embedded resources to: ") + m_resourcePath);

    bool anyExtracted = false;
    

    auto duilib = getEmbeddedResource("DUILIB_DLL");
    if (!duilib.empty()) {
        if (extractFile("DuiLib.dll", duilib)) {
            logInstallerInfo("[GUI][RES] Extracted: DuiLib.dll");
            anyExtracted = true;
        }
    }

    auto packedZip = getEmbeddedResource("RES_ZIP");
    if (!packedZip.empty()) {
        if (extractFile("resources.zip", packedZip)) {
            logInstallerInfo("[GUI][RES] Extracted: resources.zip");
            anyExtracted = true;
            m_extracted = true;
            return m_resourcePath;
        }
    }
    

    const char* xmlFiles[] = {
        "main.xml",
        "uninstall_main.xml",
        "welcome_page.xml",
        "progress_page.xml",
        "completion_page.xml",
        "uninstall_confirm_page.xml",
        "uninstall_progress_page.xml",
        "uninstall_completion_page.xml",
        "license.xml"
    };
    
    for (const char* xmlFile : xmlFiles) {
        std::string resourceName = "XML_";
        resourceName += xmlFile;

        for (char& c : resourceName) {
            if (c == '.') c = '_';
            c = toupper(c);
        }
        
        auto data = getEmbeddedResource(resourceName);
        if (!data.empty()) {
            std::string path = "skins\\";
            path += xmlFile;
            if (extractFile(path, data)) {
                logInstallerInfo(std::string("[GUI][RES] Extracted: ") + path);
                anyExtracted = true;
            }
        }
    }
    

    auto imageList = getEmbeddedResource("IMAGES_LIST");
    if (!imageList.empty()) {
        std::string listText(reinterpret_cast<const char*>(imageList.data()), imageList.size());
        size_t start = 0;
        while (start < listText.size()) {
            size_t end = listText.find_first_of("\r\n", start);
            if (end == std::string::npos) {
                end = listText.size();
            }
            std::string fileName = listText.substr(start, end - start);
            if (!fileName.empty()) {
                std::string resourceName = "IMG_";
                resourceName += fileName;
                for (char& c : resourceName) {
                    if (c == '.') c = '_';
                    c = toupper(c);
                }
                auto data = getEmbeddedResource(resourceName);
                if (!data.empty()) {
                    std::string path = "images\\";
                    path += fileName;
                    if (extractFile(path, data)) {
                        logInstallerInfo(std::string("[GUI][RES] Extracted: ") + path);
                        anyExtracted = true;
                    }
                }
            }
            start = listText.find_first_not_of("\r\n", end);
            if (start == std::string::npos) {
                break;
            }
        }
    }

    auto langList = getEmbeddedResource("LANG_LIST");
    if (!langList.empty()) {
        std::string listText(reinterpret_cast<const char*>(langList.data()), langList.size());
        size_t start = 0;
        while (start < listText.size()) {
            size_t end = listText.find_first_of("\r\n", start);
            if (end == std::string::npos) {
                end = listText.size();
            }
            std::string fileName = listText.substr(start, end - start);
            if (!fileName.empty()) {
                std::string resourceName = "LANG_";
                resourceName += fileName;
                for (char& c : resourceName) {
                    if (c == '.') c = '_';
                    c = toupper(c);
                }
                auto data = getEmbeddedResource(resourceName);
                if (!data.empty()) {
                    std::string path = "lang\\";
                    path += fileName;
                    if (extractFile(path, data)) {
                        logInstallerInfo(std::string("[GUI][RES] Extracted: ") + path);
                        anyExtracted = true;
                    }
                }
            }
            start = listText.find_first_not_of("\r\n", end);
            if (start == std::string::npos) {
                break;
            }
        }
    }

    auto licenseList = getEmbeddedResource("LICENSE_LIST");
    if (!licenseList.empty()) {
        std::string listText(reinterpret_cast<const char*>(licenseList.data()), licenseList.size());
        size_t start = 0;
        while (start < listText.size()) {
            size_t end = listText.find_first_of("\r\n", start);
            if (end == std::string::npos) {
                end = listText.size();
            }
            std::string fileName = listText.substr(start, end - start);
            if (!fileName.empty()) {
                std::string resourceName = "LICENSE_";
                resourceName += fileName;
                for (char& c : resourceName) {
                    if (c == '.') c = '_';
                    c = toupper(c);
                }
                auto data = getEmbeddedResource(resourceName);
                if (!data.empty()) {
                    std::string path = "license\\";
                    path += fileName;
                    if (extractFile(path, data)) {
                        logInstallerInfo(std::string("[GUI][RES] Extracted: ") + path);
                        anyExtracted = true;
                    }
                }
            }
            start = listText.find_first_not_of("\r\n", end);
            if (start == std::string::npos) {
                break;
            }
        }
    }

    auto license = getEmbeddedResource("LICENSE_TXT");
    if (!license.empty()) {
        if (extractFile("license.txt", license)) {
            logInstallerInfo("[GUI][RES] Extracted: license.txt");
            anyExtracted = true;
        }
    }
    

    if (!anyExtracted) {
        logInstallerWarning("[GUI][RES] No embedded resources found.");
        cleanup();
        return "";
    }
    
    m_extracted = true;
    return m_resourcePath;
}

void EmbeddedResourceManager::cleanup() {
    if (!m_resourcePath.empty() && m_extracted) {
        try {
            std::filesystem::remove_all(PathFromUtf8(m_resourcePath));
            logInstallerInfo(std::string("[GUI][RES] Cleaned up temporary resources: ") + m_resourcePath);
        } catch (const std::exception& e) {
            logInstallerWarning(std::string("[GUI][RES] Failed to cleanup resources: ") + e.what());
        }
        m_resourcePath.clear();
        m_extracted = false;
    }
}

std::string EmbeddedResourceManager::createTempDirectory() {
    wchar_t tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath) == 0) {
        return "";
    }
    

    std::wstring tempDir = tempPath;
    tempDir += L"MTInstaller_";
    tempDir += std::to_wstring(GetCurrentProcessId());
    tempDir += L"_";
    tempDir += std::to_wstring(GetTickCount64());
    
    std::filesystem::path tempDirPath(tempDir);

    if (!std::filesystem::create_directories(tempDirPath)) {
        return "";
    }
    
    return Utf8FromPath(tempDirPath);
}

bool EmbeddedResourceManager::extractFile(const std::string& relativePath, 
                                         const std::vector<uint8_t>& data) {
    if (m_resourcePath.empty() || data.empty()) {
        return false;
    }
    
    std::filesystem::path fullPath = PathFromUtf8(m_resourcePath) / PathFromUtf8(relativePath);
    
    try {
        std::filesystem::path parent = fullPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        std::ofstream file(fullPath, std::ios::binary);
        if (!file) {
            logInstallerError(std::string("[GUI][RES] Failed to create file: ") +
                              Utf8FromPath(fullPath));
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        logInstallerError(std::string("[GUI][RES] Error extracting file ") + relativePath +
                          ": " + e.what());
        return false;
    }
}

std::vector<uint8_t> EmbeddedResourceManager::getEmbeddedResource(const std::string& name) {

    HMODULE hModule = GetModuleHandle(NULL);
    HRSRC hResource = FindResourceA(hModule, name.c_str(), "BINARY");
    
    if (hResource != NULL) {
        HGLOBAL hLoadedResource = LoadResource(hModule, hResource);
        if (hLoadedResource != NULL) {
            LPVOID pLockedResource = LockResource(hLoadedResource);
            if (pLockedResource != NULL) {
                DWORD dwResourceSize = SizeofResource(hModule, hResource);
                if (dwResourceSize > 0) {
                    std::vector<uint8_t> data(dwResourceSize);
                    memcpy(data.data(), pLockedResource, dwResourceSize);
                    return data;
                }
            }
        }
    }
    


    return readEmbeddedResourceFromFile(name);
}

std::vector<uint8_t> EmbeddedResourceManager::readEmbeddedResourceFromFile(const std::string& name) {
    // Resolve current executable path.
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
        return {};
    }

    std::filesystem::path exePathFs(exePath);
    std::ifstream file(exePathFs, std::ios::binary);
    if (!file) {
        return {};
    }

    file.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    if (fileSize < sizeof(IMAGE_DOS_HEADER) + sizeof(uint32_t)) {
        return {};
    }

    auto readAt = [&](uint64_t offset, void* out, size_t bytes) -> bool {
        if (offset + bytes > fileSize) {
            return false;
        }
        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(bytes));
        return file.good();
    };

    IMAGE_DOS_HEADER dosHeader{};
    if (!readAt(0, &dosHeader, sizeof(dosHeader))) {
        return {};
    }
    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        return {};
    }

    uint64_t ntOffset = static_cast<uint64_t>(dosHeader.e_lfanew);
    if (ntOffset + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) > fileSize) {
        return {};
    }

    uint32_t peSignature = 0;
    if (!readAt(ntOffset, &peSignature, sizeof(peSignature))) {
        return {};
    }
    if (peSignature != IMAGE_NT_SIGNATURE) {
        return {};
    }

    IMAGE_FILE_HEADER fileHeader{};
    if (!readAt(ntOffset + sizeof(uint32_t), &fileHeader, sizeof(fileHeader))) {
        return {};
    }

    uint64_t sectionOffset = ntOffset + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                             static_cast<uint64_t>(fileHeader.SizeOfOptionalHeader);
    uint64_t sectionTableSize = static_cast<uint64_t>(fileHeader.NumberOfSections) *
                                sizeof(IMAGE_SECTION_HEADER);
    if (sectionOffset + sectionTableSize > fileSize) {
        return {};
    }

    uint64_t peEnd = sectionOffset;
    for (uint16_t i = 0; i < fileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!readAt(sectionOffset + static_cast<uint64_t>(i) * sizeof(IMAGE_SECTION_HEADER),
                    &section, sizeof(section))) {
            return {};
        }
        uint64_t sectionEnd = static_cast<uint64_t>(section.PointerToRawData) +
                              static_cast<uint64_t>(section.SizeOfRawData);
        if (sectionEnd > peEnd) {
            peEnd = sectionEnd;
        }
    }

    if (peEnd >= fileSize) {
        return {};
    }

    const uint32_t magic = 0x52534D45; // "EMSR"
    std::vector<uint8_t> found;

    auto parseTable = [&](uint64_t magicOffset) -> bool {
        if (magicOffset <= peEnd || magicOffset > fileSize) {
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

            std::string resourceName;
            resourceName.resize(nameLen);
            if (!readAt(offset, &resourceName[0], nameLen)) {
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

            if (resourceName == name) {
                found.resize(static_cast<size_t>(dataLen));
                if (!readAt(offset, found.data(), static_cast<size_t>(dataLen))) {
                    return false;
                }
            }

            offset += dataLen;
        }

        return offset == magicOffset;
    };

    const size_t chunkSize = 64 * 1024;
    uint64_t searchEnd = fileSize;
    while (searchEnd > peEnd + sizeof(uint32_t)) {
        uint64_t readStart = searchEnd > chunkSize ? searchEnd - chunkSize : 0;
        if (readStart < peEnd) {
            readStart = peEnd;
        }
        size_t readSize = static_cast<size_t>(searchEnd - readStart);
        if (readSize < sizeof(uint32_t)) {
            break;
        }

        std::vector<uint8_t> buffer(readSize);
        if (!readAt(readStart, buffer.data(), readSize)) {
            return {};
        }

        for (long long i = static_cast<long long>(readSize) - 4; i >= 0; --i) {
            uint32_t candidate = 0;
            std::memcpy(&candidate, buffer.data() + i, sizeof(candidate));
            if (candidate != magic) {
                continue;
            }

            uint64_t magicOffset = readStart + static_cast<uint64_t>(i);
            if (parseTable(magicOffset)) {
                return found;
            }
        }

        if (readStart == peEnd) {
            break;
        }
        searchEnd = readStart + 3;
    }

    return {};
}

} // namespace MultiThreadedInstaller

