#include "common/lzma_loader.h"
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
namespace {

std::string getEnvVar(const char* name) {
    char buffer[32767];
    DWORD len = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
    if (len == 0 || len >= sizeof(buffer)) {
        return {};
    }
    return std::string(buffer, len);
}

std::string getExecutableDir() {
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    std::string exePath(path, len);
    size_t pos = exePath.find_last_of("\/");
    if (pos == std::string::npos) {
        return {};
    }
    return exePath.substr(0, pos);
}

std::string getCurrentDir() {
    char buffer[MAX_PATH];
    DWORD len = GetCurrentDirectoryA(MAX_PATH, buffer);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::string(buffer, len);
}

bool fileExists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

HMODULE tryLoadLibrary(const std::string& path) {
    if (path.empty() || !fileExists(path)) {
        return nullptr;
    }
    return LoadLibraryA(path.c_str());
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
            std::cout << "Loaded liblzma.dll from: " << path << std::endl;
            break;
        }
    }

    if (!hModule) {
        hModule = LoadLibraryA("liblzma.dll");
    }
    if (!hModule) {
        std::cerr << "Failed to load liblzma.dll" << std::endl;
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
        std::cerr << "Failed to load required LZMA common functions" << std::endl;
        unloadLibrary();
        return false;
    }
    
    if (!compressionOk && !decompressionOk) {
        std::cerr << "Failed to load both compression and decompression functions" << std::endl;
        unloadLibrary();
        return false;
    }
    
    if (!compressionOk) {
        std::cerr << "Warning: LZMA compression functions not available" << std::endl;
    }
    
    if (!decompressionOk) {
        std::cerr << "Warning: LZMA decompression functions not available" << std::endl;
    }
    
    std::cout << "Successfully loaded LZMA library dynamically" << std::endl;
    if (compressionOk) std::cout << "  - Compression support: enabled" << std::endl;
    if (decompressionOk) std::cout << "  - Decompression support: enabled" << std::endl;
    
    return true;
    
#else
    // Linux/Unix implementation
    handle = dlopen("liblzma.so", RTLD_LAZY);
    if (!handle) {
        handle = dlopen("liblzma.so.5", RTLD_LAZY);
    }
    
    if (!handle) {
        std::cerr << "Failed to load liblzma.so: " << dlerror() << std::endl;
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
        std::cerr << "Failed to load required LZMA common functions" << std::endl;
        unloadLibrary();
        return false;
    }
    
    if (!compressionOk && !decompressionOk) {
        std::cerr << "Failed to load both compression and decompression functions" << std::endl;
        unloadLibrary();
        return false;
    }
    
    if (!compressionOk) {
        std::cerr << "Warning: LZMA compression functions not available" << std::endl;
    }
    
    if (!decompressionOk) {
        std::cerr << "Warning: LZMA decompression functions not available" << std::endl;
    }
    
    std::cout << "Successfully loaded LZMA library dynamically" << std::endl;
    if (compressionOk) std::cout << "  - Compression support: enabled" << std::endl;
    if (decompressionOk) std::cout << "  - Decompression support: enabled" << std::endl;
    
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
        std::cerr << "Failed to load function: " << name << std::endl;
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
