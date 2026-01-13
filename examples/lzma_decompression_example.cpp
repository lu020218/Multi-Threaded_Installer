/**
 * LZMA 解压示例代码
 * 
 * 本示例演示如何使用更新后的 LzmaLoader 和 DecompressionEngine
 * 进行 LZMA 格式的解压操作
 */

#include "common/lzma_loader.h"
#include "installer/decompression_engine.h"
#include "installer/thread_pool_manager.h"
#include <iostream>
#include <fstream>
#include <vector>

using namespace MultiThreadedInstaller;

/**
 * 示例1：检查 LZMA 库是否可用
 */
void example1_CheckLzmaAvailability() {
    std::cout << "=== Example 1: Check LZMA Availability ===" << std::endl;
    
    LzmaLoader loader;
    
    if (!loader.isLoaded()) {
        std::cerr << "LZMA library is not available on this system" << std::endl;
        return;
    }
    
    std::cout << "LZMA library loaded successfully!" << std::endl;
    
    // 检查压缩功能
    if (loader.lzma_easy_encoder_ptr) {
        std::cout << "✓ Compression support available" << std::endl;
    } else {
        std::cout << "✗ Compression support NOT available" << std::endl;
    }
    
    // 检查解压功能
    bool decompressionAvailable = 
        loader.lzma_stream_decoder_ptr != nullptr &&
        loader.lzma_code_ptr != nullptr &&
        loader.lzma_end_ptr != nullptr;
    
    if (decompressionAvailable) {
        std::cout << "✓ Decompression support available" << std::endl;
    } else {
        std::cout << "✗ Decompression support NOT available" << std::endl;
    }
    
    // 显示版本信息
    if (loader.lzma_version_number_ptr) {
        uint64_t version = loader.lzma_version_number_ptr();
        std::cout << "LZMA version: " 
                  << (version / 10000000) << "."
                  << ((version / 10000) % 1000) << "."
                  << (version % 10000) << std::endl;
    }
    
    std::cout << std::endl;
}

/**
 * 示例2：使用 DecompressionEngine 解压 LZMA 数据
 */
void example2_DecompressLzmaData() {
    std::cout << "=== Example 2: Decompress LZMA Data ===" << std::endl;
    
    // 注意：这是一个演示示例，实际使用时需要提供真实的压缩数据
    
    // 创建解压任务
    DecompressionTask task;
    task.algorithm = CompressionAlgorithm::LZMA_HIGH;
    task.targetPath = "./extracted_folder";
    task.originalSize = 1024 * 1024; // 1MB (示例值)
    task.expectedChecksum = 0x12345678; // 示例校验和
    
    // 在实际使用中，这里应该是从文件或内存中读取的压缩数据
    // task.compressedData = readCompressedDataFromFile("data.lzma");
    
    // 创建解压引擎
    DecompressionEngine engine;
    
    // 注册进度回调
    engine.registerProgressCallback([](const std::string& folder, float progress) {
        std::cout << "Decompressing " << folder << ": " 
                  << static_cast<int>(progress * 100) << "%" << std::endl;
    });
    
    // 执行解压（注意：由于没有真实数据，这里会失败）
    // bool success = engine.decompressFolder(task);
    
    std::cout << "Note: This is a demonstration. Real compressed data is needed." << std::endl;
    std::cout << std::endl;
}

/**
 * 示例3：使用线程池进行多文件夹并行解压
 */
void example3_ParallelDecompression() {
    std::cout << "=== Example 3: Parallel Decompression ===" << std::endl;
    
    // 创建线程池（使用 CPU 核心数）
    auto threadPool = std::make_shared<ThreadPoolManager>();
    std::cout << "Thread pool created with " 
              << threadPool->getTotalThreadCount() << " threads" << std::endl;
    
    // 创建解压引擎并设置线程池
    DecompressionEngine engine;
    engine.setThreadPool(threadPool);
    
    // 创建多个解压任务（示例）
    std::vector<DecompressionTask> tasks;
    
    for (int i = 0; i < 3; ++i) {
        DecompressionTask task;
        task.algorithm = CompressionAlgorithm::LZMA_HIGH;
        task.targetPath = "./folder_" + std::to_string(i);
        task.originalSize = 1024 * 1024;
        task.expectedChecksum = 0x12345678 + i;
        // task.compressedData = readCompressedDataFromFile("folder_" + std::to_string(i) + ".lzma");
        
        tasks.push_back(task);
    }
    
    // 并行解压所有文件夹
    std::cout << "Starting parallel decompression of " << tasks.size() << " folders..." << std::endl;
    
    // 在实际使用中：
    // for (const auto& task : tasks) {
    //     engine.decompressFolder(task);
    // }
    // threadPool->waitForAll();
    
    std::cout << "Note: This is a demonstration. Real compressed data is needed." << std::endl;
    std::cout << std::endl;
}

/**
 * 示例4：手动使用 LZMA 解压（低级 API）
 */
void example4_ManualLzmaDecompression() {
    std::cout << "=== Example 4: Manual LZMA Decompression (Low-level API) ===" << std::endl;
    
    LzmaLoader loader;
    if (!loader.isLoaded() || !loader.lzma_stream_decoder_ptr) {
        std::cerr << "LZMA decompression not available" << std::endl;
        return;
    }
    
    std::cout << "Manual decompression steps:" << std::endl;
    std::cout << "1. Initialize LZMA stream: lzma_stream stream = LZMA_STREAM_INIT;" << std::endl;
    std::cout << "2. Initialize decoder: lzma_stream_decoder(&stream, UINT64_MAX, 0);" << std::endl;
    std::cout << "3. Setup input/output buffers" << std::endl;
    std::cout << "4. Call lzma_code() in a loop until LZMA_STREAM_END" << std::endl;
    std::cout << "5. Clean up: lzma_end(&stream);" << std::endl;
    
    // 伪代码示例
    std::cout << "\nPseudo-code:" << std::endl;
    std::cout << R"(
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret = loader.lzma_stream_decoder_ptr(&stream, UINT64_MAX, 0);
    
    stream.next_in = compressedData.data();
    stream.avail_in = compressedData.size();
    
    std::vector<uint8_t> output(64 * 1024);
    while (true) {
        stream.next_out = output.data();
        stream.avail_out = output.size();
        
        ret = loader.lzma_code_ptr(&stream, LZMA_RUN);
        
        // Process output...
        
        if (ret == LZMA_STREAM_END) break;
    }
    
    loader.lzma_end_ptr(&stream);
    )" << std::endl;
    
    std::cout << std::endl;
}

/**
 * 示例5：错误处理最佳实践
 */
void example5_ErrorHandling() {
    std::cout << "=== Example 5: Error Handling Best Practices ===" << std::endl;
    
    try {
        LzmaLoader loader;
        
        // 检查库是否加载
        if (!loader.isLoaded()) {
            throw std::runtime_error("LZMA library not available");
        }
        
        // 检查必需的函数指针
        if (!loader.lzma_stream_decoder_ptr || 
            !loader.lzma_code_ptr || 
            !loader.lzma_end_ptr) {
            throw std::runtime_error("Required LZMA functions not available");
        }
        
        std::cout << "✓ All checks passed" << std::endl;
        
        // 在实际解压中处理 LZMA 错误码
        std::cout << "\nCommon LZMA error codes:" << std::endl;
        std::cout << "  LZMA_OK (0): Success" << std::endl;
        std::cout << "  LZMA_STREAM_END (1): End of stream" << std::endl;
        std::cout << "  LZMA_FORMAT_ERROR (7): Invalid format" << std::endl;
        std::cout << "  LZMA_DATA_ERROR (9): Data corruption" << std::endl;
        std::cout << "  LZMA_MEM_ERROR (5): Memory allocation failed" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    std::cout << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  LZMA Decompression Examples" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    try {
        example1_CheckLzmaAvailability();
        example2_DecompressLzmaData();
        example3_ParallelDecompression();
        example4_ManualLzmaDecompression();
        example5_ErrorHandling();
        
        std::cout << "========================================" << std::endl;
        std::cout << "  All examples completed!" << std::endl;
        std::cout << "========================================" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
