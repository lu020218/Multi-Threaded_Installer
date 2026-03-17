# 详细使用说明文档

## 1. 环境准备
- Windows + Visual Studio 2022
- CMake >= 3.16
- 已初始化子模块：

```powershell
git submodule update --init --recursive
```

## 2. 构建
推荐构建命令：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_GUI=ON `
  -DSTATIC_LINK_RUNTIME=ON `
  -DENABLE_ZSTD=ON

cmake --build build --config Release
```

构建产物：
- `build/Release/packager.exe`
- `build/Release/installer.exe`

## 3. 准备输入目录
建议目录结构：

```text
input/
├─ bin/
├─ plugins/
└─ packager.yaml
```

其中 `packager.yaml`（或 `packager.json`）放在输入目录根部。

## 4. 生成安装器
基础命令：

```powershell
.\build\Release\packager.exe <input_directory> <output_installer.exe>
```

示例：

```powershell
# LZMA
.\build\Release\packager.exe -a lzma -l 9 .\input .\dist\MyAppSetup.exe

# ZSTD
.\build\Release\packager.exe -a zstd -l 3 .\input .\dist\MyAppSetup.exe

# 同时生成外部数据包
.\build\Release\packager.exe -a zstd -l 3 -p .\dist\MyApp.data .\input .\dist\MyAppSetup.exe
```

## 5. 运行安装器
### 5.1 普通安装
```powershell
.\dist\MyAppSetup.exe
```

### 5.2 静默安装
```powershell
.\dist\MyAppSetup.exe -s
```

### 5.3 指定安装目录
```powershell
.\dist\MyAppSetup.exe -d "C:\Program Files\MyApp"
```

### 5.4 组件安装
```powershell
# 安装指定组件
.\dist\MyAppSetup.exe -s --components core,plugins

# 安装全部可选组件
.\dist\MyAppSetup.exe -s --all-components
```

### 5.5 卸载
```powershell
.\dist\MyAppSetup.exe --uninstall
```

或直接运行 `uninstall.exe`（若存在）。

## 6. 命令行参数
### 6.1 packager 参数
- `-a, --algorithm <lzma|zstd>`
- `-l, --level <level>`
- `-p, --data-out <file>`
- `-t, --threads <count>`
- `-v, --verbose`
- `-h, --help`

### 6.2 installer 参数
- `-d, --destination <dir>`
- `-p, --data-package <file>`
- `-t, --threads <count>`
- `-f, --force`
- `-s, --silent`
- `--debug`
- `--uninstall`
- `--component <id>`（可重复）
- `--components <id1,id2,...>`
- `--all-components`
- `-v, --verbose`
- `-h, --help`

## 7. 配置文件
### 7.1 配置发现规则
优先级：
1. 环境变量 `PACKAGER_CONFIG`
2. `<input>/packager.yaml`
3. `<input>/packager.yml`
4. `<input>/packager.json`
5. `<input>/.packager.json`

### 7.2 最小 YAML 示例
```yaml
schemaVersion: 1

package:
  version: "1.0"
  appName: "MyDesktopApp"
  compressionAlgorithm: "zstd"
  compressionLevel: 3

install:
  defaultInstallDir: "%ProgramFiles%"
  autoStartup: false
  desktopIcons: true
  requireAdmin: true

folders:
  - name: bin
    target: installDirectory
    appendDirectoryName: false
  - name: plugins
    target: "%AppData%\\Roaming"
  - name: userdata
    target: "%LocalAppData%"
```

### 7.3 组件配置示例（节选）
```yaml
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
```

## 8. 常见问题
### 8.1 提示配置文件字段错误
- 检查必填项：`Version`、`AppName`、`InstallDir`。
- 检查压缩等级范围是否匹配算法。
- 若使用组件下载源，确保 `https://` + 64 位 `sha256`。

### 8.2 打包失败：找不到输入目录
- 确认输入目录存在。
- 使用绝对路径排除当前目录影响。

### 8.3 安装器资源加载失败（GUI）
- 确认安装器同目录存在 `resources/`，或已正确嵌入资源。

## 9. 发布建议
- 通过 `scripts/prepare_release.ps1` 或 `scripts/prepare_release.bat` 生成分发包。
- 分发目录建议包含：
  - `installer.exe`
  - `packager.exe`
  - `resources/`
  - `docs/USER_GUIDE.md`
  - `docs/REQUIREMENTS.md`
  - `docs/DETAILED_DESIGN.md`

## 附录：`defaultSelected` 与 UI 复选框绑定

- `components[].defaultSelected` 只控制组件的默认选中状态。
- 它不会仅凭组件 `id` 自动绑定到某个 UI 复选框。
- 只有在组件和 UI 控件的绑定关系建立成功后，界面上的勾选状态才会被更新。
- 生效前提：
  - `ui.componentSelection.mode` 为 `embeddedInExistingPages` 或 `hybrid`
  - `ui.componentSelection.binding.pages[].controls` 中包含对应复选框的 `name`
  - XML 中的 `CheckBox` 带有 `userdata="component:<componentId>"`
- 示例：

```xml
<CheckBox name="chkChrome" userdata="component:chrome_plugin" />
```

```yaml
components:
  - id: chrome_plugin
    defaultSelected: true

ui:
  componentSelection:
    mode: embeddedInExistingPages
    binding:
      strategy: xml_userdata
      tokenPrefix: "component:"
      pages:
        - skin: welcome_page.xml
          controls:
            - chkChrome
```

- 如果缺少 `userdata`，或者复选框名称没有出现在 `controls` 中，或者你运行的不是 `packager.exe` 生成的最终安装包，那么界面上不会体现 `defaultSelected` 的默认勾选效果。

## 附录：`cleanup.onUninstall`

- `cleanup.onUninstall` 用于删除不属于安装根目录的额外路径，例如 `%LocalAppData%` 下的缓存目录或日志目录。
- 这些规则会在安装时写入本地 `install.manifest.json`，并在卸载时按记录执行。
- 支持的字段：
  - `path`：必填，支持环境变量以及 `%InstallDir%`
  - `recursive`：可选，默认值为 `true`；为 `true` 时会递归删除目录内容，为 `false` 时只删除目标本身，适用于单文件或空目录
  - `onlyIfEmpty`：可选，默认值为 `false`；为 `true` 时仅当目标已经为空时才删除，适合在保留用户数据的同时顺手清理空的应用根目录
- 推荐用法：
  - 对缓存目录或日志目录设置 `recursive: true`
  - 对 `%LocalAppData%` 下的应用根目录设置 `onlyIfEmpty: true`

示例：

```yaml
cleanup:
  onUninstall:
    - path: "%LocalAppData%\\MyDesktopApp\\Cache"
      recursive: true
      onlyIfEmpty: false
    - path: "%LocalAppData%\\MyDesktopApp\\Logs"
      recursive: true
      onlyIfEmpty: false
    - path: "%LocalAppData%\\MyDesktopApp"
      recursive: true
      onlyIfEmpty: true
```
