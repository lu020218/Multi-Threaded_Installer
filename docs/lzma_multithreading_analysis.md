# LZMA 多线程和分块压缩/解压分析

## 当前实现状态分析

### ✅ 已支持的功能

#### 1. ZSTD 多线程支持
```cpp
// compression_module.cpp 中已实现
ZSTD_CCtx_setParameter(zstdContext, ZSTD_c_nbWorkers, std::thread::hardware_concurrency());
ZSTD_CCtx_setParameter(zstdContext, ZSTD_c_jobSize, blockSize);
```
- ✅ **原生多线程压缩**：ZSTD 支持多线程压缩
- ✅ **块级压缩**：支持 64KB 块大小
- ✅ **随机访问**：块级压缩支持随机访问

#### 2. 文件夹级并行
```cpp
// 在 packager/main.cpp 中
for (const auto& folder : folders) {
    auto result = compressor.compressFolder(folder);
    // 每个文件夹独立压缩
}
```
- ✅ **文件夹级并行**：不同文件夹可以并行处理
- ✅ **线程池支持**：解压时使用 ThreadPoolManager

### ❌ 当前缺失的功能

#### 1. LZMA 多线程压缩
- ❌ **当前实现**：使用 `lzma_easy_encoder`，单线程
- ❌ **缺少接口**：`lzma_mt_encoder` 多线程编码器
- ❌ **缺少配置**：`lzma_mt` 多线程配置结构

#### 2. LZMA 分块压缩
- ❌ **当前实现**：整个数据流一次性压缩
- ❌ **缺少接口**：块级独立压缩/解压
- ❌ **缺少索引**：块索引和元数据管理

#### 3. LZMA 多线程解压
- ⚠️ **部分支持**：可以通过文件夹级并行实现
- ❌ **单文件多线程**：LZMA 解压本身不支持多线程
- ❌ **块级并行解压**：需要自定义实现

## LZMA 多线程技术方案

### 方案 1：LZMA 多线程压缩（推荐）

#### 需要添加的函数指针

```cpp
// 多线程编码器
typedef lzma_ret (*lzma_stream_encoder_mt_func)(lzma_stream *strm, const lzma_mt *options);

// 获取内存使用量
typedef uint64_t (*lzma_stream_encoder_mt_memusage_func)(const lzma_mt *options);
```

#### 配置结构
```cpp
// lzma_mt 结构体（liblzma 5.2.0+）
struct lzma_mt_config {
    uint32_t flags;           // 编码标志
    uint32_t threads;         // 线程数
    uint64_t block_size;      // 块大小
    uint32_t timeout;         // 超时时间
    uint32_t preset;          // 压缩级别
    lzma_check check;         // 校验类型
};
```

#### 优势
- ✅ 原生多线程支持（liblzma 5.2.0+）
- ✅ 自动块管理
- ✅ 性能提升 2-4 倍（取决于核心数）

#### 限制
- ⚠️ 需要 liblzma >= 5.2.0
- ⚠️ 仅压缩支持多线程，解压仍是单线程
- ⚠️ 内存使用增加（每线程 ~50-100MB）

### 方案 2：自定义分块压缩/解压

#### 设计思路
```
原始数据 → 分块 → 独立压缩每块 → 添加索引 → 合并
         ↓
    [Block 1] [Block 2] [Block 3] ... [Block N]
         ↓         ↓         ↓             ↓
    [Compressed] [Compressed] [Compressed] [Compressed]
         ↓
    [Index: offsets, sizes, checksums]
```

#### 数据格式
```
+------------------+
| Header           |
| - Magic: "MTLZ"  |
| - Version        |
| - Block Count    |
| - Block Size     |
+------------------+
| Block Index      |
| - Block 1 Info   |
| - Block 2 Info   |
| - ...            |
+------------------+
| Block 1 Data     |
+------------------+
| Block 2 Data     |
+------------------+
| ...              |
+------------------+
```

#### 优势
- ✅ 支持并行压缩和解压
- ✅ 支持随机访问
- ✅ 不依赖 liblzma 版本
- ✅ 可控的内存使用

#### 限制
- ⚠️ 压缩比略低（块边界开销）
- ⚠️ 需要自定义格式
- ⚠️ 实现复杂度较高

### 方案 3：混合方案（最佳实践）

#### 策略
1. **检测 liblzma 版本**
   - >= 5.2.0 → 使用原生多线程
   - < 5.2.0 → 使用自定义分块

2. **根据文件大小选择**
   - 小文件（< 10MB）→ 单线程
   - 中文件（10-100MB）→ 2-4 线程
   - 大文件（> 100MB）→ 最大线程数

3. **文件夹级 + 块级并行**
   - 不同文件夹并行处理
   - 大文件内部分块并行

## 实现计划

### 阶段 1：添加多线程压缩接口（高优先级）

#### 1.1 更新 lzma_loader.h
```cpp
// 添加多线程编码器函数指针
typedef lzma_ret (*lzma_stream_encoder_mt_func)(lzma_stream *strm, const lzma_mt *options);
typedef uint64_t (*lzma_stream_encoder_mt_memusage_func)(const lzma_mt *options);

lzma_stream_encoder_mt_func lzma_stream_encoder_mt_ptr;
lzma_stream_encoder_mt_memusage_func lzma_stream_encoder_mt_memusage_ptr;
```

#### 1.2 更新 lzma_loader.cpp
```cpp
// 尝试加载多线程函数（可选）
bool mtOk = loadFunction(lzma_stream_encoder_mt_ptr, "lzma_stream_encoder_mt");
if (mtOk) {
    loadFunction(lzma_stream_encoder_mt_memusage_ptr, "lzma_stream_encoder_mt_memusage");
    std::cout << "  - Multi-threaded compression: enabled" << std::endl;
} else {
    std::cout << "  - Multi-threaded compression: not available (requires liblzma >= 5.2.0)" << std::endl;
}
```

#### 1.3 更新 compression_module.cpp
```cpp
CompressionResult CompressionModule::compressWithLzmaMultiThreaded(const FolderInfo& folder) {
    if (!lzmaLoader->lzma_stream_encoder_mt_ptr) {
        // 降级到单线程
        return compressWithLzma(folder);
    }
    
    // 配置多线程参数
    lzma_mt mt_options = {
        .flags = 0,
        .threads = std::thread::hardware_concurrency(),
        .block_size = 4 * 1024 * 1024,  // 4MB 块
        .timeout = 0,
        .preset = compressionLevel,
        .check = LZMA_CHECK_SHA256
    };
    
    // 初始化多线程编码器
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret = lzmaLoader->lzma_stream_encoder_mt_ptr(&stream, &mt_options);
    
    // ... 压缩逻辑
}
```

### 阶段 2：实现自定义分块压缩（中优先级）

#### 2.1 创建 BlockCompressor 类
```cpp
class BlockCompressor {
public:
    struct BlockInfo {
        uint64_t offset;
        uint64_t compressedSize;
        uint64_t originalSize;
        uint32_t checksum;
    };
    
    std::vector<uint8_t> compressBlocks(
        const std::vector<uint8_t>& data,
        size_t blockSize,
        CompressionAlgorithm algorithm,
        int threads
    );
    
    std::vector<uint8_t> decompressBlocks(
        const std::vector<uint8_t>& compressedData,
        const std::vector<BlockInfo>& blockIndex,
        int threads
    );
};
```

#### 2.2 并行压缩实现
```cpp
std::vector<uint8_t> BlockCompressor::compressBlocks(...) {
    ThreadPoolManager pool(threads);
    std::vector<std::future<CompressedBlock>> futures;
    
    // 分块并提交到线程池
    for (size_t i = 0; i < blockCount; ++i) {
        futures.push_back(pool.enqueue([=]() {
            return compressSingleBlock(data, i * blockSize, blockSize);
        }));
    }
    
    // 收集结果
    std::vector<CompressedBlock> blocks;
    for (auto& future : futures) {
        blocks.push_back(future.get());
    }
    
    // 合并块和索引
    return mergeBlocks(blocks);
}
```

### 阶段 3：优化解压性能（低优先级）

#### 3.1 块级并行解压
```cpp
bool DecompressionEngine::decompressLzmaBlocks(const DecompressionTask& task) {
    // 解析块索引
    auto blockIndex = parseBlockIndex(task.compressedData);
    
    // 并行解压每个块
    std::vector<std::future<std::vector<uint8_t>>> futures;
    for (const auto& blockInfo : blockIndex) {
        futures.push_back(threadPool->enqueue([=]() {
            return decompressSingleBlock(task.compressedData, blockInfo);
        }));
    }
    
    // 合并解压结果
    std::vector<uint8_t> decompressedData;
    for (auto& future : futures) {
        auto blockData = future.get();
        decompressedData.insert(decompressedData.end(), 
                               blockData.begin(), blockData.end());
    }
    
    return extractTarData(decompressedData, task.targetPath);
}
```

## 性能预期

### 压缩性能对比

| 方案 | 单线程 | 4线程 | 8线程 | 内存使用 |
|------|--------|-------|-------|----------|
| LZMA 单线程 | 50 MB/s | - | - | 50 MB |
| LZMA 多线程 | 50 MB/s | 180 MB/s | 320 MB/s | 400 MB |
| 自定义分块 | 50 MB/s | 160 MB/s | 280 MB/s | 200 MB |
| ZSTD 多线程 | 510 MB/s | 1800 MB/s | 3200 MB/s | 150 MB |

### 解压性能对比

| 方案 | 单线程 | 4线程 | 8线程 |
|------|--------|-------|-------|
| LZMA 单线程 | 200 MB/s | - | - |
| LZMA 分块并行 | 200 MB/s | 600 MB/s | 900 MB/s |
| ZSTD 多线程 | 1550 MB/s | 4000 MB/s | 6000 MB/s |

## 使用建议

### 场景 1：快速安装（推荐 ZSTD）
```cpp
compressor.setCompressionAlgorithm(CompressionAlgorithm::ZSTD_FAST);
compressor.setCompressionLevel(1);
// 自动使用多线程
```

### 场景 2：网络分发（推荐 LZMA 多线程）
```cpp
compressor.setCompressionAlgorithm(CompressionAlgorithm::LZMA_HIGH);
compressor.setCompressionLevel(6);
compressor.enableMultiThreading(true);  // 新增接口
```

### 场景 3：大文件处理（推荐分块）
```cpp
compressor.setCompressionAlgorithm(CompressionAlgorithm::LZMA_HIGH);
compressor.enableBlockCompression(true, 4 * 1024 * 1024);  // 4MB 块
compressor.setThreadCount(8);
```

## 兼容性矩阵

| 功能 | liblzma 5.0 | liblzma 5.2+ | 自定义实现 |
|------|-------------|--------------|------------|
| 单线程压缩 | ✅ | ✅ | ✅ |
| 多线程压缩 | ❌ | ✅ | ✅ |
| 单线程解压 | ✅ | ✅ | ✅ |
| 多线程解压 | ❌ | ❌ | ✅ |
| 随机访问 | ❌ | ⚠️ | ✅ |

## 下一步行动

### 立即实施（本次更新）
- [x] 分析当前实现
- [ ] 添加多线程压缩函数指针
- [ ] 实现版本检测
- [ ] 添加配置接口

### 短期计划（1-2周）
- [ ] 实现 LZMA 多线程压缩
- [ ] 添加性能测试
- [ ] 更新文档

### 长期计划（1-2月）
- [ ] 实现自定义分块压缩
- [ ] 优化内存使用
- [ ] 添加自适应算法选择

---

**文档版本**: 1.0  
**创建日期**: 2026-01-13  
**状态**: 分析完成，待实施
