# 用户指南（User Guide）

`packager.exe` 读取一个**载荷目录**（`--input`）和一个**配置目录**（`--config`），生成一个自解压的 GUI 安装器 `installer.exe`。本指南覆盖打包、安装、卸载与升级。

> 设计原则——「值/逻辑各归其位」：
> - **YAML 只声明「本次构建是什么 + 怎么打包 + 跑哪两个脚本」**，改它不需要重编引擎；
> - **引擎写死所有稳定机制**（安装行为默认值、清理规则、注册表、系统卸载入口等）；
> - **跨版本兼容**走引擎内的 C++ 迁移表（`src/installer/migration`），跟版本走、随版本删。
>
> 因此 `packager.yaml` 已收窄为 `app` / `package` / `hooks` 三块。旧 schema（`schemaVersion: 3`、`installer`/`uninstaller`/`components`/`installState` 等）已不再支持，会在打包阶段直接失败。

---

## 1. 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_GUI=ON `
  -DSTATIC_LINK_RUNTIME=ON `
  -DENABLE_ZSTD=ON

cmake --build build --config Release
```

构建产物：
- `build/Release/packager.exe` —— 打包工具
- `build/Release/installer.exe` —— 安装器模板（被打包器嵌入）
- `build/Release/uninstaller.exe` —— 卸载器模板（作为 PE 资源嵌入安装器）

---

## 2. 目录约定

载荷目录（`--input`）—— 每个顶层子目录是一个「载荷文件夹」：

```text
payload/
├─ app/        # 主程序
└─ plugins/    # 插件等
```

配置目录（`--config`）：

```text
build-config/
├─ packager.yaml      # 必需
├─ app.ico            # 可选，app.icon 引用（相对本目录解析）
├─ resources/         # GUI 皮肤（DuiLib XML 布局 + 图片），打成 RES_ZIP 嵌入
└─ scripts/           # 可选，hooks 引用的 pre/post bat
```

- `package.layout[].source` 相对 `--input` 解析（顶层子目录名）。
- `packager.yaml`、`resources/`、`app.icon`、`hooks[].path` 相对 `--config` 解析。

---

## 3. 打包用法

```powershell
.\build\Release\packager.exe --input <载荷目录> --config <配置目录> --output <输出 installer.exe>
```

参数可任意顺序：

```powershell
.\build\Release\packager.exe -o .\dist\MyAppSetup.exe -c .\build-config -i .\payload
```

支持的公开参数（仅此四个）：

| 参数 | 简写 | 说明 |
|------|------|------|
| `--input`  | `-i` | 载荷目录 |
| `--config` | `-c` | 配置目录（含 packager.yaml / resources / scripts） |
| `--output` | `-o` | 生成的安装器 exe 路径 |
| `--help`   | `-h` | 帮助 |

---

## 4. packager.yaml 配置

完整示例（即仓库根 `packager.yaml`）：

```yaml
app:
  productName: SampleDesktopApp                    # 用户可见产品名（数据目录/注册表键统一用它）
  publisher: SampleCompany                         # 发布者，同时作为版本资源 CompanyName
  version: 1.0.0                                   # 每次发版必改（可含 -beta 等预发布后缀）
  defaultDir: "%ProgramFiles%\\SampleDesktopApp"   # GUI 默认安装目录
  # icon: app.ico                                  # 可选，安装器 exe 图标，相对 --config 解析
  # copyright: "Copyright (c) 2026 SampleCompany"  # 可选，缺省时引擎用 publisher + 构建年份生成

package:
  compression: { algorithm: xz, level: 9 }         # algorithm: xz | zstd；可选 blockSize: <MiB>
  # blockSize（可选, MiB）：XZ 多线程分块大小。0/缺省=自动(按载荷大小对齐解码并行度,块更大、
  #   压缩比更高,同时仍可多线程解压)。调大→压缩比更好/解压并行更少；调小→反之。仅影响 xz。
  layout:                                          # 可选，逐文件夹落点
    - { source: app,     target: "%InstallDir%" }
    - { source: plugins, target: "%ProgramData%\\SampleDesktopApp\\plugins" }

hooks:                                             # 可选，整段删除即不跑脚本
  preInstall:                                      # 脚本列表，按顺序依次执行，可配多个
    - path: scripts/pre_install.bat
      args: ""
      onFailure: abort                             # abort（中止并回滚） | continue（记日志继续）
      timeoutSec: 300
    - path: scripts/pre_install.ps1                # 支持 .bat / .cmd / .ps1
      onFailure: continue
      timeoutSec: 120
  postInstall:
    - path: scripts/post_install.bat
      args: ""
      onFailure: continue
      timeoutSec: 600
      keep: true                                   # 可选，执行后保留脚本+兄弟文件
      keepDir: "%INSTALL_DIR%\\scripts"            # keep=true 时必填，保留目标目录
```

### 4.1 app（身份）

| 字段 | 必填 | 说明 |
|------|------|------|
| `productName` | 是 | 产品名；数据目录、注册表键、主进程名、快捷方式名均由它派生 |
| `publisher`   | 是 | 发布者；同时写入 PE 版本资源 CompanyName |
| `version`     | 是 | 版本号；每次发版必改，可带 `-beta1` 等后缀（写 PE 资源时去后缀取四段数字） |
| `defaultDir`  | 是 | GUI 默认安装目录，支持环境变量 |
| `icon`        | 否 | 安装器 exe 图标，相对 `--config` 解析；留空不改图标 |
| `copyright`   | 否 | 缺省由 `publisher + 构建年份` 生成 |

### 4.2 package（打包参数）

- `compression.algorithm`：`xz`（LZMA2，压缩率高）或 `zstd`（速度快）。
- `compression.level`：压缩级别。
- `compression.blockSize`：**可选**，单位 MiB，仅对 xz 生效。XZ 多线程把载荷切成独立块压缩，块越大压缩比越好但解压并行度越低。`0`/缺省=**自动**：按载荷大小目标约 4 块并夹在 64–256 MiB，兼顾压缩比与多线程解压（与安装器解码线程上限对齐）。需要更小体积可调大（如 `512`，趋近单流、解压串行）；需要更快解压可调小。
- `layout`：**可选**。逐文件夹落点声明。
  - `source` = `--input` 下的顶层子目录名；
  - `target` = 安装目标，支持 `%InstallDir%` 以及环境变量（`%ProgramData%` / `%AppData%` / `%LocalAppData%` 等）；
  - 未在 `layout` 声明的子目录，默认落到 `%InstallDir%\<目录名>`。
  - 注意：安装器以管理员权限运行，`%AppData%`/`%LocalAppData%` 展开的是被提权账户目录，未必是当前登录用户；要「全机共享」用 `%ProgramData%` 更稳妥。

### 4.3 hooks（安装前/后脚本）

`hooks` 整段可选；不需要时直接删除。

- `preInstall` / `postInstall` 都是**脚本列表**，按声明顺序**依次执行**，可配置多个脚本。
- 脚本类型按扩展名识别，支持 **`.bat` / `.cmd` / `.ps1`**（ps1 经 `powershell -NoProfile -ExecutionPolicy Bypass -File` 运行）。
- 也兼容「单个对象」的旧写法（按单元素列表处理）。
- 同一钩子点内，任一脚本以 `onFailure: abort` 失败会立即中止安装（preInstall 在解压前、postInstall 触发回滚）；`continue` 则记日志后继续后续脚本。
- **兄弟脚本**：主脚本所在目录里的其余文件会被**递归内嵌**，运行期与主脚本释放到**同一临时目录**，故主脚本可直接 `call common.bat`、`.\sub\helper.ps1` 或读取同目录数据文件（脚本工作目录即该临时目录）。
- **keep / keepDir**（可选）：`keep: true` 时脚本执行完（**无论成功失败**）把主脚本+兄弟文件拷贝到 `keepDir`；默认 `false` = 释放到临时目录、用完即删。`keepDir` 支持 `%INSTALL_DIR%` / `%VERSION%` 与系统环境变量。

详见下文 [§7 安装脚本契约](#7-安装脚本契约prepost-bat)。

---

## 5. 安装器用法

GUI 安装：

```powershell
.\dist\MyAppSetup.exe
```

静默安装（可叠加覆盖项）：

```powershell
.\dist\MyAppSetup.exe --silent `
  --destination "C:\Program Files\MyApp" `
  --auto-startup true `
  --desktop-icon false
```

升级安装（需机器上已存在该产品的安装记录）：

```powershell
.\dist\MyAppSetup.exe --upgrade
.\dist\MyAppSetup.exe --upgrade --silent
```

安装器参数：

| 参数 | 说明 |
|------|------|
| `-d` / `--destination <dir>` | 默认安装目录 |
| `-s` / `--silent` | 静默安装 |
| `--upgrade` | 升级模式（探测已装目录） |
| `--auto-startup <true\|false>` | 覆盖开机自启默认值 |
| `--desktop-icon <true\|false>` | 覆盖桌面图标默认值 |
| `-h` / `--help` | 帮助 |

---

## 6. 卸载

安装完成后，引擎会在安装目录释放 `uninstall.exe`，并写入系统「程序和功能」卸载入口。

```powershell
.\dist\uninstall.exe          # GUI 卸载
.\dist\uninstall.exe --silent # 静默卸载
```

卸载读取安装目录里的 `install.manifest.json` 快照，删除其中记录的文件、快捷方式、开机自启、系统卸载入口，再清除 `install-state.json` 与产品注册表键，最后调度 `uninstall.exe` 自删除。注册表与系统卸载入口的清理由引擎按产品名写死处理，无需在 YAML 中声明。

---

## 7. 安装脚本契约（pre/post 脚本）

约定 `packager.yaml` 中 `hooks.preInstall` / `hooks.postInstall` 列表里每个脚本的行为。引擎实现与脚本作者都以此为准。每个钩子点可配置多个脚本，按声明顺序依次执行。

### 7.1 配置字段（每个脚本一项）

| 字段 | 含义 | 取值 |
|------|------|------|
| `path` | 脚本路径，相对 `--config` 目录解析；按扩展名识别类型 | `.bat` / `.cmd` / `.ps1` |
| `args` | 本次构建特有的额外参数 | 字符串，可为空 |
| `onFailure` | 失败时的处理 | `abort` \| `continue` |
| `timeoutSec` | 超时上限（秒） | 整数 |
| `keep` | 执行后是否保留脚本+兄弟文件到 `keepDir` | `true` \| `false`（默认 `false`） |
| `keepDir` | 保留目标目录（`keep: true` 时必填）；支持 `%INSTALL_DIR%`/`%VERSION%`/系统环境变量 | 字符串 |

### 7.2 成功 / 失败判据（引擎写死，不可配）

- 退出码 `0` = 成功；非 `0` = 失败。
- **超时**：到达 `timeoutSec` 后引擎 kill 进程，**一律按失败处理**，走 `onFailure`。

### 7.3 onFailure 语义

- `abort`：中止安装并**回滚**已释放的文件/注册表/卸载入口。用于「这步不成功，装了也是坏的」。
- `continue`：记录日志后继续。用于可选的、失败也不影响主体安装的步骤。

### 7.4 注入变量

脚本运行时可直接读取以下环境变量：

- **引擎注入**（引擎才知道的值）：`INSTALL_DIR`（实际安装目录）、`VERSION`（本次安装版本号）。
- **系统现有**（引擎只保证不清空环境块）：`USERPROFILE`、`APPDATA`、`LOCALAPPDATA`、`ProgramData` 等。

注入方式为**环境变量**，不拼进命令行。脚本应优先读环境变量；`args` 只承载本次构建特有的额外参数。

### 7.5 路径与执行

- **打包期**：`path` 相对 `--config` 解析，脚本被读入并内嵌进安装器（保留原扩展名）；**主脚本所在目录里的其余文件一并被递归内嵌**为兄弟文件（保留相对结构）。
- **运行期**：安装器把主脚本+兄弟文件释放到**同一个临时目录**后执行（`.bat/.cmd` 经 `cmd /c`、`.ps1` 经 `powershell -File`），脚本**工作目录即该临时目录**，与终端用户机器上是否存在该路径无关。
- 同一钩子点内的多个脚本**按声明顺序**执行；任一脚本 `abort` 失败即停止后续脚本并中止安装。
- preInstall 在解压**之前**运行；此时安装目录可能尚不存在，脚本如需写文件请自行 `mkdir`。
- postInstall 在收尾**之后**运行，此时安装目录已就绪。

#### 支持调用「同目录的兄弟脚本」

打包期会把 `path` 主脚本**所在目录下的全部文件递归内嵌**；运行期它们与主脚本一起释放到同一临时目录、且该目录就是脚本的工作目录。因此：

- ✅ **支持**调用「打包时放在主脚本旁边的兄弟脚本/文件」——例如脚本内 `call common.bat`、`.\sub\helper.ps1`，或读取同目录数据/配置文件。
- ✅ **支持**调用**系统命令 / PATH 上的工具**（`reg`、`powershell`、`sc`、`schtasks`、`msiexec` 等）。
- ✅ **支持**调用**目标机上已存在的、绝对路径**的程序或脚本。
- ✅ **支持**调用**作为 payload 安装进去的文件**，但**仅限 postInstall**：此时 payload 已解压，可用 `%INSTALL_DIR%\...\helper.bat` 调用；preInstall 在解压前运行、安装目录尚为空，不可用。

> ⚠️ 兄弟文件按「主脚本所在目录」**递归**内嵌。若多个 hook 共用同一个 `scripts/` 目录，则**每个 hook 都会带入该目录下的全部文件**（含其他 hook 的脚本），既增大安装包、`keepDir` 拷贝也会包含无关文件。**建议把每个 hook 的主脚本与其辅助文件放进各自独立的子目录**（如 `scripts/post/`、`scripts/pre/`）。

#### 执行后保留脚本（keep / keepDir）

默认脚本释放到临时目录、执行完即删。配置 `keep: true` + `keepDir` 后，脚本执行完（**无论成功还是失败/超时**）会把**主脚本+兄弟文件**整体拷贝到 `keepDir`：

```yaml
- path: scripts/post/post_install.bat
  onFailure: continue
  timeoutSec: 600
  keep: true
  keepDir: "%INSTALL_DIR%\\scripts"   # 例：保留到安装目录下的 scripts 子目录
```

- `keepDir` 支持 `%INSTALL_DIR%`（实际安装目录）、`%VERSION%`（本次版本）与系统环境变量（`%ProgramData%` 等）。
- 临时目录始终在脚本结束后清理；`keep` 只是**额外**拷一份持久副本，不改变执行位置。
- 注意：postInstall 以 `onFailure: abort` 失败会触发**整体回滚**（删安装目录）；若此时把副本拷进了安装目录，可能随回滚一并被删。需要在失败后仍保留时，`keepDir` 请指向安装目录之外（如 `%ProgramData%\<产品>\scripts`）。

### 7.6 权限

脚本继承安装器的管理员权限运行，**脚本内不要再自行 elevate**。

### 7.7 职责边界（重要）

这些脚本**只做本次构建特有、与版本演进无关的收尾动作**（如注册本次才有的服务、拷贝许可文件等）。

**跨版本兼容一律走引擎内的迁移函数**（`src/installer/migration`），禁止写进 pre/post bat。否则兼容逻辑会散落、与版本对不上，丧失「迁移跟版本走、该删随版本删」的好处。

---

## 8. 增量安装

无论 `compression` 如何配置，安装器都会在 `install.manifest.json` 里记录每个文件的内容指纹（FNV-1a）。下次「覆盖/升级」安装时：

- 内容未变的文件**跳过重写**（不写盘、不触发杀软扫描）；
- 仅删除新构建中已移除的文件。

---

## 9. 故障排查

| 现象 | 原因与处理 |
|------|-----------|
| `Unsupported package manifest version` | 安装器与包的 manifest 版本不一致（旧包或引擎未同步重编）。请用当前 `packager.exe` 重新打包，并确保 packager/installer/uninstaller 三者同版本。 |
| 打包报「No resource files found for zip packaging」 | `--config` 目录缺少 `resources/`（GUI 皮肤）。补上后重试。 |
| `package.layout[].source` 找不到 | `source` 相对 `--input` 解析，确认该顶层子目录存在。 |
| 图标未生效 | `app.icon` 相对 `--config` 解析（除非给绝对路径）。 |
| preInstall 脚本写文件失败 | preInstall 在解压前运行，安装目录尚不存在；脚本内先 `mkdir`。 |
| hook 脚本里 `call xxx.bat` 报找不到文件 | 兄弟文件按「主脚本所在目录」递归内嵌，确认被调脚本与主脚本在同一目录（或其子目录）下；详见 §7.5。 |
| `keepDir is required when keep is true` | 配了 `keep: true` 却没给 `keepDir`。补上保留目标目录。 |

---

## 10. 相关文档

- [打包器流程图](打包器流程图.md)
- [安装器流程图](安装器流程图.md)
