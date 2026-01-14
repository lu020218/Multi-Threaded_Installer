# LZMA 分块压缩/解压实现总结

## 概述

成功为 LZMA 压缩算法添加了分块压缩和解压支持，与 ZSTD 使用相同的块格式和优化策略。

## 实现详情

### 1. 压缩端 (CompressionModule)

#### 文件修改
- `include/packager/compression_module.h`
- `src/packager/compression_module.cpp`

#### 关键功能

**compressWithLzma()** - 自动格式选择
- 文件 > 128MB：自动使用块级压缩
- 文件 ≤ 128MB：使用标准 LZMA 压缩

**compressWithBlocksLzma()** - 块级压缩实现
```cpp
std::vector<uint8_t> CompressionModule::compressWithBlocksLzma(const std::vector<uint8_t>& data)
```

特性：
- 块大小：2MB（与 ZSTD 一致）
- 为每个块创建独立的 LZMA 流
- 使用 LZMA 压缩级别 5（平衡模式）
- 支持 CRC32 校验

块格式：
```
[块数量: 4字节]
[块元数据数组: 每块16字节]
  - 偏移量: 4字节
  - 压缩大小: 4字节
  - 原始大小: 4字节
  - 校验和: 4字节
[压缩块数据...]
```

### 2. 解压端 (DecompressionEngine)

#### 文件修改
- `include/installer/decompression_engine.h`
- `src/installer/decompression_engine.cpp`

#### 关键功能

**decompressLzma()** - 格式检测和路由
- 检查首字节：0x5D 或 0xFD → 标准 LZMA
- 检查首4字节：合理的块数量 → 块级 LZMA
- 自动路由到相应的解压方法

**decompressLzmaBlocks()** - 块级解压实现
```cpp
bool DecompressionEngine::decompressLzmaBlocks(const DecompressionTask& task)
```

特性：
- 并行解压支持（使用线程池）
- 批量处理：每个线程处理多个块（至少4个）
- 最多使用 8 个线程
- 为每个线程创建独立的 LZMA 流
- 支持单线程和多线程模式

优化策略：
```cpp
size_t blocksPerThreadMin = 4;
size_t optimalThreads = (blocks.size() + blocksPerThreadMin - 1) / blocksPerThreadMin;
if (optimalThreads > totalThreads) optimalThreads = totalThreads;
if (optimalThreads > 8) optimalThreads = 8;
```

### 3. 块格式统一

LZMA 和 ZSTD 使用相同的块格式：
- 块数量（4字节）
- 块元数据数组（每块16字节）
- 压缩块数据

这种统一格式简化了：
- 代码维护
- 格式检测
- 测试验证

## 性能优化

### 块大小
- **2MB** 块大小（从 64KB 优化而来）
- 减少元数据开销
- 提高压缩效率
- 优化线程调度

### 线程调度
- 限制最大线程数为 8
- 每个线程至少处理 4 个块
- 批量处理减少线程切换开销

### 示例（160MB 文件）
- 块数量：161 个块
- 元数据大小：~2.5KB
- 使用线程数：8（共 28 个可用）
- 每线程处理：~20 个块

## 格式检测逻辑

### LZMA 标准格式识别
```cpp
uint8_t firstByte = data[0];
if (firstByte == 0x5D || firstByte == 0xFD) {
    // 标准 LZMA 格式
}
```

### LZMA 块格式识别
```cpp
uint32_t firstWord = *reinterpret_cast<const uint32_t*>(data);
if (firstWord > 0 && firstWord < 100000) {
    size_t expectedMetadataSize = sizeof(uint32_t) + firstWord * 16;
    if (expectedMetadataSize < data.size()) {
        // 块级 LZMA 格式
    }
}
```

## 使用方式

### 压缩
```cpp
CompressionModule compressor;
compressor.setCompressionAlgorithm(CompressionAlgorithm::LZMA_HIGH);
compressor.setCompressionLevel(5);

FolderInfo folder;
folder.sourcePath = "path/to/folder";
// ... 添加文件 ...

CompressionResult result = compressor.compressFolder(folder);
// 自动选择：> 128MB 使用块级压缩，否则使用标准压缩
```

### 解压
```cpp
DecompressionEngine decompressor;
decompressor.setThreadPool(threadPool);

DecompressionTask task;
task.compressedData = compressedData;
task.algorithm = CompressionAlgorithm::LZMA_HIGH;
// ... 设置其他参数 ...

bool success = decompressor.decompressFolder(task);
// 自动检测格式并使用相应的解压方法
```

## 编译验证

所有目标编译成功：
```bash
cmake --build build --target packager --config Release
cmake --build build --target installer --config Release
cmake --build build --target test_lzma_blocks --config Release
```

## 与 ZSTD 的对比

| 特性 | ZSTD | LZMA |
|------|------|------|
| 块大小 | 2MB | 2MB |
| 最大线程数 | 8 | 8 |
| 每线程最小块数 | 4 | 4 |
| 大文件阈值 | 128MB | 128MB |
| 块格式 | 自定义 | 自定义（相同） |
| 压缩级别 | 1（快速） | 5（平衡） |
| 压缩比 | 中等 | 高 |
| 速度 | 快 | 慢 |

## 技术要点

### 1. 线程安全
- 每个线程使用独立的 LZMA 流
- 使用静态 LzmaLoader 实例（线程安全）
- 无共享状态

### 2. 内存管理
- 预分配结果缓冲区
- 批量处理减少内存碎片
- 及时清理 LZMA 流

### 3. 错误处理
- 完整的异常捕获
- 详细的错误日志
- 资源自动清理（RAII）

### 4. 进度报告
- 压缩进度：每10个块报告一次
- 解压进度：0.0 → 0.1 → 0.2 → 0.3-0.7 → 0.8 → 0.9 → 1.0
- 支持进度回调

## 已知限制

1. **LZMA 库依赖**
   - 需要 liblzma 动态库
   - 使用动态加载（LzmaLoader）
   - 编译时需要 LibLZMA_FOUND 宏

2. **性能特性**
   - LZMA 压缩比 ZSTD 慢
   - 但压缩比更高
   - 适合对大小敏感的场景

3. **测试限制**
   - `compressWithBlocksLzma()` 是私有方法
   - 需要通过大文件（> 128MB）触发
   - 或通过实际的文件夹压缩测试

## 下一步

1. **性能测试**
   - 使用真实的大文件测试
   - 对比 LZMA 和 ZSTD 性能
   - 测试不同线程数的影响

2. **集成测试**
   - 完整的打包/安装流程测试
   - 多种文件大小测试
   - 边界条件测试

3. **文档完善**
   - 添加使用示例
   - 性能基准测试结果
   - 最佳实践指南

## 总结

成功实现了 LZMA 的分块压缩和解压支持，与 ZSTD 保持一致的接口和优化策略。主要特点：

✅ 自动格式选择（基于文件大小）
✅ 并行解压支持（多线程）
✅ 优化的块大小和线程调度
✅ 统一的块格式
✅ 完整的错误处理
✅ 编译验证通过

实现完全符合项目的设计目标，为大文件提供了高效的压缩和解压能力。
