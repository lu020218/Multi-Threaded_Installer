# 增量安装方案 — 跳过未变更文件

> 日期：2026-05-30
> 目标：覆盖/升级安装时，对内容未变更的文件跳过"删除旧文件 + 写入新文件"，从而消除其写入 I/O 与杀软扫描成本。
> 适用场景：两个 build（如 1.0.1→1.0.2）绝大部分文件相同、仅少量变更，但当前每次覆盖安装都全量重写。

---

## 一、当前处理流程（现状）

### 1.1 打包侧（packager）

`FolderPayloadCompressor::createTarData`（`folder_payload_compressor.cpp`）把一个 folder 下所有文件**顺序拼接**成一个自定义 tar 流，每个文件的条目格式为：

```
[uint32 pathLength][uint32 fileSize][path 字节][file 内容字节]
```

随后整段 tar 流用 xz/LZMA2（或 zstd）压成**单个压缩流**，并对整段 tar 算一个 **folder 级 CRC32 校验和**。

每个文件同时生成一条 `FileIndexEntry`：

```
FileIndexEntry { relativePath; offset; size; }
```

注意：`FileIndexEntry` 当前**只有路径、偏移、大小，没有 per-file 校验和**；CRC32 是对整个 folder 算的，不是每文件。

### 1.2 元数据侧

打包结果通过 `PackageManifestBuilder` 写入 `extendedPayloadMappings`，每个 mapping 描述一个 folder 的：`folderId / folderName / target / offset / compressedSize / originalSize / checksum(folder 级) / algorithm / fileIndex[]`。这些随安装包嵌入，安装时由 `MetadataParser` 读出。

### 1.3 安装侧（installer）

覆盖安装时：

1. **清理阶段** `ExecuteInstallCleanup` → `runPreviousInstallCleanupWithWatchdog`：读**旧安装的 manifest**（`install.manifest.json` 里的 `files[]`），把其中每个文件 `std::filesystem::remove` 删除（并处理空目录、子目录隔离等）。**删除是按旧文件清单全量进行的，不判断新包里这个文件是否其实没变。**

2. **解压/写入阶段** `RunParallelInstall` → `FolderInstallExecutor` → `DecompressionEngine` → `TarStreamExtractor`：
   - `FolderPayloadReader::readPayload` 把该 folder 的整段压缩数据读入内存；
   - `DecompressionEngine` 流式解压（64KB 缓冲）；
   - `TarStreamExtractor::write` 按 tar 流顺序解析出每个文件，对**每一个**文件执行：`openCurrentFile`（已存在则 rename 旧→`.__mti_old`，再 `CreateFile` 新建）→ 写入全部字节 → 关闭句柄（触发 AV 扫描）→ `finalizeCurrentFileSuccess`（删除备份）。

### 1.4 现状的性能问题

- 解压是**整流串行**的：即使只有少数文件变了，也必须把整个 folder 的 xz 流从头解到尾（xz 无 per-file 随机访问）。
- 写入是**全量**的：每个文件都 rename+重写+删备份,且关闭句柄触发杀软对"新文件"的全量扫描。
- 删除是**全量**的：旧 manifest 里每个文件都被删,哪怕新包里它一字节没变（删了又原样写回）。
- 没有任何"内容相同则跳过"的判据(`FileIndexEntry` 无 per-file 哈希)。

→ 结果：1.0.2 覆盖安装把"绝大部分未变更文件"也走了一遍"删除→解压→重写→AV 扫描",这是单文件删/写变慢、整体耗时高的根本。

---

## 二、新方案总体思路

核心两点：

1. **打包时为每个文件预先算好"内容指纹",写进 manifest**（per-file 哈希 + size，必要时 mtime）。这样安装时无需解压即可判断某文件是否与已装版本相同。

2. **安装时按文件做差异决策**：
   - 已装文件存在且指纹与新包一致 → **跳过**：既不删除、也不解压该段、也不写盘；
   - 不一致或不存在 → 正常解压并写入；
   - 旧 manifest 有、新包没有 → 删除（这是真正该删的"废弃文件"）。

关键约束:**比较判据必须来自打包时写入的 manifest,绝不能在安装时为了比较去解压整个 payload**——否则省不掉解压成本,方案失去意义。

---

## 三、打包侧改动

### 3.1 为 `FileIndexEntry` 增加 per-file 指纹

在每个文件条目中新增字段：

- `contentHash`：对**文件原始内容**算的快哈希（推荐 xxHash64，非加密、极快；CRC32 亦可但碰撞率略高）。
- `size`：已有，保留（作为哈希前的快速排除）。
- （可选）`mtime`：源文件修改时间,用于"最省判据"或日志。

`createTarData` 在读每个文件内容写入 tar 流时,顺带对该文件内容算 `contentHash`(读取已经发生,哈希几乎不增加 I/O,只增加少量 CPU)。

### 3.2 指纹定义要稳定且确定

- 哈希只覆盖**文件内容**,不含路径/时间,保证"内容相同→哈希相同"。
- 相对路径规范化方式(大小写、分隔符)需与安装侧比较时一致,避免同一文件被误判为不同。
- 写进 `extendedPayloadMappings[].fileIndex[]`,经 `PackageManifestBuilder` → manifest codec 序列化进包。需在 manifest schema 里加版本位(见 5.3 兼容性)。

### 3.3 打包成本评估

哈希在"本来就要读该文件内容"的循环里顺带完成,只增加 CPU(xxHash 约数 GB/s),对打包总时长影响可忽略。

---

## 四、安装侧改动

### 4.1 在解压前构建"已装文件指纹索引"

覆盖安装开始时,需要一个"目标位置上的文件是否与新包相同"的判断来源。**已装版本能提供多少信息,分三档,跳过/清理策略要逐档退化**(详见第五节)。基础实现两种:

- **方案 A(最省):复用旧 manifest 里的哈希。** 本方案上线后,安装写出的 manifest 自带 per-file 哈希。覆盖安装时直接"旧 manifest 哈希 vs 新包哈希",**完全不读磁盘内容**。
- **方案 B(回退):读已装文件算哈希。** 旧 manifest 无哈希、或根本没有 manifest 时,对**目标路径上已存在的文件**读一遍内容算哈希再比。这是"读 I/O",但换掉的是"写 I/O + AV 扫描",净收益仍为正(读通常快于写,且不触发写入扫描)。

关键认知:**跳过判据本质只依赖"目标路径上现在的文件内容",不依赖旧 manifest。** 所以即使没有 manifest,跳过功能依然可用(走方案 B);旧 manifest 只是把"读盘算哈希"优化成"零读盘比较"的加速器,不是前提。

实务上:**优先 size 快筛**(size 不同必定不同,直接判定需写,跳过读取/哈希);size 相同再比哈希。

### 4.2 解压阶段按文件跳过

改造 `TarStreamExtractor`(或其上层 `FolderInstallExecutor`)的逐文件处理:解析出 `[pathLength][fileSize][path]` 头之后、进入写文件之前,做差异决策:

- **命中跳过条件**(目标已存在 且 size 一致 且 指纹一致):
  - **不** rename、**不**新建、**不**写盘、**不**触发 AV;
  - 但仍要从压缩流中**消费掉**这 `fileSize` 字节(因为 xz 是连续流,必须解出来才能定位到下一个文件头)——即"解压但丢弃,不落盘"。
  - 把该文件登记进"已安装文件清单"(供新 manifest 与卸载使用),状态记为 skipped。
- **未命中**:走现有逻辑,正常 rename+写入。

> 重要:xz 单流无法 seek 跳过压缩字节,所以"跳过"省掉的是**写盘 + AV 扫描 + rename/删备份的元数据操作**,解压 CPU 仍会发生(把该段解出来丢弃)。在本案中瓶颈是写入/AV 而非解压 CPU,所以这层收益已经很大。若要连解压也省,需改包格式(见 6.2 进阶)。

### 4.3 清理阶段按差异删除

当前清理是"旧 manifest 全删"。新方案在**旧 manifest 可用时**改为**按新旧清单求差集**:

- `旧有 ∩ 新有` 且内容相同 → 文件保留(既不删也不写)。
- `旧有 ∩ 新有` 且内容不同 → 由解压阶段覆盖写入(清理阶段不必单独删,写入阶段的 rename 会处理)。
- `旧有 − 新有`(新包已移除的文件) → **删除**。这才是清理阶段真正要做的。
- `新有 − 旧有`(新增文件) → 解压阶段正常写入。

这样清理从"删除全部旧文件"缩减为"只删新包里已不存在的文件",删除量大幅下降,直接缓解"单个删除慢 × 全量"的问题。

> 注意:**差集清理依赖"旧文件全集",而全集只有 manifest 才有。** 完全没有 manifest 的更老版本无法做差集(不能靠遍历安装目录来推断"哪些是上一版装的",会误删用户数据)。该情形的退化策略见第五节。

### 4.4 manifest 与卸载的正确性

跳过的文件**仍属于本次安装**,必须照常写进新的 `install.manifest.json` 的 `files[]`(否则卸载时漏删)。`install_finalize` 里 `CollectFilesRecursive` / `installedFiles` 的汇总逻辑要把 skipped 文件一并纳入。

---

## 五、决策判据与兼容性

### 5.1 三种判据取舍

- **size + mtime**:最省,零内容读取;风险是 mtime 易受外部改动(误判为"变了"→退化成重写,不会装错)。适合做快筛或保守起步。
- **size + 内容哈希**:最稳,内容相同必判同;需要哈希来源(打包写入或安装读取)。推荐为最终判据。
- **组合**:先 size 快排,size 相同再比哈希;绝大多数文件走最便宜分支。

### 5.2 安全红线

- 哈希用于"是否相同"判定,碰撞会导致"内容变了却被当作没变→漏更新"。xxHash64 碰撞率对安装场景足够,若极度保守可用 SHA-256(慢但 `install_execution` 已有 BCrypt SHA256 实现可复用)。
- 任何"无法确定相同"的情况一律按"需要写入"处理(fail-safe 偏向重写,不偏向跳过)。

### 5.3 已装版本状态分级与退化路径(重要)

覆盖安装时,已装版本能提供的信息分三档,**跳过判据**与**废弃文件清理**要分别逐档退化:

| 已装版本状态 | 旧文件全集 | 旧哈希 | 跳过判据 | 废弃文件清理 |
|---|---|---|---|---|
| 新版(本方案上线后所装) | 有(manifest.files) | 有(manifest per-file hash) | 最省:旧哈希 vs 新哈希,**零读盘** | 差集删除(旧有−新有) |
| 有 manifest、无哈希 | 有(manifest.files) | 无 | 方案 B:读目标文件算哈希 | 差集删除(旧有−新有) |
| **无 manifest(更老版本)** | **无** | 无 | 方案 B:目标路径存在则读盘算哈希 | **不能做差集**;仅按包内 `installerCleanup` 规则删已知废弃项,或跳过废弃清理,**绝不全目录删** |

要点:

1. **跳过功能在三档下都可用**——它只依赖"目标路径当前文件",不依赖旧 manifest。无 manifest 时仍能跳过未变更文件(走读盘算哈希),写盘/AV 成本照样省下。
2. **差集清理需要旧文件全集**——无 manifest 时拿不到,**不可**用"遍历安装目录"代替(目录里可能有用户数据/日志/配置/他程序文件,误删后果严重;现有 `RemoveDirectoryContentsBestEffort` 的 `IsProtectedFullCleanupRoot` 等保护正是为此)。无 manifest 时废弃文件清理退化为"仅按包内预置规则",残留少量旧版孤儿文件通常无害,优先保证不误删。
3. **过渡是一次性的**:本次安装结束会写出带哈希的新 manifest。所以"无 manifest/无哈希"只在**首次升级到新版安装器**时各发生一次,之后机器上即有完整带哈希 manifest,后续升级自动进入最省路径。

### 5.4 安装侧"已装指纹来源"判定顺序

1. 旧 manifest 存在且带哈希 → manifest 哈希(零读盘)+ manifest.files 差集清理。
2. 旧 manifest 存在但无哈希 → manifest.files 差集清理;跳过判据读盘算哈希。
3. 无 manifest → 跳过判据"目标存在则读盘算哈希";废弃清理仅按包内规则,不做全目录差集。
4. 任何一步无法确定"相同" → fail-safe 按"需要写入"处理,不误跳过。

### 5.5 向后兼容

- manifest 增加 `schemaVersion` / per-file `hash` 字段;读取侧对缺字段、缺整份 manifest 都要兜底(不抛错,按 5.3/5.4 退化)。
- 新增字段不破坏旧安装器读取(旧安装器忽略未知字段即可)。

---

## 六、预期收益与进阶

### 6.1 本方案(不改包格式)的收益

- **省掉未变更文件的:删除、rename、新建、写盘、AV 写入扫描、删备份。** 这些正是当前 per-op 变慢的来源。
- 删除量从"旧全量"降为"差集(已移除文件)"。
- 解压 CPU 仍全量发生(解出后丢弃未变更段),但本案瓶颈不在 CPU。
- 净效果:1.0.2 覆盖安装应从"全量删写"降为"仅处理少量变更文件",耗时大幅下降,且 AV 扫描面收敛到变更文件。

### 6.2 进阶(若要连解压也省,需改包格式)

当前 folder = 单 xz 流,无法跳过压缩字节。若想让"未变更文件连解压都省":

- 改为**按文件或按块分别压缩**(每文件/每块独立 xz/zstd 帧 + 在 manifest 记录每段的压缩偏移与长度),即可对未变更段直接 seek 跳过、完全不解压。
- 代价:压缩率略降(分帧损失)、打包与格式复杂度上升。
- 建议:先上 6.1(收益大、改动小、瓶颈匹配),确有 CPU 瓶颈再评估 6.2。

---

## 七、改动清单(落地时涉及的位置)

打包侧:
- `FileIndexEntry` 结构增加 `contentHash`(+可选 `mtime`)。
- `FolderPayloadCompressor::createTarData`:读文件内容时顺带算哈希,填入条目。
- `PackageManifestBuilder` + manifest codec:序列化新字段 + schema 版本位。

安装侧:
- `MetadataParser` / manifest 读取:解析 per-file 哈希;读旧 manifest 时构建"已装指纹索引"。
- 清理(`install_cleanup_executor` / `upgrade_cleanup`):由"全量删旧"改为"删差集(旧有−新有)"。
- 解压(`TarStreamExtractor` 或 `FolderInstallExecutor`):逐文件做差异决策,命中则解出丢弃、不落盘,并登记为已安装。
- 收尾(`install_finalize`):skipped 文件纳入新 manifest 的 `files[]`,保证卸载完整。

兼容/回退(按第五节三档):
- 旧 manifest 带哈希 → 零读盘比较 + 差集清理(最省)。
- 旧 manifest 无哈希 → 读盘算哈希 + 差集清理。
- 无 manifest(更老版本) → 读盘算哈希仍可跳过;废弃清理仅按包内规则,不做全目录差集,不误删。
- 一律 fail-safe:无法确定相同就写入,不报错。

---

## 八、优先级建议

1. **P0**:打包侧写 per-file 哈希 + 安装侧"解压阶段命中跳过(解出丢弃、不写盘)" + 清理改差集删除。这是收益主体,且不改包格式。
2. **P1**:写盘改"临时文件 + `ReplaceFileW` 原子替换"、收敛 rename 重试退避——与本方案叠加,进一步压缩变更文件那部分的 AV/元数据成本。
3. **P2(可选)**:改包格式为分帧压缩,实现"未变更文件连解压都省"。

---

## 九、落地实施记录（实现状态）

> 状态：**P0 + P1 + 方案A + P2 均已实现**，65 个回归用例通过，并完成两轮真实打包/安装/升级/卸载端到端验证。

### 9.1 打包侧
- `FileIndexEntry` 增加 `contentHash`（FNV-1a 64，见 `include/common/content_hash.h`，打包/安装两端共用同一算法保证确定性），以及分帧字段 `frameOffset` / `frameCompressedSize`。
- `FolderPayloadCompressor::createTarData` 在读文件内容时顺带算 `contentHash`。
- 新增按文件分帧路径 `FolderPayloadCompressor::compressFolderFramed`：每个文件独立压成一帧（XZ/ZSTD），folder 级 checksum 置 0，改由 per-file 哈希保证完整性。
- 配置开关 `package.compression.perFileFrames`（默认 false）。
- codec（`package_manifest_codec.cpp`）序列化 `contentHash` / `framed` / 分帧字段；新字段对旧包缺省为 0/false，**向后兼容**（未 bump `Constants::VERSION`）。

### 9.2 安装侧 — 跳过判据（三档，已全部实现）
- **方案 A（零读盘）**：上一版 `install.manifest.json` 写入了 `fileFingerprints[]`（path/size/contentHash）。覆盖/升级安装时，**在规划阶段（清理前，旧 manifest 尚存）** 由 `BuildInstallExecutionPlan` 读入并存进 `InstallExecutionPlan.previousInstalledFingerprints`，再透传到解压层；命中即"旧哈希 vs 新哈希"零读盘比较：相等→跳过，不等→重写，**都不读文件内容**。
  - ⚠ 时序要点：清理阶段会删除旧 `install.manifest.json`，因此指纹必须在规划阶段捕获，不能等到解压时再读（否则已被删）。
- **方案 B（回退）**：无旧 manifest 哈希时，读目标文件算哈希再比。
- 判据核心抽成共享自由函数 `ExistingInstalledFileMatches(...)`，流式路径与分帧路径共用；`contentHash==0` 一律按"需要写入"处理（fail-safe）。

### 9.3 安装侧 — 解压/写入
- 单流 folder：`TarStreamExtractor` 逐文件命中跳过（解出丢弃、不写盘），跳过文件仍登记进 `installedFiles_`（保证卸载完整）。
- 分帧 folder：`FolderInstallExecutor::InstallFramedFolder` 逐文件决策——命中跳过则**既不读帧也不解压**；否则只 `readPayload(folder.offset+frameOffset, frameCompressedSize)` 读该帧并解单帧、校验 per-file 哈希，再复用写盘路径落盘。
- **P1 原子写**：内容先写同目录暂存文件 `*.__mti_new`，再 `ReplaceFileW` 原子替换（目标不存在则 `MoveFileEx`）；`ReplaceFileW` 失败（如 `ERROR_UNABLE_TO_REMOVE_REPLACED` 1175）时回退到 `MoveFileEx(REPLACE_EXISTING)`，锁定的敏感二进制走 reboot 替换；rename/open/replace 重试改为指数退避（25→400ms 封顶）。

### 9.4 清理（差集删除）
- `runPreviousInstallCleanupWithWatchdog` 增加 `keepFiles`（新包文件全集）：非空时**只删"旧有−新有"**，并**关闭整目录隔离**（避免把未变更文件搬走抵消跳过）。`keepFiles` 为空时行为与旧版完全一致。
- 无 manifest 的更老版本：拿不到旧文件全集，退化为"仅按包内规则清理、不做全目录差集、不误删"。

### 9.5 收尾（manifest 完整性）
- `install_finalize` 由包内 `fileIndex` 解析出绝对路径，写入 `install.manifest.json` 的 `fileFingerprints[]`（供下次升级走方案 A）；跳过的文件照常计入 `files[]`，卸载时不漏删。

### 9.6 主要改动文件
- 打包：`folder_payload_compressor.{h,cpp}`、`compression_module.{h,cpp}`、`package_manifest_builder.cpp`、`configuration_loader.cpp`、`config_types.h`、`packager/main.cpp`。
- 公共：`archive_types.h`、`package_manifest.{h,cpp}`、`package_manifest_codec.cpp`、`content_hash.h`、`installer_parallel_install.{h,cpp}`。
- 安装：`tar_stream_extractor.{h,cpp}`、`folder_install_executor.{h,cpp}`、`decompression_engine.{h,cpp}`、`install_plan_builder.{h,cpp}`、`install_execution.cpp`、`install_cleanup_executor.cpp`、`upgrade_cleanup.{h,cpp}`、`install_finalize.cpp`、`install_manifest_store.{h,cpp}`。

### 9.7 兼容性小结
- 旧安装器读新包：忽略未知字段即可（codec 容缺省）。
- 新安装器装旧包：`contentHash`/`framed` 缺省为 0/false → 退化为方案 B / 单流路径，功能正常。
- `perFileFrames` 为 opt-in，默认关闭，不影响既有打包产物。
