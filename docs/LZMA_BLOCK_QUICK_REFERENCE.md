# LZMA 分块压缩快速参考

## 快速概览

LZMA 现在支持分块压缩/解压，自动处理大文件（> 128MB）。

## 关键参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 块大小 | 2MB | 每个压缩块的大小 |
| 大文件阈值 | 128MB | 触发块级压缩的文件大小 |
| 最大线程数 | 8 | 并行解压的最大线程数 |
| 每线程最小块数 | 4 | 批量处理的最小块数 |
| 压缩级别 | 5 | LZMA 压缩级别（平衡） |

## 代码示例

### 压缩（自动选择格式）

```cpp
#include "packager/compression_module.h"

CompressionModule compressor;
compressor.setCompressionAlgorithm(CompressionAlgorithm::LZMA_HIGH);
compressor.setCompressionLevel(5);

FolderInfo folder;
folder.sourcePath = "path/to/folder";
folder.files = { /* 文件列表 */ };

CompressionResult result = compressor.compressFolder(folder);
// 自动选择：> 128MB 使用块级压缩
```

### 解压（自动检测格式）

```cpp
#include "installer/decompression_engine.h"

auto threadPool = std::make_shared<ThreadPoolManager>(8);

DecompressionEngine decompressor;
decompressor.setThreadPool(threadPool);

DecompressionTask task;
task.compressedData = compressedData;
task.originalSize = originalSize;
task.expectedChecksum = checksum;
task.algorithm = CompressionAlgorithm::LZMA_HIGH;
task.targetPath = "output/path";

bool success = decompressor.decompressFolder(task);
// 自动检测并使用相应的解压方法
```

## 块格式结构

```
+------------------+
| 块数量 (4字节)    |
+------------------+
| 块元数据数组      |
|  - 偏移量 (4B)   |
|  - 压缩大小 (4B) |
|  - 原始大小 (4B) |
|  - 校验和 (4B)   |
|  (重复 N 次)     |
+------------------+
| 压缩块数据 1     |
+------------------+
| 压缩块数据 2     |
+------------------+
| ...              |
+------------------+
```

## 格式检测

### 标准 LZMA
- 首字节：`0x5D` 或 `0xFD`
- 使用标准 LZMA 解压

### 块级 LZMA
- 首4字节：块数量（1-100000）
- 验证元数据大小
- 使用块级解压

## 性能提示

### 压缩
- 小文件（≤ 128MB）：标准 LZMA，单线程
- 大文件（> 128MB）：块级 LZMA，为解压准备

### 解压
- 标准格式：单线程流式解压
- 块级格式：多线程并行解压（最多8线程）

### 线程调度
```
块数量 = 161
可用线程 = 28
最优线程 = min(161/4, 28, 8) = 8
每线程块数 = 161/8 ≈ 20
```

## 常见问题

### Q: 如何强制使用块级压缩？
A: 确保文件大小 > 128MB，系统会自动选择。

### Q: 可以调整块大小吗？
A: 可以，通过 `setBlockSize()` 方法，但推荐使用默认的 2MB。

### Q: 如何禁用多线程解压？
A: 不设置线程池或设置为 nullptr：
```cpp
decompressor.setThreadPool(nullptr);
```

### Q: LZMA 和 ZSTD 如何选择？
A: 
- ZSTD：速度优先，压缩比中等
- LZMA：压缩比优先，速度较慢

## 调试日志

启用详细日志查看格式检测和处理过程：

```
LZMA format detection for: folder_name
  First word: 0xa1
  Data size: 167772160 bytes
  Format: Block-based LZMA (161 blocks)
Using block-level LZMA decompression
Using 8 threads (of 28 available) for 161 LZMA blocks
Each thread processes ~20 blocks
Successfully decompressed 161 LZMA blocks, total size: 167772160 bytes
```

## 相关文件

- `include/packager/compression_module.h` - 压缩接口
- `src/packager/compression_module.cpp` - 压缩实现
- `include/installer/decompression_engine.h` - 解压接口
- `src/installer/decompression_engine.cpp` - 解压实现
- `include/common/types.h` - 常量定义

## 编译要求

```cmake
# CMakeLists.txt
find_package(LibLZMA REQUIRED)
target_link_libraries(target ${LIBLZMA_LIBRARIES})
target_include_directories(target PRIVATE ${LIBLZMA_INCLUDE_DIRS})
```

## 测试

```bash
# 编译
cmake --build build --target packager --config Release
cmake --build build --target installer --config Release

# 测试（需要 > 128MB 的测试文件）
./build/Release/packager.exe --input large_folder --output package.bin --algorithm lzma
./build/Release/installer.exe --package package.bin --output install_dir
```

## 性能基准（参考）

基于 160MB 测试数据：

| 操作 | ZSTD | LZMA |
|------|------|------|
| 压缩速度 | ~125 MB/s | ~30 MB/s |
| 解压速度 | ~139 MB/s | ~80 MB/s |
| 压缩比 | ~40% | ~25% |
| 块数量 | 161 | 161 |

*注：实际性能取决于硬件、数据类型和压缩级别*
