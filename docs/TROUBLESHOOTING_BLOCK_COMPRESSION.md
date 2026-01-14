# 分块压缩安装包故障排查指南

## 问题描述
使用 ZSTD 分块压缩生成的安装包运行后没有任何输出。

## 可能的原因

### 1. 元数据问题
- 元数据标记未正确嵌入
- 元数据偏移量计算错误
- 压缩数据偏移量不正确

### 2. 格式检测问题
- 分块格式标识不正确
- 魔数检测逻辑错误
- 块数量字段损坏

### 3. 解压失败
- 块元数据损坏
- 块数据偏移量错误
- ZSTD 解压失败但未报错

### 4. 文件提取问题
- TAR 数据格式错误
- 文件写入权限问题
- 目标路径创建失败

## 诊断步骤

### 步骤 1: 使用诊断工具检查安装包

```bash
# 编译诊断工具
cd build
cmake --build . --target diagnose_installer --config Release

# 运行诊断
.\Release\diagnose_installer.exe <your_installer.exe>
```

诊断工具会检查：
- ✓ 文件大小和完整性
- ✓ 元数据标记位置
- ✓ 文件夹数量和映射信息
- ✓ 压缩数据偏移量和大小
- ✓ 压缩格式（标准 ZSTD vs 分块）
- ✓ 块元数据完整性

### 步骤 2: 检查输出

#### 正常输出示例：
```
=== Installer Diagnostics ===

1. Checking installer file: installer.exe
   File size: 52428800 bytes (50 MB)
   File loaded successfully

2. Searching for metadata marker...
   Metadata marker found at offset: 51200000

3. Parsing metadata...
   Metadata size: 256 bytes
   Folder count: 1

4. Folder mappings:

   Folder 1:
      Name: test_folder
      Offset: 1024
      Compressed size: 51198976 bytes (48 MB)
      Original size: 167772160 bytes (160 MB)
      Checksum: 0x12345678
      Algorithm: ZSTD
      Format: Block-based ZSTD (blocks: 161)
      Block metadata: OK
      First block offset: 1700
      First block compressed: 318000 bytes
      First block original: 1048576 bytes

=== Diagnostics Complete ===
Summary:
  - Metadata marker: FOUND
  - Folder count: 1
  - All data ranges: VALID
```

#### 异常输出示例：

**问题 1: 元数据标记未找到**
```
ERROR: Metadata marker not found!
This installer may not have embedded metadata.
```
**解决方案**: 检查打包器是否正确嵌入元数据

**问题 2: 数据范围超出文件大小**
```
ERROR: Compressed data exceeds file size!
Data range: 1024 - 52428800
File size: 51200000
```
**解决方案**: 元数据中的偏移量或大小计算错误

**问题 3: 块元数据损坏**
```
ERROR: Block metadata exceeds file size!
```
**解决方案**: 分块压缩格式生成错误

### 步骤 3: 手动测试解压

创建测试程序验证解压逻辑：

```cpp
#include "installer/decompression_engine.h"
#include "installer/metadata_parser.h"
#include <iostream>

int main() {
    // 解析元数据
    MetadataParser parser;
    auto metadata = parser.parseEmbeddedMetadata();
    
    if (metadata.folderMappings.empty()) {
        std::cerr << "No folder mappings found" << std::endl;
        return 1;
    }
    
    // 读取第一个文件夹的压缩数据
    const auto& mapping = metadata.folderMappings[0];
    std::vector<uint8_t> compressedData = parser.readCompressedData(
        mapping.offset, mapping.compressedSize
    );
    
    std::cout << "Read " << compressedData.size() << " bytes of compressed data" << std::endl;
    
    // 检查格式
    if (compressedData.size() >= 4) {
        uint32_t firstWord = *reinterpret_cast<const uint32_t*>(compressedData.data());
        std::cout << "First word: 0x" << std::hex << firstWord << std::dec << std::endl;
        
        const uint32_t ZSTD_MAGIC = 0xFD2FB528;
        if (firstWord == ZSTD_MAGIC) {
            std::cout << "Format: Standard ZSTD" << std::endl;
        } else if (firstWord > 0 && firstWord < 10000) {
            std::cout << "Format: Block-based (" << firstWord << " blocks)" << std::endl;
        }
    }
    
    // 尝试解压
    DecompressionEngine decompressor;
    DecompressionTask task;
    task.compressedData = compressedData;
    task.targetPath = "test_output";
    task.algorithm = mapping.algorithm;
    task.originalSize = mapping.originalSize;
    task.expectedChecksum = mapping.checksum;
    
    std::cout << "Starting decompression..." << std::endl;
    bool success = decompressor.decompressFolder(task);
    
    if (success) {
        std::cout << "Decompression successful!" << std::endl;
    } else {
        std::cerr << "Decompression failed!" << std::endl;
    }
    
    return success ? 0 : 1;
}
```

### 步骤 4: 检查常见问题

#### 问题 A: 控制台窗口立即关闭
**症状**: 双击运行安装包后窗口闪现即消失  
**原因**: 程序崩溃或立即退出  
**解决方案**:
```bash
# 在命令行中运行以查看输出
cmd /k installer.exe

# 或添加暂停
installer.exe & pause
```

#### 问题 B: 无输出但进程仍在运行
**症状**: 任务管理器中看到进程，但无任何输出  
**原因**: 可能卡在某个操作上  
**解决方案**:
1. 检查是否在等待用户输入
2. 使用调试器附加进程
3. 添加更多日志输出

#### 问题 C: 解压失败但无错误信息
**症状**: 程序正常退出但文件未提取  
**原因**: 错误被静默处理  
**解决方案**:
1. 检查 `std::cerr` 输出
2. 启用详细日志
3. 检查返回值

### 步骤 5: 验证分块格式

手动验证分块压缩格式：

```cpp
// 读取块数量
uint32_t blockCount = *reinterpret_cast<const uint32_t*>(data);
std::cout << "Block count: " << blockCount << std::endl;

// 验证块数量合理性
if (blockCount == 0 || blockCount > 10000) {
    std::cerr << "Invalid block count: " << blockCount << std::endl;
    return false;
}

// 读取块元数据
struct BlockMeta {
    uint32_t offset;
    uint32_t compressedSize;
    uint32_t originalSize;
    uint32_t checksum;
};

const BlockMeta* blocks = reinterpret_cast<const BlockMeta*>(data + 4);

// 验证每个块
for (uint32_t i = 0; i < blockCount; ++i) {
    std::cout << "Block " << i << ":" << std::endl;
    std::cout << "  Offset: " << blocks[i].offset << std::endl;
    std::cout << "  Compressed: " << blocks[i].compressedSize << std::endl;
    std::cout << "  Original: " << blocks[i].originalSize << std::endl;
    
    // 验证偏移量
    if (blocks[i].offset + blocks[i].compressedSize > dataSize) {
        std::cerr << "  ERROR: Block exceeds data size!" << std::endl;
        return false;
    }
}
```

## 常见修复方案

### 修复 1: 确保正确的格式检测

在 `decompression_engine.cpp` 中：

```cpp
// 检查是否为块级格式
if (task.compressedData.size() >= 4) {
    uint32_t firstWord = *reinterpret_cast<const uint32_t*>(task.compressedData.data());
    const uint32_t ZSTD_MAGIC = 0xFD2FB528;
    
    // 添加详细日志
    std::cout << "First word: 0x" << std::hex << firstWord << std::dec << std::endl;
    
    if (firstWord != ZSTD_MAGIC && firstWord > 0 && firstWord < 10000) {
        std::cout << "Detected block format with " << firstWord << " blocks" << std::endl;
        return decompressZstdBlocks(task);
    } else {
        std::cout << "Detected standard ZSTD format" << std::endl;
    }
}
```

### 修复 2: 添加错误日志

在关键位置添加日志：

```cpp
bool DecompressionEngine::decompressZstdBlocks(const DecompressionTask& task) {
    std::cout << "=== Starting block decompression ===" << std::endl;
    std::cout << "Task target: " << task.targetPath << std::endl;
    std::cout << "Compressed size: " << task.compressedData.size() << std::endl;
    std::cout << "Expected original size: " << task.originalSize << std::endl;
    
    // ... 解压逻辑 ...
    
    std::cout << "=== Block decompression complete ===" << std::endl;
    return true;
}
```

### 修复 3: 验证校验和逻辑

检查校验和计算是否一致：

```cpp
bool DecompressionEngine::verifyChecksum(const std::vector<uint8_t>& data, uint32_t expected) {
    uint32_t calculated = calculateChecksum(data);
    
    std::cout << "Checksum verification:" << std::endl;
    std::cout << "  Expected: 0x" << std::hex << expected << std::dec << std::endl;
    std::cout << "  Calculated: 0x" << std::hex << calculated << std::dec << std::endl;
    
    if (calculated != expected) {
        std::cerr << "Checksum mismatch!" << std::endl;
        return false;
    }
    
    return true;
}
```

## 调试技巧

### 1. 启用详细输出
在编译时定义调试宏：
```cmake
add_definitions(-DVERBOSE_LOGGING)
```

### 2. 使用调试器
```bash
# Visual Studio
devenv installer.exe

# GDB
gdb installer.exe
(gdb) run
(gdb) bt  # 查看调用栈
```

### 3. 检查文件权限
```bash
# 确保有写入权限
icacls output_folder /grant Users:F
```

### 4. 比对测试
```bash
# 使用测试程序验证
.\test_zstd_blocks.exe

# 对比标准格式和分块格式
.\packager.exe --algorithm zstd --level 3 --no-blocks
.\packager.exe --algorithm zstd --level 3 --use-blocks
```

## 预防措施

### 1. 添加完整性检查
在打包时验证生成的数据：
```cpp
// 打包后立即验证
CompressionResult result = compressor.compressFolder(folder);
if (!verifyCompressionResult(result)) {
    std::cerr << "Compression verification failed!" << std::endl;
    return false;
}
```

### 2. 添加版本标识
在元数据中添加版本号：
```cpp
struct Metadata {
    uint32_t version = 1;  // 格式版本
    uint32_t flags = 0;    // 特性标志
    // ...
};
```

### 3. 添加自检功能
安装包启动时自检：
```cpp
bool selfCheck() {
    // 验证元数据
    // 验证压缩数据范围
    // 验证格式标识
    return true;
}
```

## 总结

使用诊断工具和详细日志可以快速定位问题：

1. ✓ 运行 `diagnose_installer.exe` 检查元数据
2. ✓ 检查压缩格式识别
3. ✓ 验证块元数据完整性
4. ✓ 测试解压逻辑
5. ✓ 检查文件提取

如果问题仍然存在，请提供：
- 诊断工具的完整输出
- 安装包的大小和生成参数
- 任何错误消息或异常行为
