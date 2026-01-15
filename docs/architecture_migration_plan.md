# 架构改造计划（分阶段）

本文档定义以下三项改造方向的分阶段方案：
1) 流式管线化
2) 数据布局升级（文件级索引 + 块级索引）
3) 运行时与数据分离
同时包含兼容策略与里程碑交付物。

## 目标

- 保持多线程安装与压缩能力
- 消除安装进度 80%~90% 阶段的明显卡顿
- 降低安装峰值内存占用
- 兼容现有包格式
- 为后续格式与算法演进提供扩展性

## 范围

- Packager 与 Installer 的核心流程
- 包元数据格式
- 安装器运行时的分发方式

## 阶段0：基线与约束确认

### 目标
- 明确当前包格式与安装器模板分发方式
- 确定兼容范围（需支持的历史版本）
- 定义性能指标与允许的体积增量

### 兼容策略
- 确立元数据版本/扩展标记规则
- 确认“新包默认生成 + 旧包可读”是否可行

### 交付物
- 基线格式说明
- 兼容矩阵（旧/新包读写能力）
- 性能目标定义（耗时、内存、CPU、体积增量）

### 退出标准
- 兼容范围达成一致
- 约束与指标确认

### 阶段0结论（已确认）
- 兼容性：不需要支持历史版本，仅保留新格式
- 输出策略：新包格式作为默认输出，不保留旧格式开关
- 包体增量上限：<= 1%
- 性能目标：80%~90% 阶段耗时降低 50% 以上
- 分发方式：保持单文件自解压为默认模式
- 版本策略：允许提升元数据版本号

## 阶段1：流式管线化（不改包格式）

### 目标
- 避免全量解压缓存
- 解包与写盘流式化
- 进度按真实工作量平滑更新

### 设计要点
- 引入流式接口：
  - 解压输出 -> 流式 tar-like 解析
  - 流式增量校验（CRC32）
- 减少大 vector 合并与二次校验遍历

### 兼容策略
- 完全兼容当前包格式
- 不修改元数据结构

### 交付物
- 流式解包模块（解析即写盘）
- 流式校验模块（CRC32 增量更新）
- 更新进度模型（按字节/文件）

### 结构体草图

```cpp
// 流式输出接口：解压器将输出写入 Sink
struct StreamSink {
    virtual ~StreamSink() = default;
    virtual bool write(const uint8_t* data, size_t size) = 0;
    virtual void flush() = 0;
};

// 流式校验：边写边更新 CRC32
class Crc32Stream {
public:
    void update(const uint8_t* data, size_t size);
    uint32_t finalize() const;
};

// tar-like 流式解析器：从解压字节流解析文件并落盘
class TarStreamExtractor : public StreamSink {
public:
    explicit TarStreamExtractor(const std::string& targetRoot);
    bool write(const uint8_t* data, size_t size) override;
    void flush() override;
private:
    // 内部维护解析状态：路径长度、文件大小、路径缓存、文件缓存等
};
```

### API 草图

```cpp
// 解压器提供流式输出
bool DecompressionEngine::decompressToStream(
    const DecompressionTask& task,
    StreamSink& sink,
    Crc32Stream* checksum);
```

### 退出标准
- 80%~90% 卡顿明显缓解
- 峰值内存下降（相对基线）

## 阶段2：数据布局升级（文件级索引 + 块级索引）

### 目标
- 支持按文件随机读取与并行处理
- 避免全量读取与合并

### 设计要点
- 增加 FileIndex：
  - 文件路径、大小、所属块/偏移
- 增加或扩展 BlockIndex：
  - offset、compressedSize、originalSize、checksum
- Installer 优先使用索引；缺失索引则回退旧流程

### 兼容策略
- 通过版本号或扩展标记区分 V1/V2
- Installer 同时支持旧包与新包

### 交付物
- 新元数据结构与序列化/反序列化
- Installer 解析器支持 V1/V2
- Packager 默认生成 V2（可选开关生成 V1）

### 结构体草图

```cpp
// V2 元数据头
struct BinaryMetadataV2 {
    uint32_t magic;        // "MTIP"
    uint32_t version;      // 2
    uint32_t folderCount;
    uint64_t metadataSize;
    uint64_t dataOffset;
    uint64_t fileIndexOffset;
    uint64_t fileIndexSize;
    uint64_t blockIndexOffset;
    uint64_t blockIndexSize;
};

// 文件索引
struct FileIndexEntry {
    uint32_t pathLen;
    std::string path;
    uint64_t originalSize;
    uint32_t blockCount;
    std::vector<uint32_t> blockIds;
};

// 块索引
struct BlockIndexEntry {
    uint32_t blockId;
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t originalSize;
    uint32_t checksum;
};
```

### API 草图

```cpp
// V2 解析器：若缺失索引则回退 V1
bool MetadataParser::parseMetadataV2(MetadataV2& out);

// 按文件读取并解压所需块
bool DecompressionEngine::decompressFileByIndex(
    const FileIndexEntry& fileEntry,
    const std::vector<BlockIndexEntry>& blocks,
    const std::string& targetRoot);
```

### 退出标准
- 大包多文件安装耗时下降
- 旧包仍可正确安装

## 阶段3：运行时与数据分离

### 目标
- 安装器运行时与数据包解耦
- 支持运行时升级而无需重打包数据

### 设计要点
- 输出拆分为：
  - 运行时可执行文件
  - 数据包（payload）
- 运行时加载外部数据包方式：
  - 命令行参数
  - 同目录自动发现

### 兼容策略
- 保留单文件自解压模式作为回退
- 新旧分发模式并行支持

### 交付物
- 新输出模式（runtime + data）
- 外部数据包加载逻辑
- Packager CLI 与文档更新

### 结构体草图

```cpp
// 外部数据包头（独立文件）
struct DataPackageHeader {
    uint32_t magic;       // "MTDP"
    uint32_t version;     // 1
    uint64_t metadataOffset;
    uint64_t metadataSize;
    uint64_t dataOffset;
    uint64_t dataSize;
};
```

### API 草图

```cpp
// 运行时入口：优先读取外部数据包
bool RuntimeLoader::loadPackage(
    const std::optional<std::string>& dataPackagePath,
    PackageContext& out);

// Packager 输出模式
// --standalone: 单文件
// --runtime-data: 分离模式
```

### 退出标准
- 分离模式可独立安装
- 单文件模式仍保持可用

## 阶段4：验收与迁移

### 目标
- 验证性能与兼容性
- 完成文档与示例更新

### 交付物
- 基准测试报告（前后对比）
- 迁移指南与示例
- 回退策略与兼容说明

### 退出标准
- 性能指标达成
- 兼容范围全部通过

## 里程碑汇总

1. M0：基线与约束确认完成
2. M1：流式管线化完成，卡顿显著缓解
3. M2：文件索引/块索引上线，V1/V2 兼容验证通过
4. M3：运行时/数据分离模式上线，双模式分发支持
5. M4：验收完成，迁移文档发布
