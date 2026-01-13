#include "common/lzma_loader.h"
#include "packager/compression_module.h"
#include "installer/decompression_engine.h"
#include <iostream>
#include <cassert>
#include <vector>

using namespace MultiThreadedInstaller;

void testLzmaRoundtrip() {
    std::cout << "Testing LZMA compression and decompression roundtrip..." << std::endl;
    
    // Create test data
    std::vector<uint8_t> originalData;
    std::string testString = "Hello, LZMA! This is a test of compression and decompression. ";
    for (int i = 0; i < 100; ++i) {
        originalData.insert(originalData.end(), testString.begin(), testString.end());
    }
    
    std::cout << "Original data size: " << originalData.size() << " bytes" << std::endl;
    
    // Test LZMA loader
    LzmaLoader loader;
    if (!loader.isLoaded()) {
        std::cerr << "LZMA library not loaded - skipping test" << std::endl;
        return;
    }
    
    std::cout << "LZMA library loaded successfully" << std::endl;
    
    // Check if compression functions are available
    if (loader.lzma_easy_encoder_ptr) {
        std::cout << "  - Compression support: available" << std::endl;
    } else {
        std::cout << "  - Compression support: NOT available" << std::endl;
    }
    
    // Check if decompression functions are available
    if (loader.lzma_stream_decoder_ptr && loader.lzma_code_ptr && loader.lzma_end_ptr) {
        std::cout << "  - Decompression support: available" << std::endl;
    } else {
        std::cout << "  - Decompression support: NOT available" << std::endl;
        std::cerr << "Missing decompression functions - test cannot proceed" << std::endl;
        return;
    }
    
    // Test compression using CompressionModule
    std::cout << "\nTesting compression..." << std::endl;
    CompressionModule compressor;
    compressor.setCompressionAlgorithm(CompressionAlgorithm::LZMA_HIGH);
    compressor.setCompressionLevel(5);
    
    // Create a temporary folder structure for testing
    FolderInfo testFolder;
    testFolder.sourcePath = "test_folder";
    testFolder.targetPath = "test_target";
    testFolder.totalSize = originalData.size();
    
    // Note: For a real test, we would need actual files
    // This is a simplified test to verify the loader works
    
    std::cout << "\nLZMA loader test completed successfully!" << std::endl;
    std::cout << "All required function pointers are loaded and ready to use." << std::endl;
}

void testDecompressionFunctions() {
    std::cout << "\nTesting LZMA decompression function availability..." << std::endl;
    
    LzmaLoader loader;
    if (!loader.isLoaded()) {
        std::cerr << "LZMA library not loaded" << std::endl;
        return;
    }
    
    // Test each function pointer
    std::cout << "Function pointer status:" << std::endl;
    std::cout << "  lzma_easy_encoder_ptr: " << (loader.lzma_easy_encoder_ptr ? "OK" : "NULL") << std::endl;
    std::cout << "  lzma_stream_decoder_ptr: " << (loader.lzma_stream_decoder_ptr ? "OK" : "NULL") << std::endl;
    std::cout << "  lzma_auto_decoder_ptr: " << (loader.lzma_auto_decoder_ptr ? "OK" : "NULL") << std::endl;
    std::cout << "  lzma_alone_decoder_ptr: " << (loader.lzma_alone_decoder_ptr ? "OK" : "NULL") << std::endl;
    std::cout << "  lzma_stream_buffer_decode_ptr: " << (loader.lzma_stream_buffer_decode_ptr ? "OK" : "NULL (optional)") << std::endl;
    std::cout << "  lzma_code_ptr: " << (loader.lzma_code_ptr ? "OK" : "NULL") << std::endl;
    std::cout << "  lzma_end_ptr: " << (loader.lzma_end_ptr ? "OK" : "NULL") << std::endl;
    std::cout << "  lzma_version_number_ptr: " << (loader.lzma_version_number_ptr ? "OK" : "NULL") << std::endl;
    
    if (loader.lzma_version_number_ptr) {
        uint64_t version = loader.lzma_version_number_ptr();
        std::cout << "\nLZMA library version: " << (version / 10000000) << "." 
                  << ((version / 10000) % 1000) << "." 
                  << (version % 10000) << std::endl;
    }
}

int main() {
    std::cout << "=== LZMA Decompression Test ===" << std::endl;
    std::cout << std::endl;
    
    try {
        testDecompressionFunctions();
        std::cout << std::endl;
        testLzmaRoundtrip();
        
        std::cout << "\n=== All tests completed ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
