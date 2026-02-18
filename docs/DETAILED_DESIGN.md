# 方案详细设计文档

## 1. 总体架构
系统由 4 层组成：
- 入口层：`src/packager/main.cpp`、`src/installer/main.cpp`
- 业务层：扫描、压缩、元数据生成、安装服务、卸载管理
- 配置层：配置加载（JSON/YAML）、配置校验、配置管理
- 基础层：路径/编码转换、日志、线程池、文件系统操作

主要模块目录：
- `src/packager/`
- `src/installer/`
- `src/common/`
- `src/gui/`（`BUILD_GUI=ON`）

## 2. 打包端设计
### 2.1 流程
1. 解析命令行参数（`ConsoleInterface::parsePackagerArgs`）。
2. 读取并校验配置（`ConfigurationManager`）。
3. 扫描输入目录（`FolderScanner`）。
4. 按配置映射安装目标目录（`applyFolderTargets`）。
5. 执行压缩（`CompressionModule`）。
6. 生成扩展元数据（`MetadataGenerator`）。
7. 生成安装器（`InstallerGenerator`）。
8. 可选生成外部数据包（`--data-out`）。

### 2.2 算法与参数
- 压缩算法枚举：`CompressionAlgorithm::{LZMA_HIGH, ZSTD}`。
- CLI 优先级：`CLI 参数 > 配置文件 > 默认值`。
- 压缩级别校验由配置校验器完成：
  - lzma: 0-9
  - zstd: 1-22

## 3. 配置系统设计
### 3.1 配置发现与优先级
配置来源顺序：
1. 环境变量 `PACKAGER_CONFIG`
2. 输入目录内自动发现：
   - `packager.yaml`
   - `packager.yml`
   - `packager.json`
   - `.packager.json`

### 3.2 配置格式兼容
- 支持 legacy 扁平 JSON。
- 支持结构化 schema（`package/install/folders/...`），加载时会归一化到内部字段。
- YAML 通过 `yaml-cpp` 解析后转 JSON 再统一处理。

### 3.3 关键字段
必填：
- `Version`
- `AppName`
- `InstallDir`

可选（部分）：
- `Icon`, `WebPageUrl`, `ProductName`, `FileVersion`, `ProductVersion`
- `compressionAlgorithm`, `compressionLevel`
- `Folder`, `Registry`, `KillProcesses`
- `InstallState`
- `components`, `ui.componentSelection`

## 4. 配置校验设计
校验器：`ConfigurationValidator`

主要规则：
- 应用名不能为空，不含非法字符。
- 安装目录、目标路径合法。
- icon 文件存在且扩展名为 `.ico`。
- 安装状态配置与模式匹配（Registry/File/Both）。
- 组件规则（唯一 ID、依赖图无环、来源合法、安全约束）。

失败输出：
- `isValid=false`
- 返回结构化错误列表，入口层汇总并终止流程。

## 5. 安装端设计
### 5.1 模式切换
- GUI 模式（默认，`BUILD_GUI=ON` 且非 `--silent`）
- 控制台模式（`--silent` 或无 GUI）

### 5.2 安装流程（控制台核心流程）
1. 解析参数。
2. 读取嵌入元数据或外部数据包元数据。
3. 校验元数据。
4. 解析目标路径并执行解压安装。
5. 处理注册表、快捷方式、安装状态、清理逻辑。
6. 写入 manifest，用于卸载回放。

### 5.3 卸载流程
触发方式：
- `installer.exe --uninstall`
- 可执行名为 `uninstall.exe`

manifest 查找路径：
1. 当前目录本地 manifest
2. 卸载注册表路径推导
3. 默认 manifest 路径

## 6. 数据模型设计
关键结构体位于 `include/common/types.h`：
- `PackagerConfiguration`
- `ExtendedInstallationMetadata`
- `FolderInfo`, `FolderTargetConfig`
- `ComponentConfig`, `ComponentSourceConfig`
- `InstallStateConfig`

## 7. 并发与性能设计
- 压缩/解压支持多线程参数（`--threads`）。
- 数据分块与文件索引用于大文件场景。
- `sparseFileThresholdBytes` 控制稀疏文件阈值。

## 8. 构建设计
关键 CMake 选项：
- `BUILD_GUI`（默认 ON）
- `ENABLE_ZSTD`（默认 ON）
- `VERIFY_THIRD_PARTY_HASHES`（默认 ON）
- `YAML_CPP_FETCH_FALLBACK`（默认 OFF）
- `ZSTD_FORCE_THIRD_PARTY_BINARY`（默认 OFF）

产物：
- `build/Release/packager.exe`
- `build/Release/installer.exe`

## 9. 错误处理与可观测性
- 配置阶段：返回明确字段错误。
- 打包/安装阶段：控制台分级输出（INFO/WARNING/ERROR）。
- 失败返回非 0 退出码。

## 10. 设计边界与后续扩展
当前已支持：
- 组件化安装、下载源安全校验、UI 绑定配置。

可扩展方向：
- 更细粒度安装流程 DSL（`flow`）执行引擎。
- 更丰富的安装前检查插件机制。
- 完整发布工件签名与校验流水线。