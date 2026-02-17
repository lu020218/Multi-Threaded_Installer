# ZSTD 支持 MVP 清单

## 目标

在不破坏现有 LZMA 行为的前提下，完成以下能力：

1. `packager.yaml/json` 可配置压缩算法：`zstd` 或 `lzma`
2. 可配置压缩级别：`compressionLevel`
3. 打包与安装端都支持按算法正确压缩/解压
4. 旧配置保持兼容（默认仍可运行）

---

## 范围（MVP）

包含：

- 配置解析（YAML/JSON）
- 压缩端 ZSTD 接入（packager）
- 解压端 ZSTD 接入（installer）
- 基础测试与端到端验证
- README 与配置文档最小更新

不包含（后续迭代）：

- ZSTD 高级调优参数（windowLog、strategy、long mode 等）
- 自动算法选择策略（按文件大小自动选 zstd/lzma）
- 复杂性能压测平台化

---

## 执行清单

## 阶段 1：数据模型与配置解析

- [ ] 扩展压缩算法枚举
  - 文件：`include/common/types.h`
  - 任务：在 `CompressionAlgorithm` 中新增 `ZSTD`
  - 验收：编译通过；不影响现有 `LZMA_HIGH` 路径

- [ ] 增加配置字段 `compressionLevel`
  - 文件：`include/common/types.h`
  - 任务：在 `PackagerConfiguration` 中新增 `int compressionLevel`
  - 默认：`-1`（表示未显式设置，运行时按算法取默认）
  - 验收：无配置时行为与当前一致

- [ ] 扩展配置解析：支持 `zstd`
  - 文件：`src/packager/configuration_loader.cpp`
  - 任务：
    - `compressionAlgorithm` 支持 `lzma` / `zstd`
    - 解析 `compressionLevel`（整数）
    - 非法值报错（算法未知、级别非整数）
  - 验收：YAML/JSON 均可加载；错误信息清晰

- [ ] 配置校验规则补充
  - 文件：`src/packager/configuration_validator.cpp`
  - 任务：
    - LZMA 级别范围：`0-9`
    - ZSTD 级别范围：`1-22`
    - `-1` 允许（表示默认）
  - 验收：越界时报错，合法值通过

---

## 阶段 2：Packager 侧 ZSTD 压缩

- [ ] CMake 接入 ZSTD 依赖
  - 文件：`CMakeLists.txt`
  - 任务：
    - 增加 `ENABLE_ZSTD` 选项
    - 增加 zstd 头文件/库查找与链接
  - 验收：开启 zstd 时可编译；关闭时 LZMA 路径正常

- [ ] 压缩模块新增 ZSTD 实现
  - 文件：`include/packager/compression_module.h`
  - 文件：`src/packager/compression_module.cpp`
  - 任务：
    - 新增 `compressWithZstd(...)`
    - `compressFolder(...)` 按算法路由
    - `setCompressionAlgorithm(...)` 正确接受 `ZSTD`
  - 验收：可生成带 ZSTD 算法标记的安装包数据

- [ ] 压缩级别默认策略
  - 文件：`src/packager/compression_module.cpp`
  - 任务：
    - 未设置级别时：LZMA 默认 `9`，ZSTD 默认 `3`
    - 显式设置时：使用用户值
  - 验收：日志中可看到生效级别

---

## 阶段 3：Installer 侧 ZSTD 解压

- [ ] 解压路由支持 ZSTD
  - 文件：`src/installer/decompression_engine.cpp`
  - 任务：`decompressToStream(...)` 中新增 `ZSTD` 分支
  - 验收：不再对 ZSTD 直接报 unsupported

- [ ] 新增 `decompressZstd(...)`
  - 文件：`include/installer/decompression_engine.h`（如需要）
  - 文件：`src/installer/decompression_engine.cpp`
  - 任务：
    - 使用 zstd API 解压到流
    - 保持 checksum、进度回调与错误处理一致
  - 验收：ZSTD 安装包可成功安装并通过校验

---

## 阶段 4：CLI 与文档同步

- [ ] CLI 帮助文本更新
  - 文件：`src/installer/console_interface.cpp`
  - 任务：
    - `packager --algorithm` 帮助改为 `<lzma|zstd>`
    - 示例新增 zstd
  - 验收：`--help` 输出与实现一致

- [ ] README 与配置文档更新
  - 文件：`README.md`
  - 文件：`docs/configuration_reference.md`
  - 任务：
    - 增加 `compressionAlgorithm`、`compressionLevel` 示例
    - 声明级别范围与默认值
  - 验收：文档不再与代码冲突

---

## 阶段 5：测试与验收

- [ ] 配置解析测试
  - 新增/扩展测试：YAML/JSON 下 `lzma/zstd + compressionLevel` 合法/非法用例
  - 验收：错误用例能稳定失败并给出明确原因

- [ ] 压缩解压回环测试
  - 任务：
    - LZMA 回归测试（原有）
    - ZSTD 新增回环测试（同输入输出一致）
  - 验收：输出文件内容与 checksum 一致

- [ ] 端到端测试
  - 流程：
    - `packager (zstd)` -> 产物安装成功
    - `packager (lzma)` -> 产物安装成功
  - 验收：两条链路都通过

---

## MVP 交付定义（DoD）

满足以下条件即视为 MVP 完成：

- 可通过 YAML/JSON 选择 `zstd|lzma`
- 可配置 `compressionLevel` 并生效
- `packager` 可生成 ZSTD 包
- `installer` 可解压并安装 ZSTD 包
- 旧 LZMA 包无行为回归
- 关键测试通过，文档已同步

---

## 风险与应对

- 风险：ZSTD 库接入在 Windows 静态链接下出现符号/运行时冲突
  - 应对：先采用与 liblzma 一致的链接策略；必要时引入 `ENABLE_ZSTD=OFF` 兜底

- 风险：元数据算法字段兼容性问题导致旧包无法安装
  - 应对：保持默认算法不变；解析端对旧值走 LZMA 分支

- 风险：压缩级别越界导致运行时崩溃
  - 应对：在配置校验阶段阻断，并在运行时再做防御性检查

---

## 推荐实施顺序（最小路径）

1. 阶段 1（模型 + 解析 + 校验）
2. 阶段 2（packager ZSTD）
3. 阶段 3（installer ZSTD）
4. 阶段 5（先做回环与端到端）
5. 阶段 4（文档与帮助收尾）
