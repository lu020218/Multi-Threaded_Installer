#include "installer/metadata_parser.h"
#include <iostream>
#include <fstream>
#include <vector>

using namespace MultiThreadedInstaller;

int main(int argc, char* argv[]) {
    std::cout << "=== Installer Diagnostics ===" << std::endl;
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <installer_executable>" << std::endl;
        return 1;
    }
    
    std::string installerPath = argv[1];
    std::cout << "\n1. Checking installer file: " << installerPath << std::endl;
    
    // 检查文件是否存在
    std::ifstream file(installerPath, std::ios::binary);
    if (!file) {
        std::cerr << "   ERROR: Cannot open installer file" << std::endl;
        return 1;
    }
    
    // 获取文件大小
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::cout << "   File size: " << fileSize << " bytes (" 
              << (fileSize / (1024 * 1024)) << " MB)" << std::endl;
    
    // 读取整个文件
    std::vector<uint8_t> fileData(fileSize);
    file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
    file.close();
    
    std::cout << "   File loaded successfully" << std::endl;
    
    // 查找元数据标记
    std::cout << "\n2. Searching for metadata marker..." << std::endl;
    const std::string marker = "###METADATA_START###";
    
    size_t markerPos = std::string::npos;
    for (size_t i = 0; i <= fileSize - marker.length(); ++i) {
        if (std::memcmp(fileData.data() + i, marker.c_str(), marker.length()) == 0) {
            markerPos = i;
            break;
        }
    }
    
    if (markerPos == std::string::npos) {
        std::cerr << "   ERROR: Metadata marker not found!" << std::endl;
        std::cerr << "   This installer may not have embedded metadata." << std::endl;
        return 1;
    }
    
    std::cout << "   Metadata marker found at offset: " << markerPos << std::endl;
    
    // 解析元数据
    std::cout << "\n3. Parsing metadata..." << std::endl;
    
    size_t offset = markerPos + marker.length();
    
    // 读取元数据大小
    if (offset + sizeof(uint32_t) > fileSize) {
        std::cerr << "   ERROR: Cannot read metadata size" << std::endl;
        return 1;
    }
    
    uint32_t metadataSize = *reinterpret_cast<const uint32_t*>(fileData.data() + offset);
    offset += sizeof(uint32_t);
    
    std::cout << "   Metadata size: " << metadataSize << " bytes" << std::endl;
    
    if (offset + metadataSize > fileSize) {
        std::cerr << "   ERROR: Metadata size exceeds file size" << std::endl;
        return 1;
    }
    
    // 读取文件夹数量
    if (offset + sizeof(uint32_t) > fileSize) {
        std::cerr << "   ERROR: Cannot read folder count" << std::endl;
        return 1;
    }
    
    uint32_t folderCount = *reinterpret_cast<const uint32_t*>(fileData.data() + offset);
    offset += sizeof(uint32_t);
    
    std::cout << "   Folder count: " << folderCount << std::endl;
    
    // 解析每个文件夹的映射信息
    std::cout << "\n4. Folder mappings:" << std::endl;
    
    for (uint32_t i = 0; i < folderCount; ++i) {
        std::cout << "\n   Folder " << (i + 1) << ":" << std::endl;
        
        // 读取文件夹名称长度
        if (offset + sizeof(uint32_t) > fileSize) {
            std::cerr << "      ERROR: Cannot read folder name length" << std::endl;
            return 1;
        }
        
        uint32_t nameLength = *reinterpret_cast<const uint32_t*>(fileData.data() + offset);
        offset += sizeof(uint32_t);
        
        // 读取文件夹名称
        if (offset + nameLength > fileSize) {
            std::cerr << "      ERROR: Cannot read folder name" << std::endl;
            return 1;
        }
        
        std::string folderName(reinterpret_cast<const char*>(fileData.data() + offset), nameLength);
        offset += nameLength;
        std::cout << "      Name: " << folderName << std::endl;
        
        // 读取偏移量
        if (offset + sizeof(uint64_t) > fileSize) {
            std::cerr << "      ERROR: Cannot read offset" << std::endl;
            return 1;
        }
        
        uint64_t dataOffset = *reinterpret_cast<const uint64_t*>(fileData.data() + offset);
        offset += sizeof(uint64_t);
        std::cout << "      Offset: " << dataOffset << std::endl;
        
        // 读取压缩大小
        if (offset + sizeof(uint64_t) > fileSize) {
            std::cerr << "      ERROR: Cannot read compressed size" << std::endl;
            return 1;
        }
        
        uint64_t compressedSize = *reinterpret_cast<const uint64_t*>(fileData.data() + offset);
        offset += sizeof(uint64_t);
        std::cout << "      Compressed size: " << compressedSize << " bytes (" 
                  << (compressedSize / (1024 * 1024)) << " MB)" << std::endl;
        
        // 读取原始大小
        if (offset + sizeof(uint64_t) > fileSize) {
            std::cerr << "      ERROR: Cannot read original size" << std::endl;
            return 1;
        }
        
        uint64_t originalSize = *reinterpret_cast<const uint64_t*>(fileData.data() + offset);
        offset += sizeof(uint64_t);
        std::cout << "      Original size: " << originalSize << " bytes (" 
                  << (originalSize / (1024 * 1024)) << " MB)" << std::endl;
        
        // 读取校验和
        if (offset + sizeof(uint32_t) > fileSize) {
            std::cerr << "      ERROR: Cannot read checksum" << std::endl;
            return 1;
        }
        
        uint32_t checksum = *reinterpret_cast<const uint32_t*>(fileData.data() + offset);
        offset += sizeof(uint32_t);
        std::cout << "      Checksum: 0x" << std::hex << checksum << std::dec << std::endl;
        
        // 读取算法
        if (offset + sizeof(uint32_t) > fileSize) {
            std::cerr << "      ERROR: Cannot read algorithm" << std::endl;
            return 1;
        }
        
        uint32_t algorithm = *reinterpret_cast<const uint32_t*>(fileData.data() + offset);
        offset += sizeof(uint32_t);
        std::cout << "      Algorithm: " << (algorithm == 0 ? "ZSTD" : "LZMA") << std::endl;
        
        // 检查压缩数据是否在文件范围内
        if (dataOffset + compressedSize > fileSize) {
            std::cerr << "      ERROR: Compressed data exceeds file size!" << std::endl;
            std::cerr << "      Data range: " << dataOffset << " - " << (dataOffset + compressedSize) << std::endl;
            std::cerr << "      File size: " << fileSize << std::endl;
            return 1;
        }
        
        // 检查压缩数据格式
        if (algorithm == 0 && compressedSize >= 4) { // ZSTD
            uint32_t firstWord = *reinterpret_cast<const uint32_t*>(fileData.data() + dataOffset);
            const uint32_t ZSTD_MAGIC = 0xFD2FB528;
            
            if (firstWord == ZSTD_MAGIC) {
                std::cout << "      Format: Standard ZSTD" << std::endl;
            } else if (firstWord > 0 && firstWord < 10000) {
                std::cout << "      Format: Block-based ZSTD (blocks: " << firstWord << ")" << std::endl;
                
                // 验证块格式
                size_t blockOffset = dataOffset + sizeof(uint32_t);
                uint32_t blockCount = firstWord;
                
                struct BlockMeta {
                    uint32_t offset;
                    uint32_t compressedSize;
                    uint32_t originalSize;
                    uint32_t checksum;
                };
                
                if (blockOffset + blockCount * sizeof(BlockMeta) <= fileSize) {
                    std::cout << "      Block metadata: OK" << std::endl;
                    
                    // 读取第一个块的信息
                    const BlockMeta* blocks = reinterpret_cast<const BlockMeta*>(fileData.data() + blockOffset);
                    std::cout << "      First block offset: " << blocks[0].offset << std::endl;
                    std::cout << "      First block compressed: " << blocks[0].compressedSize << " bytes" << std::endl;
                    std::cout << "      First block original: " << blocks[0].originalSize << " bytes" << std::endl;
                } else {
                    std::cerr << "      ERROR: Block metadata exceeds file size!" << std::endl;
                }
            } else {
                std::cerr << "      WARNING: Unknown format (first word: 0x" << std::hex << firstWord << std::dec << ")" << std::endl;
            }
        }
    }
    
    std::cout << "\n=== Diagnostics Complete ===" << std::endl;
    std::cout << "Summary:" << std::endl;
    std::cout << "  - Metadata marker: FOUND" << std::endl;
    std::cout << "  - Folder count: " << folderCount << std::endl;
    std::cout << "  - All data ranges: VALID" << std::endl;
    
    return 0;
}
