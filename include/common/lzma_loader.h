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
    typedef lzma_ret (*lzma_stream_decoder_func)(lzma_stream *strm, uint64_t memlimit, uint32_t flags);
    typedef lzma_ret (*lzma_auto_decoder_func)(lzma_stream *strm, uint64_t memlimit, uint32_t flags);
    typedef lzma_ret (*lzma_alone_decoder_func)(lzma_stream *strm, uint64_t memlimit);
    typedef lzma_ret (*lzma_code_func)(lzma_stream *strm, lzma_action action);
    typedef void (*lzma_end_func)(lzma_stream *strm);
    typedef uint64_t (*lzma_version_number_func)(void);
    typedef lzma_ret (*lzma_stream_buffer_decode_func)(uint64_t *memlimit, uint32_t flags,
                                                        const lzma_allocator *allocator,
                                                        const uint8_t *in, size_t *in_pos, size_t in_size,
                                                        uint8_t *out, size_t *out_pos, size_t out_size);
    
    // Multi-threaded compression (liblzma >= 5.2.0)
    typedef lzma_ret (*lzma_stream_encoder_mt_func)(lzma_stream *strm, const lzma_mt *options);
    typedef uint64_t (*lzma_stream_encoder_mt_memusage_func)(const lzma_mt *options);
    
    // Block compression support
    typedef lzma_ret (*lzma_block_encoder_func)(lzma_stream *strm, lzma_block *block);
    typedef lzma_ret (*lzma_block_decoder_func)(lzma_stream *strm, lzma_block *block);
    
    // Compression function pointers
    lzma_easy_encoder_func lzma_easy_encoder_ptr;
    
    // Decompression function pointers
    lzma_stream_decoder_func lzma_stream_decoder_ptr;
    lzma_auto_decoder_func lzma_auto_decoder_ptr;
    lzma_alone_decoder_func lzma_alone_decoder_ptr;
    lzma_stream_buffer_decode_func lzma_stream_buffer_decode_ptr;
    
    // Multi-threaded compression function pointers (optional, liblzma >= 5.2.0)
    lzma_stream_encoder_mt_func lzma_stream_encoder_mt_ptr;
    lzma_stream_encoder_mt_memusage_func lzma_stream_encoder_mt_memusage_ptr;
    
    // Block compression function pointers (optional)
    lzma_block_encoder_func lzma_block_encoder_ptr;
    lzma_block_decoder_func lzma_block_decoder_ptr;
    
    // Common function pointers
    lzma_code_func lzma_code_ptr;
    lzma_end_func lzma_end_ptr;
    lzma_version_number_func lzma_version_number_ptr;
    
    // Capability flags
    bool supportsMultiThreadedCompression() const { return lzma_stream_encoder_mt_ptr != nullptr; }
    bool supportsBlockCompression() const { return lzma_block_encoder_ptr != nullptr && lzma_block_decoder_ptr != nullptr; }
    
    // Get LZMA version
    struct Version {
        uint32_t major;
        uint32_t minor;
        uint32_t patch;
    };
    Version getVersion() const;
    
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