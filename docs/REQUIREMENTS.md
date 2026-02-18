# 需求文档

## 1. 项目目标
本项目提供一套 Windows 安装包解决方案，包含两个可执行程序：
- `packager.exe`：将输入目录打包为安装器（支持 LZMA/ZSTD）。
- `installer.exe`：执行安装/卸载，支持静默模式、组件选择、外部数据包。

核心目标：
- 统一打包与安装流程，减少手工部署步骤。
- 在保证安装成功率的前提下，提高压缩率与解压性能。
- 支持企业常见需求：注册表写入、管理员权限、组件化安装、自动卸载。

## 2. 角色与使用场景
### 2.1 角色
- 打包维护者：配置打包参数并生成安装器。
- 最终用户：运行安装器完成安装或卸载。
- 发布工程师：基于构建产物生成发布包。

### 2.2 典型场景
- 场景 A：通过 `packager.yaml` 生成 GUI 安装器。
- 场景 B：安装器静默安装到自定义目录。
- 场景 C：按组件安装（`--components`）或全量安装（`--all-components`）。
- 场景 D：通过 `--uninstall` 或 `uninstall.exe` 执行卸载。

## 3. 功能需求
### 3.1 打包器功能需求
- 输入目录扫描与文件打包。
- 支持压缩算法：`lzma`、`zstd`。
- 支持压缩级别配置：
  - `lzma`: 0-9
  - `zstd`: 1-22
  - `-1` 表示使用算法默认值
- 支持从配置文件读取打包参数，支持 JSON/YAML。
- 支持生成外部数据包（`--data-out`）。
- 支持更新安装器图标与版本信息（配置存在相关字段时）。

### 3.2 安装器功能需求
- 支持 GUI 模式与控制台模式。
- 支持静默安装：`-s/--silent`。
- 支持卸载：`--uninstall`。
- 支持组件选择：`--component`、`--components`、`--all-components`。
- 支持指定目标目录：`-d/--destination`。
- 支持外部数据包：`-p/--data-package`。
- 支持覆盖安装：`-f/--force`。

### 3.3 配置能力需求
配置文件至少包含：
- `Version`
- `AppName`
- `InstallDir`

支持扩展字段：
- 安装行为：`AutoStartup`、`DesktopIcons`、`AutoCleanOldInstall`、`RequireAdmin`
- 压缩参数：`compressionAlgorithm`、`compressionLevel`
- 文件映射：`Folder.InstallDir`、`Folder.Roaming`、`Folder.Local`
- 注册表：`Registry[]`
- 进程终止：`KillProcesses`
- 安装状态：`InstallState`
- 组件化安装：`components[]`
- 组件 UI 绑定：`ui.componentSelection`

## 4. 组件与安全约束需求
- 组件 ID 必须唯一。
- 组件依赖必须可解析，且不允许循环依赖。
- `required=true` 的组件必须 `defaultSelected=true`。
- `source.type=local` 时：
  - `base` 必须以 `%InstallDir%`/`installDirectory` 开头。
  - `installer` 必须为相对路径。
  - 不允许 `..` 路径穿越。
- `source.type=download` 时：
  - `url` 必须 `https://`。
  - `sha256` 必须为 64 位十六进制字符串。

## 5. 非功能需求
- 兼容性：Windows 平台，优先 MSVC 构建。
- 可维护性：配置与实现解耦，配置错误可读性高。
- 可观测性：控制台输出关键进度与错误信息。
- 可靠性：元数据校验失败时应终止安装并返回失败码。

## 6. 构建与发布需求
- 需支持 CMake 构建。
- 需输出：`packager.exe`、`installer.exe`。
- 发布脚本应拷贝核心文档与资源目录。

## 7. 验收标准
- 能成功从输入目录生成安装器并运行安装。
- 配置合法时可打包，配置非法时给出明确错误。
- 静默安装、组件安装、卸载功能可用。
- 发布包包含可执行文件、资源、三份核心文档。