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

/// liblzma 的运行时动态加载器：在构造时尝试加载 liblzma 并解析所需函数指针，
/// 之后通过本类的函数指针成员调用 XZ/LZMA2 压缩/解压。多线程压缩/解压能力按
/// 运行时探测（见 supportsMultiThreaded*），缺失时回退单线程。
class LzmaLoader {
public:
    LzmaLoader();
    ~LzmaLoader();

    /// liblzma 是否成功加载（失败时上层应回退或报错）。
    bool isLoaded() const { return loaded; }
    
    // LZMA function pointers
    typedef lzma_ret (*lzma_easy_encoder_func)(lzma_stream *strm, uint32_t preset, lzma_check check);
    typedef lzma_ret (*lzma_stream_decoder_func)(lzma_stream *strm, uint64_t memlimit, uint32_t flags);
    typedef lzma_ret (*lzma_stream_decoder_mt_func)(lzma_stream *strm, const lzma_mt *options);
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
    
    // Compression function pointers
    lzma_easy_encoder_func lzma_easy_encoder_ptr;
    
    // Decompression function pointers
    lzma_stream_decoder_func lzma_stream_decoder_ptr;
    lzma_stream_decoder_mt_func lzma_stream_decoder_mt_ptr;
    lzma_auto_decoder_func lzma_auto_decoder_ptr;
    lzma_alone_decoder_func lzma_alone_decoder_ptr;
    lzma_stream_buffer_decode_func lzma_stream_buffer_decode_ptr;
    
    // Multi-threaded compression function pointers (optional, liblzma >= 5.2.0)
    lzma_stream_encoder_mt_func lzma_stream_encoder_mt_ptr;
    lzma_stream_encoder_mt_memusage_func lzma_stream_encoder_mt_memusage_ptr;
    
    // Common function pointers
    lzma_code_func lzma_code_ptr;
    lzma_end_func lzma_end_ptr;
    lzma_version_number_func lzma_version_number_ptr;
    
    // 能力探测：对应函数指针非空即支持。
    bool supportsMultiThreadedCompression() const { return lzma_stream_encoder_mt_ptr != nullptr; }   ///< 是否支持多线程压缩。
    bool supportsMultiThreadedDecompression() const { return lzma_stream_decoder_mt_ptr != nullptr; }  ///< 是否支持多线程解压。

    /// liblzma 版本号（major.minor.patch）。
    struct Version {
        uint32_t major;
        uint32_t minor;
        uint32_t patch;
    };
    Version getVersion() const;

private:
    bool loaded;  ///< 加载是否成功。

#ifdef _WIN32
    HMODULE hModule;  ///< liblzma 动态库句柄（Windows）。
#else
    void* handle;     ///< liblzma 动态库句柄（dlopen）。
#endif

    bool loadLibrary();    ///< 加载动态库并解析全部函数指针。
    void unloadLibrary();  ///< 卸载动态库。
    /// 解析单个导出函数到 func；失败返回 false。
    template<typename T>
    bool loadFunction(T& func, const char* name);
};

} // namespace MultiThreadedInstaller
