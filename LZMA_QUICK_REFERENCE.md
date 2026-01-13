# LZMA 解压快速参考

## 快速开始

### 1. 检查 LZMA 是否可用
```cpp
#include "common/lzma_loader.h"

LzmaLoader loader;
if (loader.isLoaded()) {
    std::cout << "LZMA available!" << std::endl;
}
```

### 2. 解压 LZMA 数据
```cpp
#include "installer/decompression_engine.h"

DecompressionTask task;
task.algorithm = CompressionAlgorithm::LZMA_HIGH;
task.compressedData = /* your data */;
task.targetPath = "/extract/path";
task.expectedChecksum = /* checksum */;
task.originalSize = /* size */;

DecompressionEngine engine;
bool success = engine.decompressFolder(task);
```

### 3. 多线程解压
```cpp
auto threadPool = std::make_shared<ThreadPoolManager>();
engine.setThreadPool(threadPool);
engine.decompressFolder(task);
threadPool->waitForAll();
```

## 函数指针速查

| 函数指针 | 用途 | 必需 |
|---------|------|------|
| `lzma_stream_decoder_ptr` | .xz 格式解码 | ✅ |
| `lzma_auto_decoder_ptr` | 自动格式检测 | ✅ |
| `lzma_alone_decoder_ptr` | .lzma 格式解码 | ✅ |
| `lzma_code_ptr` | 执行编解码 | ✅ |
| `lzma_end_ptr` | 清理资源 | ✅ |
| `lzma_stream_buffer_decode_ptr` | 单次解压 | ⭕ |

## 错误码速查

| 错误码 | 值 | 含义 |
|--------|---|------|
| `LZMA_OK` | 0 | 成功 |
| `LZMA_STREAM_END` | 1 | 流结束 |
| `LZMA_MEM_ERROR` | 5 | 内存不足 |
| `LZMA_FORMAT_ERROR` | 7 | 格式错误 |
| `LZMA_DATA_ERROR` | 9 | 数据损坏 |

## 常见问题

### Q: LZMA 库加载失败？
**A**: 确保 `liblzma.dll` (Windows) 或 `liblzma.so` (Linux) 在系统路径中。

### Q: 解压速度慢？
**A**: LZMA 解压速度约 200 MB/s，比 ZSTD 慢。考虑使用 ZSTD 以获得更快速度。

### Q: 如何选择压缩算法？
**A**: 
- 快速安装 → ZSTD
- 网络分发 → LZMA

### Q: 支持多线程解压吗？
**A**: LZMA 多线程解压支持有限，建议使用文件夹级并行。

## 性能提示

✅ **推荐做法**：
- 使用 `lzma_auto_decoder` 自动检测格式
- 设置合理的内存限制（避免 UINT64_MAX）
- 使用 64KB 块大小进行流式解压
- 启用进度回调提升用户体验

❌ **避免做法**：
- 不要在主线程执行大文件解压
- 不要忽略错误码
- 不要忘记调用 `lzma_end` 清理资源

## 示例代码位置

- 📁 `examples/lzma_decompression_example.cpp` - 完整示例
- 📁 `tests/test_lzma_decompression.cpp` - 测试代码
- 📁 `docs/lzma_decompression_update.md` - 详细文档

## 相关命令

```bash
# 编译项目
cd build && cmake .. && cmake --build . --config Release

# 运行测试
./test_lzma_decompression

# 查看 LZMA 版本
./installer --version  # (如果实现了版本显示)
```

---
**更新**: 2026-01-13 | **版本**: 1.0.0
