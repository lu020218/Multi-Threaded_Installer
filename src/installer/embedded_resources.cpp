#ifdef GUI_ENABLED

#include "../../include/installer/embedded_resources.h"
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
    
    // 创建临时目录
    m_resourcePath = createTempDirectory();
    if (m_resourcePath.empty()) {
        std::cerr << "Failed to create temporary directory for resources" << std::endl;
        return "";
    }
    
    std::cout << "Extracting embedded resources to: " << m_resourcePath << std::endl;
    
    // 创建子目录
    std::filesystem::create_directories(m_resourcePath + "\\skins");
    std::filesystem::create_directories(m_resourcePath + "\\images");
    std::filesystem::create_directories(m_resourcePath + "\\lang");
    
    bool anyExtracted = false;
    
    // 提取 DuiLib.dll
    auto duilib = getEmbeddedResource("DUILIB_DLL");
    if (!duilib.empty()) {
        if (extractFile("DuiLib.dll", duilib)) {
            std::cout << "  Extracted: DuiLib.dll" << std::endl;
            anyExtracted = true;
        }
    }

    auto packedZip = getEmbeddedResource("RES_ZIP");
    if (!packedZip.empty()) {
        if (extractFile("resources.zip", packedZip)) {
            std::cout << "  Extracted: resources.zip" << std::endl;
            anyExtracted = true;
            m_extracted = true;
            return m_resourcePath;
        }
    }
    
    // 提取 XML 资源
    const char* xmlFiles[] = {
        "main.xml",
        "uninstall_main.xml",
        "welcome_page.xml",
        "progress_page.xml",
        "completion_page.xml",
        "uninstall_confirm_page.xml",
        "uninstall_progress_page.xml",
        "uninstall_completion_page.xml",
        "license.xml",
        "msgBox.xml"
    };
    
    for (const char* xmlFile : xmlFiles) {
        std::string resourceName = "XML_";
        resourceName += xmlFile;
        // 转换为大写
        for (char& c : resourceName) {
            if (c == '.') c = '_';
            c = toupper(c);
        }
        
        auto data = getEmbeddedResource(resourceName);
        if (!data.empty()) {
            std::string path = "skins\\";
            path += xmlFile;
            if (extractFile(path, data)) {
                std::cout << "  Extracted: " << path << std::endl;
                anyExtracted = true;
            }
        }
    }
    
    // 提取 license.txt
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
                        std::cout << "  Extracted: " << path << std::endl;
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
                        std::cout << "  Extracted: " << path << std::endl;
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
            std::cout << "  Extracted: license.txt" << std::endl;
            anyExtracted = true;
        }
    }
    
    // 如果没有提取任何资源，清理并返回空字符串
    if (!anyExtracted) {
        std::cout << "No embedded resources found, will use external resources" << std::endl;
        cleanup();
        return "";
    }
    
    m_extracted = true;
    return m_resourcePath;
}

void EmbeddedResourceManager::cleanup() {
    if (!m_resourcePath.empty() && m_extracted) {
        try {
            std::filesystem::remove_all(m_resourcePath);
            std::cout << "Cleaned up temporary resources: " << m_resourcePath << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to cleanup resources: " << e.what() << std::endl;
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
    
    // 创建唯一的临时目录名
    std::wstring tempDir = tempPath;
    tempDir += L"MTInstaller_";
    tempDir += std::to_wstring(GetCurrentProcessId());
    tempDir += L"_";
    tempDir += std::to_wstring(GetTickCount64());
    
    // 转换为窄字符串
    int size = WideCharToMultiByte(CP_UTF8, 0, tempDir.c_str(), -1, NULL, 0, NULL, NULL);
    if (size <= 0) {
        return "";
    }
    
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, tempDir.c_str(), -1, &result[0], size, NULL, NULL);
    
    // 创建目录
    if (!std::filesystem::create_directories(result)) {
        return "";
    }
    
    return result;
}

bool EmbeddedResourceManager::extractFile(const std::string& relativePath, 
                                         const std::vector<uint8_t>& data) {
    if (m_resourcePath.empty() || data.empty()) {
        return false;
    }
    
    std::string fullPath = m_resourcePath + "\\" + relativePath;
    
    try {
        std::ofstream file(fullPath, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to create file: " << fullPath << std::endl;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error extracting file " << relativePath << ": " << e.what() << std::endl;
        return false;
    }
}

std::vector<uint8_t> EmbeddedResourceManager::getEmbeddedResource(const std::string& name) {
    // 首先尝试从 Windows 资源加载（如果使用 .rc 文件）
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
    
    // 如果 Windows 资源不存在，尝试从文件末尾读取嵌入的资源
    // 这是通过 embed_resources.ps1 脚本添加的
    return readEmbeddedResourceFromFile(name);
}

std::vector<uint8_t> EmbeddedResourceManager::readEmbeddedResourceFromFile(const std::string& name) {
    // Resolve current executable path.
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
        return {};
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, exePath, -1, NULL, 0, NULL, NULL);
    if (size <= 0) {
        return {};
    }

    std::string exePathStr(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, exePath, -1, &exePathStr[0], size, NULL, NULL);

    std::ifstream file(exePathStr, std::ios::binary);
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

#endif // GUI_ENABLED
