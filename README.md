# Multi-Threaded Installer

一个基于 C++17 的打包与安装系统，包含两个核心程序：

- `packager`：将输入目录打包为自解压安装程序
- `installer`：执行安装/静默安装/卸载，支持组件选择与多线程解压

当前仓库的主线构建与 CI 面向 Windows（含 GUI）。

## 主要能力

- LZMA 压缩与解压（当前仅支持 LZMA）
- 生成单文件安装包（可选额外导出 `.dat` 数据包）
- 安装器支持 GUI 与 CLI/静默模式
- 支持组件化安装（`--component` / `--components` / `--all-components`）
- 支持卸载（`--uninstall` 或 `uninstall.exe` 自动识别）
- 支持 JSON/YAML 配置文件

## 仓库结构

```text
.
├─ include/                  # 头文件
├─ src/
│  ├─ packager/              # 打包器实现
│  ├─ installer/             # 安装器实现
│  ├─ gui/                   # GUI 实现（BUILD_GUI=ON）
│  └─ common/                # 公共模块
├─ resources/                # GUI 资源（XML/图片/语言/license）
├─ docs/                     # 详细文档
├─ tests/                    # 测试
├─ third_party/              # 第三方依赖（含预编译库与子模块）
└─ CMakeLists.txt
```

## 构建前准备

### 1) 同步子模块

`yaml-cpp` 通过子模块提供：

```powershell
git submodule update --init --recursive
```

### 2) 准备第三方库

默认配置会使用仓库内预置依赖并校验哈希：

- `third_party/xz/include` 与 `third_party/xz/lib_static/lzma.lib`
- `third_party/DuiLib_Ultimate/DuiLib`
- `third_party/DuiLib_Ultimate/lib_static/DuiLib.lib`（`BUILD_GUI=ON` 时必需）

说明：
- `BUILD_GUI=ON` 时，`STATIC_LINK_RUNTIME` 必须为 `ON`（CMake 中有强约束）。
- 若哈希不匹配且 `VERIFY_THIRD_PARTY_HASHES=ON`，配置会失败。

## 构建

推荐 Windows + Visual Studio 生成器：

```powershell
cmake -S . -B build `
  -DBUILD_GUI=ON `
  -DSTATIC_LINK_RUNTIME=ON `
  -DVERIFY_THIRD_PARTY_HASHES=ON

cmake --build build --config Release
```

产物通常位于：

- `build/Release/packager.exe`
- `build/Release/installer.exe`

## 快速使用

### 1) 准备输入目录

`packager` 会扫描输入目录下的一级子目录作为“安装文件夹单元”。  
请把待安装内容放在这些子目录中，并在输入目录放置配置文件（可选）：

- `packager.yaml`
- `packager.yml`
- `packager.json`
- `.packager.json`

也可通过环境变量指定：

```powershell
$env:PACKAGER_CONFIG="E:\path\to\packager.yaml"
```

### 2) 生成安装包

```powershell
.\build\Release\packager.exe <input_directory> <output_installer.exe>
```

示例：

```powershell
.\build\Release\packager.exe .\test_input .\dist\TestAppSetup.exe
```

注意：
- 输出目录必须已存在（否则参数校验会报错）。
- `packager` 会使用已构建的 `installer.exe` 作为模板拼装自解压包。

### 3) 运行安装

```powershell
.\dist\TestAppSetup.exe
```

静默安装：

```powershell
.\dist\TestAppSetup.exe -s
```

卸载：

```powershell
.\dist\TestAppSetup.exe --uninstall
```

或将卸载程序命名为 `uninstall.exe` 执行（会自动进入卸载模式）。

## 命令行参数

### packager

```text
Usage: packager [options] <input_directory> <output_file>

  -a, --algorithm <lzma>   压缩算法（当前仅 lzma）
  -l, --level <level>      压缩级别（0-9）
  -p, --data-out <file>    额外导出外部数据包
  -t, --threads <count>    压缩线程数
  -v, --verbose            详细日志
  -h, --help               帮助
```

### installer

```text
Usage: installer [options] [folder_mappings...]

  -d, --destination <dir>     默认安装目录
  -p, --data-package <file>   使用外部数据包
  -t, --threads <count>       解压线程数
  -f, --force                 强制覆盖
  -s, --silent                静默安装
  --debug                     GUI 模式下显示控制台
  --uninstall                 卸载模式
  --component <id>            选择单个组件（可重复）
  --components <a,b,c>        批量选择组件
  --all-components            安装全部可选组件
  -v, --verbose               详细日志
  -h, --help                  帮助

  folder_mappings 格式:
  <folder_name>=<target_path>
```

## 配置文件示例（YAML）

```yaml
Version: "1.0"
AppName: "MyDesktopApp"
InstallDir: "%ProgramFiles%"
Folder:
  InstallDir: "bin"
  Roaming: "plugins"
  Local: "userdata"
AutoStartup: false
DesktopIcons: true
RequireAdmin: true
MinWindowsVersion: "10.0.19041"

components:
  - id: core
    name: Core Files
    required: true
    defaultSelected: true
    folders: ["bin"]
    source:
      type: embedded

  - id: plugins
    name: Optional Plugins
    required: false
    defaultSelected: true
    dependsOn: ["core"]
    source:
      type: local
      local:
        base: "%InstallDir%\\components"
        installer: "plugins\\install_plugins.bat"
        args: "/silent"
        wait: true
        timeoutSec: 900
```

完整字段说明见：`docs/configuration_reference.md`

## 测试

默认不构建测试，开启方式：

```powershell
cmake -S . -B build-tests -DBUILD_TESTS=ON
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

## 相关文档

- `docs/configuration_reference.md`
- `docs/COMMAND_LINE_REFERENCE.md`
- `docs/BUILD_AND_DEPLOYMENT.md`
- `docs/components_troubleshooting_guide.md`
- `docs/TROUBLESHOOTING.md`
