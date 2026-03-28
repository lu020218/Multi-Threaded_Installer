#include "common/lzma_loader.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
namespace {

using MultiThreadedInstaller::Utf8ToWide;
using MultiThreadedInstaller::WideToUtf8;

std::string getEnvVar(const char* name) {
    std::wstring nameW = Utf8ToWide(name ? std::string(name) : std::string());
    if (nameW.empty()) {
        return {};
    }
    wchar_t buffer[32767];
    DWORD cap = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    DWORD len = GetEnvironmentVariableW(nameW.c_str(), buffer, cap);
    if (len == 0 || len >= cap) {
        return {};
    }
    return WideToUtf8(std::wstring(buffer, len));
}

std::string getExecutableDir() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    std::wstring exePath(path, len);
    size_t pos = exePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return {};
    }
    return WideToUtf8(exePath.substr(0, pos));
}

std::string getCurrentDir() {
    wchar_t buffer[MAX_PATH];
    DWORD len = GetCurrentDirectoryW(MAX_PATH, buffer);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return WideToUtf8(std::wstring(buffer, len));
}

bool fileExists(const std::string& path) {
    std::wstring pathW = Utf8ToWide(path);
    if (pathW.empty()) {
        return false;
    }
    DWORD attrs = GetFileAttributesW(pathW.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

HMODULE tryLoadLibrary(const std::string& path) {
    if (path.empty() || !fileExists(path)) {
        return nullptr;
    }
    std::wstring pathW = Utf8ToWide(path);
    if (pathW.empty()) {
        return nullptr;
    }
    return LoadLibraryW(pathW.c_str());
}

} // namespace
#endif

namespace MultiThreadedInstaller {

LzmaLoader::LzmaLoader() 
    : loaded(false)
    , lzma_easy_encoder_ptr(nullptr)
    , lzma_stream_decoder_ptr(nullptr)
    , lzma_auto_decoder_ptr(nullptr)
    , lzma_alone_decoder_ptr(nullptr)
    , lzma_stream_buffer_decode_ptr(nullptr)
    , lzma_stream_encoder_mt_ptr(nullptr)
    , lzma_stream_encoder_mt_memusage_ptr(nullptr)
    , lzma_block_encoder_ptr(nullptr)
    , lzma_block_decoder_ptr(nullptr)
    , lzma_code_ptr(nullptr)
    , lzma_end_ptr(nullptr)
    , lzma_version_number_ptr(nullptr)
#ifdef _WIN32
    , hModule(nullptr)
#else
    , handle(nullptr)
#endif
{
    loaded = loadLibrary();
}

LzmaLoader::~LzmaLoader() {
    unloadLibrary();
}

bool LzmaLoader::loadLibrary() {
#ifdef _WIN32
    std::vector<std::string> candidates;
    std::string envPath = getEnvVar("LZMA_DLL_PATH");
    if (!envPath.empty()) {
        candidates.push_back(envPath);
    }
    std::string envDir = getEnvVar("LZMA_DLL_DIR");
    if (!envDir.empty()) {
        candidates.push_back(envDir + "\\liblzma.dll");
    }
    std::string exeDir = getExecutableDir();
    if (!exeDir.empty()) {
        candidates.push_back(exeDir + "\\liblzma.dll");
    }
    std::string cwd = getCurrentDir();
    if (!cwd.empty()) {
        candidates.push_back(cwd + "\\liblzma.dll");
    }

    for (const auto& path : candidates) {
        hModule = tryLoadLibrary(path);
        if (hModule) {
            logInstallerInfo(std::string("[LZMA] Loaded liblzma.dll from: ") + path);
            break;
        }
    }

    if (!hModule) {
        hModule = LoadLibraryW(L"liblzma.dll");
    }
    if (!hModule) {
        logInstallerError("[LZMA] Failed to load liblzma.dll.");
        return false;
    }
    
    // Load compression function pointers
    bool compressionOk = loadFunction(lzma_easy_encoder_ptr, "lzma_easy_encoder");
    
    // Load decompression function pointers
    bool decompressionOk = 
        loadFunction(lzma_stream_decoder_ptr, "lzma_stream_decoder") &&
        loadFunction(lzma_auto_decoder_ptr, "lzma_auto_decoder") &&
        loadFunction(lzma_alone_decoder_ptr, "lzma_alone_decoder");
    
    // lzma_stream_buffer_decode is optional (for single-call decompression)
    loadFunction(lzma_stream_buffer_decode_ptr, "lzma_stream_buffer_decode");
    
    // Load common function pointers (required)
    bool commonOk = 
        loadFunction(lzma_code_ptr, "lzma_code") &&
        loadFunction(lzma_end_ptr, "lzma_end") &&
        loadFunction(lzma_version_number_ptr, "lzma_version_number");
    
    if (!commonOk) {
        logInstallerError("[LZMA] Failed to load required LZMA common functions.");
        unloadLibrary();
        return false;
    }
    
    if (!compressionOk && !decompressionOk) {
        logInstallerError("[LZMA] Failed to load both compression and decompression functions.");
        unloadLibrary();
        return false;
    }
    
    if (!compressionOk) {
        logInstallerWarning("[LZMA] Compression functions not available.");
    }
    
    if (!decompressionOk) {
        logInstallerWarning("[LZMA] Decompression functions not available.");
    }
    
    logInstallerInfo("[LZMA] Successfully loaded LZMA library dynamically.");
    if (compressionOk) logInstallerInfo("[LZMA] Compression support: enabled.");
    if (decompressionOk) logInstallerInfo("[LZMA] Decompression support: enabled.");
    
    return true;
    
#else
    // Linux/Unix implementation
    handle = dlopen("liblzma.so", RTLD_LAZY);
    if (!handle) {
        handle = dlopen("liblzma.so.5", RTLD_LAZY);
    }
    
    if (!handle) {
        logInstallerError(std::string("[LZMA] Failed to load liblzma.so: ") + dlerror());
        return false;
    }
    
    // Load compression function pointers
    bool compressionOk = loadFunction(lzma_easy_encoder_ptr, "lzma_easy_encoder");
    
    // Load decompression function pointers
    bool decompressionOk = 
        loadFunction(lzma_stream_decoder_ptr, "lzma_stream_decoder") &&
        loadFunction(lzma_auto_decoder_ptr, "lzma_auto_decoder") &&
        loadFunction(lzma_alone_decoder_ptr, "lzma_alone_decoder");
    
    // lzma_stream_buffer_decode is optional (for single-call decompression)
    loadFunction(lzma_stream_buffer_decode_ptr, "lzma_stream_buffer_decode");
    
    // Load common function pointers (required)
    bool commonOk = 
        loadFunction(lzma_code_ptr, "lzma_code") &&
        loadFunction(lzma_end_ptr, "lzma_end") &&
        loadFunction(lzma_version_number_ptr, "lzma_version_number");
    
    if (!commonOk) {
        logInstallerError("[LZMA] Failed to load required LZMA common functions.");
        unloadLibrary();
        return false;
    }
    
    if (!compressionOk && !decompressionOk) {
        logInstallerError("[LZMA] Failed to load both compression and decompression functions.");
        unloadLibrary();
        return false;
    }
    
    if (!compressionOk) {
        logInstallerWarning("[LZMA] Compression functions not available.");
    }
    
    if (!decompressionOk) {
        logInstallerWarning("[LZMA] Decompression functions not available.");
    }
    
    logInstallerInfo("[LZMA] Successfully loaded LZMA library dynamically.");
    if (compressionOk) logInstallerInfo("[LZMA] Compression support: enabled.");
    if (decompressionOk) logInstallerInfo("[LZMA] Decompression support: enabled.");
    
    return true;
#endif
}

void LzmaLoader::unloadLibrary() {
#ifdef _WIN32
    if (hModule) {
        FreeLibrary(hModule);
        hModule = nullptr;
    }
#else
    if (handle) {
        dlclose(handle);
        handle = nullptr;
    }
#endif
    
    lzma_easy_encoder_ptr = nullptr;
    lzma_stream_decoder_ptr = nullptr;
    lzma_auto_decoder_ptr = nullptr;
    lzma_alone_decoder_ptr = nullptr;
    lzma_stream_buffer_decode_ptr = nullptr;
    lzma_stream_encoder_mt_ptr = nullptr;
    lzma_stream_encoder_mt_memusage_ptr = nullptr;
    lzma_block_encoder_ptr = nullptr;
    lzma_block_decoder_ptr = nullptr;
    lzma_code_ptr = nullptr;
    lzma_end_ptr = nullptr;
    lzma_version_number_ptr = nullptr;
    loaded = false;
}

template<typename T>
bool LzmaLoader::loadFunction(T& func, const char* name) {
#ifdef _WIN32
    func = reinterpret_cast<T>(GetProcAddress(hModule, name));
#else
    func = reinterpret_cast<T>(dlsym(handle, name));
#endif
    
    if (!func) {
        logInstallerError(std::string("[LZMA] Failed to load function: ") + name);
        return false;
    }
    
    return true;
}

LzmaLoader::Version LzmaLoader::getVersion() const {
    Version ver = {0, 0, 0};
    
    if (lzma_version_number_ptr) {
        uint64_t version = lzma_version_number_ptr();
        ver.major = static_cast<uint32_t>(version / 10000000);
        ver.minor = static_cast<uint32_t>((version / 10000) % 1000);
        ver.patch = static_cast<uint32_t>(version % 10000);
    }
    
    return ver;
}

} // namespace MultiThreadedInstaller
