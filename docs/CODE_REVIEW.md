# 代码评审报告 — Multi-Threaded Installer

> 范围：`src/` 下自有代码（common / packager / installer / gui），不含 `third_party/`、`build*/`。
> 日期：2026-05-30
> 说明：本报告基于实际读取的源文件。覆盖了打包、安装主流程、解压、并行、注册表、日志、自删除、组件执行等关键路径，未逐行通读全部 86 个源文件。

## 一、整体结论

代码采用 C++17 + DuiLib + yaml-cpp + zstd/LZMA，目标是 Windows 平台自包含 exe 安装/打包框架。这是一个**完成度和工程质量都相当高**的项目，不是脚手架。整体走"函数式流水线 + Input/Result 结构体"风格：每个环节一个小文件、一对输入/输出结构，串成清晰调用链。命名统一（文件 snake_case、函数 PascalCase、统一 `MultiThreadedInstaller` 命名空间），内部 UTF-8、Win32 边界转宽字符，处理了大量真实世界的边角情况（文件被占用、长路径、重启替换、崩溃转储、日志轮转）。

主要改进点集中在"单文件过大/职责过载""失败回滚不完整""个别疑似复制粘贴 bug"，而非架构性缺陷或安全漏洞。

## 二、架构设计

### 安装调用链（实际实现）

```
installation_worker (后台线程, PostMessage 通知 GUI)
  └─ ExecuteInstallService
       ├─ BuildInstallExecutionPlan     组装计划(路径决策/组件依赖拓扑排序/注册表)
       ├─ ExecuteInstallPrecheck        磁盘空间/OS版本/进程占用/互斥锁
       ├─ ExecuteInstallCleanup         清理旧安装
       ├─ ExecuteInstallExecution
       │     ├─ RunParallelInstall      ThreadPoolManager 按文件夹并行
       │     │     └─ FolderInstallExecutor → FolderPayloadReader → DecompressionEngine → TarStreamExtractor
       │     └─ 组件安装(本地/下载, WinHTTP+SHA256 校验, Job Object 进程树管理)
       └─ ExecuteInstallFinalization    注册表/快捷方式/自启动 + 失败时回滚 install state
```

### 优点

- **流水线清晰、职责分层好**：plan / precheck / cleanup / execute / finalize 各司其职；payload 读取、解压引擎、tar 落盘三层解耦，便于测试。
- **GUI 线程模型正确**：`InstallationWorker` 在独立 `std::thread` 跑安装，通过 `PostMessage`(`WM_INSTALLATION_PROGRESS`/`WM_INSTALLATION_COMPLETE`) 把进度/完成投递回 GUI 线程，且对无效窗口、PostMessage 失败均有兜底删除 payload，杜绝跨线程操作控件与内存泄漏。
- **名副其实的多线程**：安装侧 `RunParallelInstall` 用 `ThreadPoolManager` 按文件夹并行解压，并按硬件并发与负载推导 worker 数与解码器线程预算；XZ 大 payload 还会启用多线程解码器；打包侧 `main.cpp` 用原子游标 + 首错置位的 worker 池并行压缩。
- **编码/平台细节规范**：现代 `SHGetKnownFolderPath` 思路、注册表全宽字符 API 且检查返回值并处理 WOW6432 视图、`ExpandEnvironmentStringsW`、长路径 `\\?\` 前缀、tar 路径用 `u8path`。
- **健壮性细节成熟**：payload 读取做了 offset/size 越界、最大尺寸上限、`size_t`/`streamsize` 可表示性、`bad_alloc` 多重校验；解压设原始大小推导的内存上限；写文件遇占用用 Restart Manager 查锁定进程并重试，锁定的 .exe/.dll 等走"重启后替换"；崩溃时写 minidump + crash log；安装日志按数量/天数轮转。

### 安全：路径穿越已防护（更正先前误判）

`tar_stream_extractor.cpp` 的 `validateCurrentPath()` 在落盘前对每个相对路径做了校验：拒绝空路径、绝对路径、Windows 盘符（`X:`）、`lexically_normal` 后含 `..` 的分量。Zip-Slip 防护到位。

## 三、主要问题（建议优先处理）

### 1. `install_execution.cpp` 单文件过大、职责过载
约 1300 行，把 WinHTTP 流式下载、`StreamingSha256`(BCrypt)、进程执行与超时/取消/心跳、Job Object 进程树、本地/下载组件编排、整体进度聚合全塞在一个文件。建议拆分为 `component_downloader`、`component_executor`、`install_execution` 编排三层，降低维护成本与改动风险。

### 2. 疑似复制粘贴 bug（下载组件的卸载命令）
`ExecuteDownloadComponent`（约第 1026、1031 行）在登记卸载命令时读取的是 `component.source.local.uninstall`，而当前分支处理的是 `download` 源。看起来应为 `component.source.download.uninstall`。会导致下载型组件的卸载命令登记错误或缺失，建议核对。

### 3. 失败回滚不完整
- 跨文件夹并行安装中，某个文件夹失败会使整体失败，但已成功落盘的其他文件夹不回滚，留下半成品安装。
- 单文件替换：`finalizeCurrentFileFailure` 会删掉新写入的文件，但被重命名的旧文件（`.__mti_old` 备份）不自动还原，仅记 `backup_left_behind` 警告——极端情况下可能丢失原文件。建议失败路径补"恢复备份"。

## 四、其他建议

- **包含路径风格不统一**：`gui/installation_worker.cpp` 用 `../../include/gui/...` 相对包含，与项目其余 `"installer/..."`/`"common/..."` 的统一风格不一致，建议改齐。
- **压缩数据整块入内存**：解压输出是 64KB 缓冲流式写盘，但"压缩数据本身"由 `FolderPayloadReader::readPayload` 整块读入内存（有上限保护）。超大 payload 仍会占用较多内存，可考虑对输入也做流式喂入解码器。
- **`installation_worker.cpp` 残留 `<codecvt>`**：已弃用头文件，且文件内 `NOTE: Comment text normalized to avoid encoding mojibake` 占位注释较多、末尾大量空行，建议清理。
- **打包 tar 格式限制**：自定义 payload 流用 `uint32_t` 存单文件大小，单文件上限 4GB，已有显式校验并报错；如需支持超大单文件需升级格式。
- **工程卫生**：`build/`、`build_codex/`、`cmake-build-debug/`、`test-work/` 等构建产物入库，`resources/images/*` 多处重复；存在 `page_controller.cpp.bak`。建议完善 `.gitignore` 并移除备份文件。

## 五、值得肯定

模块边界清晰、Input/Result 流水线一致、GUI 线程模型正确（PostMessage + payload 生命周期管理）、真正的并行打包/安装、组件下载有 HTTPS 强制 + SHA256 校验、进程树用 Job Object 管理超时与取消、UTF-8↔宽字符边界规范、宽字符注册表 API 且处理 WOW6432、文件占用走 Restart Manager + 重启替换、崩溃 minidump、日志轮转、安装路径穿越防护到位。这是一个细节扎实、考虑了大量真实部署场景的代码库。

---
*修订记录：本文件第一版基于错误的文件结构推断（引用了不存在的 `installer_app.cpp`、`compression.cpp` 等），第二版仍残留 Zip-Slip、占位骨架、并行未接入等误判——原因是当时文件读取与沙箱启动失败、文档在真实源码读取完成前就被写出。本版（第三版）是在完整读取真实源码后重写的，已推翻并更正上述所有错误结论。*
