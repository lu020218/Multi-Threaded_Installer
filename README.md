# Multi-Threaded Installer

一个基于 C++17 的打包与安装系统，包含两个核心程序：

- `packager`：将输入目录打包为安装程序（支持 `lzma` / `zstd`）
- `installer`：执行安装、静默安装、卸载，支持多线程解压与组件选择

## 当前压缩算法支持

- 已支持：`LZMA`（原有主路径）
- 已支持：`ZSTD`（打包端压缩 + 安装端解压）
- 配置方式：可在 `packager.yaml` 或 `packager.json` 中指定
  - `compressionAlgorithm`: `lzma` 或 `zstd`
  - `compressionLevel`: 压缩级别
- 级别范围：
  - `lzma`: `0-9`（默认 `9`）
  - `zstd`: `1-22`（默认 `3`）
  - `-1` 表示未显式设置，运行时使用算法默认值

## 仓库结构

```text
.
├─ include/
├─ src/
│  ├─ packager/
│  ├─ installer/
│  ├─ gui/
│  └─ common/
├─ resources/
├─ docs/
├─ third_party/
└─ CMakeLists.txt
```

## 构建前准备

### 1) 同步子模块

```powershell
git submodule update --init --recursive
```

### 2) 第三方依赖

默认使用仓库内依赖：

- `third_party/xz`（LZMA）
- `third_party/yaml-cpp`（配置解析）
- `third_party/DuiLib_Ultimate`（GUI 模式）
- `third_party/zstd`（可选，启用 ZSTD）

## 构建（Windows / MSVC）

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_GUI=ON `
  -DSTATIC_LINK_RUNTIME=ON `
  -DENABLE_ZSTD=ON

cmake --build build --config Release
```

产物：

- `build/Release/packager.exe`
- `build/Release/installer.exe`

## ZSTD（MSVC 静态库）说明

项目会优先自动检测本地静态库：

- `third_party/zstd/lib_static/zstd_static.lib`

并自动查找头文件目录（优先）：

- `third_party/zstd/include`
- `third_party/zstd/lib`

如果检测成功，会自动启用 `ZSTD_FOUND`，无需再手动指定 `ZSTD_FORCE_THIRD_PARTY_BINARY=ON`。

## 使用方式

### 1) 生成安装包

```powershell
.\build\Release\packager.exe <input_directory> <output_installer.exe>
```

Notes:
- `packager.exe` reads the `installer.exe` template and `resources/` source assets from its current directory.
- Generated installers embed UI resources, so distribution no longer requires an external `resources/` directory.

示例：

```powershell
# 使用 LZMA
.\build\Release\packager.exe -a lzma -l 9 .\input .\dist\MyAppSetup.exe

# 使用 ZSTD
.\build\Release\packager.exe -a zstd -l 3 .\input .\dist\MyAppSetup.exe
```

### 2) 运行安装

```powershell
.\dist\MyAppSetup.exe
```

静默安装：

```powershell
.\dist\MyAppSetup.exe -s
```

卸载：

```powershell
.\dist\MyAppSetup.exe --uninstall
```

## 配置文件示例（YAML）

```yaml
Version: "1.0"
AppName: "MyDesktopApp"
InstallDir: "%ProgramFiles%"
compressionAlgorithm: "zstd"
compressionLevel: 3

Folder:
  InstallDir: "bin"
  Roaming: "plugins"
  Local: "userdata"

AutoStartup: false
DesktopIcons: true
RequireAdmin: true
```

完整字段说明见 `docs/USER_GUIDE.md`。

## 命令行参数（摘要）

### packager

- `-a, --algorithm <lzma|zstd>`
- `-l, --level <level>`（`lzma: 0-9`，`zstd: 1-22`）
- `-p, --data-out <file>`
- `-t, --threads <count>`
- `-v, --verbose`
- `-h, --help`

参数与配置文件关系：

- 推荐将 `compressionAlgorithm` / `compressionLevel` 固化在 `packager.yaml/json`（主配置源）
- `--algorithm` / `--level` 用于临时覆盖（例如 CI 场景或压缩对比）
- 优先级：`CLI 参数` > `配置文件` > `内置默认值`

### installer

- `-d, --destination <dir>`
- `-p, --data-package <file>`
- `-t, --threads <count>`
- `-f, --force`
- `-s, --silent`
- `--uninstall`
- `--component <id>` / `--components <a,b,c>` / `--all-components`
- `-v, --verbose`
- `-h, --help`


## 相关文档

- `docs/REQUIREMENTS.md`
- `docs/DETAILED_DESIGN.md`
- `docs/USER_GUIDE.md`

## Release Model

- Generated installers are `embedded-only` for GUI resources.
- Keep `installer.exe`, `packager.exe`, and the source `resources/` directory together while packaging.
- Do not distribute an external `resources/` directory with the final generated installer unless you are deliberately debugging packaging internals.
