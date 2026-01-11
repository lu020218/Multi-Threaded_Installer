#pragma once

#ifdef LibLZMA_FOUND
#include <lzma.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace MultiThreadedInstaller {

class LzmaLoader {
public:
    LzmaLoader();
    ~LzmaLoader();
    
    bool isLoaded() const { return loaded; }
    
    // LZMA function pointers
    typedef lzma_ret (*lzma_easy_encoder_func)(lzma_stream *strm, uint32_t preset, lzma_check check);
    typedef lzma_ret (*lzma_code_func)(lzma_stream *strm, lzma_action action);
    typedef void (*lzma_end_func)(lzma_stream *strm);
    typedef uint64_t (*lzma_version_number_func)(void);
    
    lzma_easy_encoder_func lzma_easy_encoder_ptr;
    lzma_code_func lzma_code_ptr;
    lzma_end_func lzma_end_ptr;
    lzma_version_number_func lzma_version_number_ptr;
    
private:
    bool loaded;
    
#ifdef _WIN32
    HMODULE hModule;
#else
    void* handle;
#endif
    
    bool loadLibrary();
    void unloadLibrary();
    template<typename T>
    bool loadFunction(T& func, const char* name);
};

} // namespace MultiThreadedInstaller