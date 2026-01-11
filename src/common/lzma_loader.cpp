#include "common/lzma_loader.h"
#include <iostream>

namespace MultiThreadedInstaller {

LzmaLoader::LzmaLoader() 
    : loaded(false)
    , lzma_easy_encoder_ptr(nullptr)
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
    // Try to load liblzma.dll
    hModule = LoadLibraryA("liblzma.dll");
    if (!hModule) {
        std::cerr << "Failed to load liblzma.dll" << std::endl;
        return false;
    }
    
    // Load function pointers
    if (!loadFunction(lzma_easy_encoder_ptr, "lzma_easy_encoder") ||
        !loadFunction(lzma_code_ptr, "lzma_code") ||
        !loadFunction(lzma_end_ptr, "lzma_end") ||
        !loadFunction(lzma_version_number_ptr, "lzma_version_number")) {
        unloadLibrary();
        return false;
    }
    
    std::cout << "Successfully loaded LZMA library dynamically" << std::endl;
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
    
    // Load function pointers
    if (!loadFunction(lzma_easy_encoder_ptr, "lzma_easy_encoder") ||
        !loadFunction(lzma_code_ptr, "lzma_code") ||
        !loadFunction(lzma_end_ptr, "lzma_end") ||
        !loadFunction(lzma_version_number_ptr, "lzma_version_number")) {
        unloadLibrary();
        return false;
    }
    
    std::cout << "Successfully loaded LZMA library dynamically" << std::endl;
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

} // namespace MultiThreadedInstaller