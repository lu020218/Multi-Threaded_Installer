#include "installer/platform/embedded_resources.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

namespace MultiThreadedInstaller {

namespace {

std::vector<uint8_t> ReadEmbeddedResourceFromCurrentModule(const std::string& name) {
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
    return {};
}

} // namespace

EmbeddedResourceManager::EmbeddedResourceManager() {
}

EmbeddedResourceManager::~EmbeddedResourceManager() {
}


std::vector<uint8_t> EmbeddedResourceManager::getEmbeddedResource(const std::string& name) {
    // 资源以原生 PE 资源（类型 "BINARY"）嵌入，直接经 FindResource/LoadResource 读取（O(1)、内存映射）。
    return ReadEmbeddedResourceFromCurrentModule(name);
}

std::vector<uint8_t> LoadEmbeddedBinaryResource(const std::string& name) {
    EmbeddedResourceManager manager;
    return manager.getEmbeddedResource(name);
}

bool ExtractEmbeddedBinaryResourceToFile(const std::string& name, const std::string& outputPath) {
    const std::vector<uint8_t> data = LoadEmbeddedBinaryResource(name);
    if (data.empty()) {
        return false;
    }
    try {
        const std::filesystem::path output = PathFromUtf8(outputPath);
        const std::filesystem::path parent = output.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        std::ofstream file(output, std::ios::binary);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return file.good();
    } catch (const std::exception& e) {
        logInstallerError(std::string("[GUI][RES] Failed to extract embedded binary resource: ") + e.what());
        return false;
    }
}

bool InjectBinaryResourceIntoFile(const std::string& filePath,
                                  const std::string& name,
                                  const std::vector<uint8_t>& data) {
    if (data.empty()) {
        logInstallerError("[RES] InjectBinaryResource: empty data for " + name);
        return false;
    }
    const std::wstring wpath = Utf8ToWide(filePath);
    // FALSE：保留目标 exe 已有的图标/版本/清单等资源，只新增/替换本资源。
    HANDLE update = BeginUpdateResourceW(wpath.c_str(), FALSE);
    if (update == NULL) {
        logInstallerError("[RES] BeginUpdateResource failed for " + filePath +
                          " (GetLastError=" + std::to_string(GetLastError()) + ")");
        return false;
    }
    if (!UpdateResourceA(update, "BINARY", name.c_str(),
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                         const_cast<void*>(static_cast<const void*>(data.data())),
                         static_cast<DWORD>(data.size()))) {
        EndUpdateResource(update, TRUE);  // discard
        logInstallerError("[RES] UpdateResource failed for " + name + " in " + filePath);
        return false;
    }
    if (!EndUpdateResource(update, FALSE)) {
        logInstallerError("[RES] EndUpdateResource failed for " + filePath +
                          " (GetLastError=" + std::to_string(GetLastError()) + ")");
        return false;
    }
    return true;
}

} // namespace MultiThreadedInstaller

