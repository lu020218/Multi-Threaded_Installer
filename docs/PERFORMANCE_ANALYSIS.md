# 安装性能分析 — 清理旧文件与释放新文件耗时问题

> 日期：2026-05-30（已根据反馈更正问题前提）
> 范围：基于实际读取的源码 `install_cleanup_executor.cpp` / `cleanup_delete_executor.cpp` / `upgrade_cleanup.cpp` / `tar_stream_extractor.cpp`（写盘路径）/ `decompression_engine.cpp` / `installer_concurrency_policy.cpp` / `installer_task_manager.cpp` / `install_finalize.cpp` / `folder_payload_compressor.cpp` / `packager/main.cpp`。

## 〇、问题前提（已二次更正）

- **两次安装都是覆盖 / 升级安装**。
- 唯一变量是**安装包本身**：第一个包是 build 1.0.1，第二个包是 build 1.0.2；两个 build 的 payload 文件**绝大部分相同，仅少量变更**。
- **压缩格式与级别两次完全一致：xz / LZMA2 level 9。**
- 表现是：1.0.2 的**单个文件的删除、释放（写入）速度变慢**（per-op 延迟变大），不是文件数量变多导致的总量变大。
- 只有一部分用户出现。

由此可排除的因素：压缩相同 → 每字节解压 CPU 成本两次一致，"释放新文件慢"**不是解压算法/级别问题**；per-op 延迟变大 → 不是文件数量变多。剩下唯一能在"内容相同、算法相同、机器相同"条件下仍让单文件删/写变慢的，是**文件系统元数据 / 磁盘 I/O / 杀毒软件实时扫描**这一层。能让这一层随 build 改变的，是"哪些文件内容变了"和"新包总字节多大"。

> 注：下文第一、二节（压缩配置、目录映射）在本案前提下**已不成立**（压缩相同、且为 per-op 而非总量问题），保留仅作排查对照。真正的主因见新增的"〇·一"节。

## 〇·一、主因：per-op 延迟变大，指向元数据 / 磁盘 / 杀软层

### A. 变更文件被杀软按"新文件"全量扫描（最契合"单文件写变慢" + "部分用户"）

Windows Defender 等实时防护对**内容发生变化**的文件，在写入句柄关闭时触发完整扫描；内容**未变**的文件通常命中扫描缓存、近乎零成本。

`tar_stream_extractor` 对每个文件都走"rename 旧 → 新建 → 写入 → 关闭 → 删备份"，新建并写入的句柄一关闭即被 AV 拦截扫描。1.0.2 中**真正变更的那部分文件**，每个的写入完成都要等 AV 扫完才返回 → 单文件写延迟显著上升。删除侧同理：`std::filesystem::remove` 经过 AV minifilter，被占用/被扫描时更慢，甚至落入 `kOpenFileRetryCount = 8 × kOpenFileRetryDelayMs = 200ms` 的 rename 重试（`tar_stream_extractor.cpp` 第 24–25、138 行）。

只有开启激进实时防护 / 云同步的机器显著 → 解释"只有一部分用户"。

### B. 1.0.2 总字节更大 → 写入饱和磁盘，与后台删除抢 I/O

即便文件数相近、绝大部分相同，只要 1.0.2 有少量大文件变更/增大，写入吞吐就会压满磁盘（HDD / 慢 SSD 尤甚）。叠加真实代码问题：清理 watchdog 判超时后**无法终止后台删除线程**（`upgrade_cleanup.cpp` 第 1377 行 `cleanupFuture.wait()`），后台删除会与前台解压写入**并发抢同一块磁盘**，导致删与写的单 op 延迟同时上升。此项随"包变大"而恶化，属 build 相关。

### C. 放大器：无 per-file 校验和，无法跳过未变更文件

`FileIndexEntry`（`folder_payload_compressor.cpp` 第 337 行）只有 `relativePath / offset / size`，校验和按整个 folder 算（第 153 / 218 行）。安装器无法识别"该文件与已装版本完全相同"，于是对**每个**文件 rename+重写+删备份、对旧 manifest 中**每个**文件 remove。A、B 的 per-file 成本因此被乘以全量文件数 → "只改一小部分文件"也整体变慢。

### 验证（可证伪）

- `[DECOMP][PayloadWrite] open_handle_failed / rename_failed ... locking_processes=[...]`：占用进程若为 `MsMpEng.exe`(Defender)/`OneDrive.exe`/`SearchIndexer.exe` → 坐实 A。
- `[InstallFlow][Cleanup] slow delete path=... elapsedMs=...`：单删除计时。
- `[InstallFlow][TimingSummary]` 中 `write=` ≫ `decompress=`（压缩相同前提下）→ 瓶颈在写盘/AV 而非解码。
- 把安装目录临时加入 Defender 排除项后重装 1.0.2，若恢复正常 → AV 主因（A），否则偏向 B。

### 修复优先级（据新结论）

1. **【P0】per-file 哈希 + 覆盖安装跳过未变更文件**：让变更文件之外的部分完全不触发写入/AV 扫描/删除。
2. **【P0/P1】写盘改"临时文件 + `ReplaceFileW` 原子替换"**，收敛重试退避（指数 + 总封顶 ~1s），缩短被 AV 抓住的窗口、减少每文件元数据操作。
3. **【P1】watchdog 超时后等待后台删除线程结束再进入解压**，消除 I/O 互抢。
4. **【运维】受影响用户把安装目录加入杀软排除项**（需管理员）。

## 一、最可能：解压慢 → 压缩算法/级别在两次打包间变了

解压速度由 payload 的压缩方式决定，而这是**写进包里**的。`packager/main.cpp` 中：

```cpp
CompressionAlgorithm effectiveAlgorithm = config.package.compression.algorithm; // 第 214 行
int effectiveCompressionLevel = config.package.compression.level;               // 第 248 行
int configuredThreadCount     = config.package.compression.threads;             // 第 259 行
```

即算法 / 级别 / 线程全部来自 `packager.yaml`。两次打包若配置不同（最典型：1.0.1 用 zstd，1.0.2 改 xz/LZMA2 level 9；或级别从低调高），解压成本会差一个量级，即便文件内容几乎一致。

关键反直觉点（见 `decompression_engine.cpp`）：多线程 XZ 解码**很少真正生效**。`BuildMultiThreadedDecoderDecision`（第 26 行）要求 `payloadLargeEnough = originalSize >= 64MB`（第 35 行 `kMinMultiThreadedLzmaPayloadSize`）且 .xz 为多 block 流才能并行；而打包侧 `compressWithXzLzma2` 用 `lzma_stream_encoder_mt`，对多数体量的 folder 只产生**单 block**，单 block 只能串行解码。所以 XZ 路径实际接近单线程解压，算法/级别一变，"释放新文件"立刻变慢。zstd 解压则远快于 xz。

→ 这是最契合"释放新文件非常慢 + 随 build 变化"的解释。

**验证**：对比两次打包日志 `[Packager][Payload] folder=... algorithm=... level=... threads=...`；以及安装日志 `[DECOMP] XZ decoder decision ... original_size=... use_mt=... reason=...`、`[InstallFlow][TimingSummary]` 的 `decompress=` 分项。

## 二、其次：清理慢 → payload 目标映射 / 目录结构在两次打包间变了

清理传给 `runPreviousInstallCleanupWithWatchdog` 的 `replacementTargets`，由 `ResolveSelectedPayloadTargets` 从**新包的** `metadata.extendedPayloadMappings` + `plan.selectedEmbeddedFolders` 计算（`install_cleanup_executor.cpp` 第 23–69、139 行）。也就是说**新包怎么映射目录，直接决定清理走哪条分支**：

- 当某 payload 的 target 规范化后**等于安装根目录**（把文件直接铺到 `%InstallDir%` 根，第 49 行分支），会展开成**逐文件**候选；随后在覆盖到同一根目录时触发 `upgrade_cleanup.cpp` 的"重路径"：
  - `BuildSubdirectoryIsolationCandidates`（第 788 行）在"根替换"下**遍历旧 manifest 全部文件**，每个候选 `AddIsolationCandidate`（第 769 行）做 `exists` + `is_directory` + `IsReparsePointPath` 三次 stat；
  - `IsolateCleanupSubdirectories`（第 831 行）把每个旧子目录 `rename` 成 `.mti_delete_pending_*`，再用一套 async + watchdog 递归删除；
  - **之后又**跑一遍 manifest 驱动的 `PreviousInstall` 删除（第 1437–1444 行）。即同一批文件被遍历/处理两轮。
- 若 target 是子目录（如 `%InstallDir%\app`），走相对轻量的分支。

因此 1.0.2 若相比 1.0.1 改了目录布局或映射 target（哪怕文件内容基本没动），就可能把清理从"轻量"翻转为"隔离所有子目录 + 双重删除"，清理时间暴涨。

**验证**：对比两次 `packager.yaml` 的 `installer.payload` 目标映射；安装日志中是否大量出现 `[InstallFlow][Cleanup] isolated old subdirectory` 与两轮 `delete executor start/end`、`worker timeout`。

## 三、放大器（不是主因，但把上面任一差异乘大）

### 3.1 无 per-file 校验和 → 不能跳过未变更文件
`FileIndexEntry`（`folder_payload_compressor.cpp` 第 337 行）只有 `relativePath / offset / size`，校验和 `calculateChecksum` 是对**整个 folder 的 tar 流**算的（第 153 / 218 行）。覆盖安装对每个文件都 rename + 全量重写 + 删备份，"绝大部分文件相同"得不到任何跳过。任何 build 差异都会被乘以"全量文件数"。

### 3.2 写盘 rename 重试循环（解释"只有部分用户"）
`tar_stream_extractor.cpp`：

```cpp
constexpr int kOpenFileRetryCount = 8;          // 第 24 行
constexpr DWORD kOpenFileRetryDelayMs = 200;    // 第 25 行
bool IsRetriableOpenError(DWORD e){ return e==ERROR_SHARING_VIOLATION||e==ERROR_LOCK_VIOLATION||e==ERROR_ACCESS_DENIED; } // 第 55 行
```

覆盖安装时每个已存在文件先 `RenameExistingFileWithRetry`（第 138 行）再 `OpenOutputHandleWithRetry`（第 196 行），各最多 8 × 200ms。文件被 Windows Defender 实时扫描 / OneDrive / 索引器占用时，单文件最坏 ≈ 3.2s 空转。**只有装了激进杀软/云同步的机器才触发**——这正是"只有一部分用户"的来源；与包无关，但会和第一、二节叠加。`OpenOutputHandleWithRetry` 用 `FILE_SHARE_READ|FILE_SHARE_DELETE`（第 204 行）不含 `FILE_SHARE_WRITE`，进一步增大冲突概率。

### 3.3 watchdog 超时后台线程不可终止
`RunTaskWithWatchdog`（`upgrade_cleanup.cpp` 第 1294 行）超时/卡心跳走 `cleanupFuture.wait()`（第 1377 行，注释明示线程不可安全 kill）。判超时后清理线程仍在后台删，可能与后续解压**并行抢磁盘 I/O**，互相拖慢。

## 四、定位步骤（用日志一锤定音）

让"装 1.0.2 慢"的机器提供 `%LOCALAPPDATA%\MTInstaller\MTInstaller_*.log`，对比 1.0.1 与 1.0.2：

1. 看 `[InstallFlow][TimingSummary]` 的 `cleanup=` 与 `payload write/decompress=` 分项，先确定瓶颈在清理还是解压。
2. 若解压占大头 → 看 `[DECOMP] ... algorithm/original_size/use_mt` 与打包日志 `[Packager][Payload] algorithm/level` → 大概率是压缩配置变了（第一节）。
3. 若清理占大头 → 看是否刷屏 `isolated old subdirectory` / 两轮删除 → 大概率是目录映射变了（第二节）。
4. 看 `rename_failed ... locking_processes=[...]` / `open_handle_failed ... locking_processes=[...]` → 确认是否被 AV/同步占用放大（第 3.2 节，解释为何只有部分用户）。

## 五、修复建议（按性价比）

1. **【P0】统一并锁定两次打包的压缩配置**，避免无意把 zstd 换成 xz 或调高级别；如需高压缩比，至少保证解压侧能并行（payload 分块），否则优先 zstd 以保解压速度。
2. **【P0】增量安装：给每个 `FileIndexEntry` 加 per-file 哈希**，覆盖安装时比对目标 size + 哈希（或退而 size+mtime）相同则跳过 rename/重写——把"绝大部分文件相同"变成"绝大部分文件不动"，对该场景立竿见影。
3. **【P1】稳定目录映射**：保证两次 build 的 `installer.payload` target 布局一致，避免触发"根替换 + 子目录隔离 + 双重删除"的重路径；并合并清理的双轮遍历、减少每文件 3 次 stat。
4. **【P1】写盘改"临时文件 + 原子替换"**（`ReplaceFileW` / `MoveFileExW(REPLACE_EXISTING)`），减少元数据操作并缩短被 AV 抓住的窗口；收敛重试退避（指数退避 + 总封顶约 1s，区分 `ACCESS_DENIED` 与 `SHARING_VIOLATION`）。
5. **【P2】watchdog 超时后等待后台清理线程结束再解压**，避免 I/O 互抢；安装目录可加入 Defender 排除项（需管理员）。

---
*本分析基于实际源码与行号。具体是压缩配置还是目录映射导致 1.0.2 变慢，请以两台/两次对比的 `[InstallFlow][TimingSummary]`、`[DECOMP]`、`[InstallFlow][Cleanup]` 日志与两份 `packager.yaml` 的 diff 为准。*
