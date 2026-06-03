# 安装打包框架总体验收测试方案

## 1. 验收目标

本方案覆盖整个安装打包框架从 v3 配置、打包、manifest、安装、覆盖/升级、卸载、清理、UI、权限、日志到性能稳定性的端到端验收。目标是确认：

- `packager.exe` 只接受 v3 配置，能按 `--input/--config/--output` 生成可运行 installer。
- installer 可完成 GUI、静默、覆盖、升级安装。
- uninstaller 可完成 GUI、静默、manifest 卸载和安全 fallback 卸载。
- v3 `PackageManifest`、本地 `install.manifest.json`、installState、系统卸载项、快捷方式、启动项、组件信息能正确写入与清理。
- 覆盖/升级与卸载清理不会卡死，不会误删危险目录，不会跟随 reparse point。
- 多线程、日志、权限、长路径、中文路径等关键能力符合当前实现约定。

## 2. 测试环境

### 2.1 基础环境

- Windows 10 19041+ 或 Windows 11。
- Visual Studio 2022 Build Tools。
- CMake 可用。
- PowerShell。
- 普通用户与管理员两类运行环境均需覆盖。

### 2.2 构建产物

使用 Release 构建：

```powershell
cmake --build build_codex --config Release
```

需生成：

- `build_codex\Release\packager.exe`
- `build_codex\Release\installer.exe`
- `build_codex\Release\uninstaller.exe`
- `build_codex\test\Release\SchemaRegressionTests.exe`

### 2.3 测试目录

建议准备：

```text
test-work/
├─ payload/
│  ├─ app/
│  ├─ resources/
│  └─ plugins/
├─ config/
│  ├─ packager.yaml
│  ├─ app.ico
│  └─ resources/
└─ dist/
```

测试注册表命名空间建议：

- `HKCU\Software\MTITest_*`
- `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\MTITest_*`
- 管理员测试可使用 `HKLM\Software\MTITest_*`

## 3. 自动化回归

### TC-A01 构建通过

步骤：

1. 执行：
   ```powershell
   cmake --build build_codex --config Release
   ```

预期：

- 所有目标构建成功。
- 不出现编译错误。

### TC-A02 SchemaRegressionTests 全部通过

步骤：

1. 执行：
   ```powershell
   build_codex\test\Release\SchemaRegressionTests.exe
   ```

预期：

- 全部 `[PASS]`。
- 覆盖 v3 loader、manifest codec、installState、安装计划、升级清理、卸载上下文、清理 worker、多线程策略、路径格式化等核心回归。

### TC-A03 文档与示例没有旧主路径字段

步骤：

1. 执行：
   ```powershell
   rg "schemaVersion:\s*2|^install:|^ui:|^layout:|^lifecycle:|systemUninstallEntry:\s*auto|uninstaller\.cleanup\.legacy\.uninstallEntries" docs examples test -n
   ```

预期：

- 除“不兼容/拒绝旧字段”的说明和负向测试外，不再出现旧主路径配置。

## 4. Packager CLI 与配置加载

### TC-P01 三路径具名参数任意顺序

步骤：

1. 执行：
   ```powershell
   packager.exe -o test-work\dist\setup.exe -c test-work\config -i test-work\payload
   ```

预期：

- 打包成功。
- 输出 installer exe。
- 配置文件只从 `config` 目录读取。
- payload 只从 `input` 目录读取。

### TC-P02 拒绝裸路径参数

步骤：

1. 执行：
   ```powershell
   packager.exe test-work\payload test-work\config test-work\dist\setup.exe
   ```

预期：

- 打包失败。
- 提示必须使用 `--input/--config/--output`。

### TC-P03 拒绝旧 CLI 参数

步骤：

1. 执行：
   ```powershell
   packager.exe --input test-work\payload --config test-work\config --output test-work\dist\setup.exe --threads 4
   ```

预期：

- 打包失败。
- 提示未知参数。

### TC-P04 配置只从 config 目录读取

准备：

- 在 input 目录放一份无效 `packager.yaml`。
- 在 config 目录放合法 v3 `packager.yaml`。

预期：

- 打包成功。
- 不读取 input 目录中的配置。

### TC-P05 resources 只从 config/resources 读取

准备：

- `config/resources` 存在。
- 删除 packager.exe 旁边的 resources。

预期：

- 打包成功。
- installer 启动后 UI 资源正常加载。

### TC-P06 icon 相对 config 目录解析

准备：

- `app.icon: app.ico`。
- `app.ico` 放在 config 目录。

预期：

- 打包成功。
- 生成 installer 的图标和 version info 正确。

## 5. v3 配置校验

### TC-C01 合法完整 v3 配置通过

预期：

- `schemaVersion: 3`。
- 包含 `app/package/installer/uninstaller`。
- `installer.payload[]`、`installer.installState.detect`、`installer.systemUninstallEntry`、`uninstaller.cleanup.systemUninstallEntry` 均合法。
- 打包成功。

### TC-C02 v2 配置失败

步骤：

1. 使用 `schemaVersion: 2` 配置打包。

预期：

- 打包失败。
- 提示只支持 schemaVersion 3。

### TC-C03 缺少 installer.directoryName 失败

预期：

- 打包失败。
- 错误指向 `installer.directoryName`。

### TC-C04 installState detect 校验

用例：

- primary registry 引用不存在，失败。
- primary value 引用不存在，失败。
- legacy id 重复，失败。
- primary 和 legacy 都缺失，失败。
- primary 命中配置合法，成功。
- 仅 legacy 配置合法，成功。

### TC-C05 systemUninstallEntry 校验

用例：

- `installer.systemUninstallEntry.displayName` 缺失，失败。
- `installer.systemUninstallEntry.enabled` 出现，失败。
- `uninstaller.cleanup.systemUninstallEntry` 为字符串，失败。
- `uninstaller.cleanup.legacy.uninstallEntries` 出现，失败。
- legacyEntries 缺 `displayName`，失败。
- legacyEntries scope 为 `auto/any`，失败。

### TC-C06 requireAdmin=false 管理员配置拒绝

用例：

- `installer.defaultDir` 使用 `%ProgramFiles%`，失败。
- `installer.installState.registries[].path` 使用 HKLM，失败。
- `installer.registry.write[].path` 使用 HKLM，失败。
- `installer.systemUninstallEntry.scope=machine/both`，失败。

## 6. PackageManifest 与本地 manifest

### TC-M01 PackageManifest section codec round-trip

步骤：

1. 使用 v3 配置构建 manifest。
2. 执行 `SerializePackageManifest -> DeserializePackageManifest`。

预期：

- identity、install policy、payload、components、ui、lifecycle 全部不丢字段。
- installState 多 registry/file store 保留。
- `installer.cleanup.systemUninstallEntry.legacyEntries` 保留。
- `uninstaller.cleanup.systemUninstallEntry` 保留。

### TC-M02 运行时 metadata 投影正确

预期：

- `ExtendedInstallationMetadata.installState` 来自 v3 manifest。
- `systemUninstallEntry` 来自 `installer.systemUninstallEntry`。
- `installerCleanup` 来自 `installer.cleanup`。
- `uninstallerCleanup` 来自 `uninstaller.cleanup`。
- 旧 `installInfo` 只作为兜底投影，不作为主业务来源。

### TC-M03 install.manifest.json 快照完整

安装完成后检查：

- `app` 包含 id/name/version/publisher。
- `installer.installState` 包含 registry/file stores 和 detect。
- `installer.payload.files` 包含实际释放文件。
- `installer.components` 包含实际选择组件。
- `uninstaller.cleanup.actual` 包含实际创建的 shortcut/startup/systemUninstallEntry。
- `uninstaller.cleanup.systemUninstallEntry.legacyEntries` 保留。
- `selectedComponentIds`、`installAllComponents`、语言、桌面图标、开机启动选择被写入。

### TC-M04 覆盖安装后 manifest 不包含旧 pending 目录

预期：

- 新 manifest files 中不包含 `.mti_delete_pending_*`。
- 只记录本次实际释放文件。

## 7. GUI 安装

### TC-G01 全新 GUI 安装

步骤：

1. 双击 installer。
2. 使用默认安装目录。
3. 保持默认组件和默认选项。
4. 点击安装。

预期：

- 进入欢迎页。
- 安装目录显示为展开后的合法路径。
- 释放文件成功。
- 写入 installState。
- 写入系统卸载项。
- 写入 `install.manifest.json`。
- 创建配置要求的桌面快捷方式、开始菜单快捷方式、启动项。

### TC-G02 GUI 自定义安装目录

步骤：

1. 选择一个不带产品目录名的父目录。

预期：

- UI 自动补齐 `installer.directoryName`。
- 不再使用 app.id 或 app.name 推导目录名。

### TC-G03 GUI 组件选择

步骤：

1. 只选择 core 组件。

预期：

- 只释放 core 对应 payload。
- manifest 只记录被选择组件与文件。

### TC-G04 进度文案按阶段显示

预期：

- 清理旧版本时显示“正在清理”。
- 释放文件时显示“正在释放文件”。
- 长路径被 UI 中间省略。
- tooltip 显示完整路径。

### TC-G05 中文安装路径

步骤：

1. 安装到包含中文的路径，例如 `D:\测试安装\我的应用`。

预期：

- 安装成功。
- manifest 和日志路径无乱码导致的失败。

## 8. 静默安装

### TC-S01 静默全新安装

步骤：

```powershell
setup.exe --silent --destination D:\MTITest\MyDesktopApp
```

预期：

- 无 GUI。
- 安装成功，退出码成功。
- installState、系统卸载项、manifest 写入。

### TC-S02 静默组件选择

步骤：

```powershell
setup.exe --silent --components core
```

预期：

- 只安装指定组件。
- required 组件不可被遗漏。

### TC-S03 静默安装非法路径失败

预期：

- 返回失败退出码。
- 不写入半成品 installState 为 installed。
- 失败路径写入 install_failed 状态。

## 9. 覆盖安装

### TC-O01 发现旧版本并进入覆盖安装

准备：

- installState registry 中已有 `InstallDir`，目录存在。

步骤：

1. 启动 GUI installer。

预期：

- 初始安装目录使用 detect 发现的旧目录。
- 模式为覆盖安装。

### TC-O02 覆盖安装清理旧文件

预期：

- 旧 manifest 可读时按旧 manifest files 清理。
- same-root 覆盖不删除安装根。
- 未记录的用户文件保留。

### TC-O03 旧 manifest 缺失 fallback

配置 `uninstaller.cleanup.missingManifestFallback=safeDirectoryFallback`。

预期：

- 旧目录通过安全校验时清理目录内容。
- 危险目录拒绝。
- 清理失败项跳过，不阻断安装成功。

### TC-O04 旧系统卸载项清理

准备：

- 创建旧卸载项 `DisplayName=Old Desktop App`。
- 配置 `installer.cleanup.systemUninstallEntry.legacyEntries[]`。

预期：

- 覆盖安装前删除该旧系统卸载项。
- 删除只按 DisplayName + scope。

## 10. 升级安装

### TC-UP01 GUI --upgrade 直接进入安装中页面

步骤：

```powershell
setup.exe --upgrade
```

预期：

- 不进入欢迎页。
- detect 命中旧目录后直接进入安装中页面。

### TC-UP02 --upgrade 无旧版本失败

预期：

- detect 找不到旧目录时失败退出。
- GUI 提示无法找到旧版本。

### TC-UP03 --upgrade 不要求旧 manifest

准备：

- detect 可找到旧目录。
- 删除旧目录 `install.manifest.json`。

预期：

- 升级安装仍继续。
- 旧安装参数读取失败时使用当前包默认参数。

### TC-UP04 --upgrade 忽略覆盖参数

步骤：

```powershell
setup.exe --upgrade --silent --destination D:\Other
```

预期：

- 安装目录仍固定为 detect 发现的旧目录。

## 11. 卸载

### TC-R01 GUI 卸载 manifest 分支

步骤：

1. 从安装目录运行 `uninstall.exe`。
2. 确认卸载。

预期：

- 关闭配置进程。
- 按 manifest files 删除安装文件。
- 清理实际创建的 shortcut/startup/systemUninstallEntry。
- 按 cleanup 删除 registry/paths。
- 处理 installState：delete/markUninstalled/keep。
- 卸载完成后没有后台删除进程继续清理业务文件。

### TC-R02 静默卸载

步骤：

```powershell
uninstall.exe --silent
```

预期：

- 无 GUI。
- 行为与 GUI 卸载一致。

### TC-R03 manifest 缺失 fallback 卸载

准备：

- 删除 `install.manifest.json`。
- detect 可找到安装目录。
- 配置 `safeDirectoryFallback`。

预期：

- 危险目录校验通过后清理安装目录。
- 清理 `uninstaller.cleanup.systemUninstallEntry` 当前项和 legacy entries。

### TC-R04 manifest 损坏失败

准备：

- 写入损坏 JSON 到 `install.manifest.json`。

预期：

- 不进入目录级 fallback。
- 卸载失败，避免误删。

### TC-R05 卸载系统卸载项精确匹配

用例：

- key 不同但 DisplayName 匹配，删除。
- key 匹配但 DisplayName 不匹配，不删除。
- scope=user 不删 HKLM。
- scope=both 删除 HKCU 和 HKLM。

## 12. 清理性能与稳定性

### TC-CL01 大量小文件覆盖清理

准备：

- 旧安装目录包含 5 万以上小文件。

预期：

- 清理使用多线程删除。
- 不进入长时间 UI 卡死。
- 超时策略生效时安装继续，日志记录 partial。

### TC-CL02 大量小文件卸载

预期：

- 卸载同步等待清理完成或 watchdog 超时。
- UI 完成后不再有后台业务文件清理。
- 日志有摘要，不写逐文件成功日志。

### TC-CL03 reparse point / symlink / junction

准备：

- 安装目录内放指向外部目录的 reparse point。

预期：

- 清理跳过。
- 不跟随删除外部目标。

### TC-CL04 危险目录保护

用例：

- `C:\`
- `D:\`
- `C:\Windows`
- `C:\Windows\System32`
- `C:\Program Files`
- 用户目录根、Desktop、Documents、Downloads

预期：

- fallback 清理拒绝。

## 13. 权限策略

### TC-PR01 requireAdmin=true

预期：

- packager 写入 installer application manifest `requireAdministrator`。
- 启动 installer 触发 UAC。
- 安装 HKLM、Program Files 场景成功。

### TC-PR02 requireAdmin=false

预期：

- packager 写入 `asInvoker`。
- 配置 HKLM 或 Program Files 时打包阶段失败。
- 安装器运行时不再动态提权重启。

### TC-PR03 卸载提权

预期：

- 卸载器保留现有提权逻辑。
- 提权重启时 `--uninstall-manifest` 上下文可传递。

## 14. 日志

### TC-L01 单日志继承

步骤：

1. 执行安装或卸载，触发清理 worker/子进程。

预期：

- 子进程继承 `MTINSTALLER_LOG_PATH`。
- 不额外创建多份普通日志。

### TC-L02 日志保留策略

准备：

- 创建超过 5 个 `MTInstaller_*.log`。
- 创建超过 3 天的旧 log。

预期：

- 启动后只保留最近 5 个普通 `.log`。
- 超过 3 天的普通 `.log` 删除。
- crash dump 不受普通日志保留策略影响。

### TC-L03 日志级别

步骤：

```powershell
$env:MTINSTALLER_LOG_LEVEL="debug"
setup.exe
```

预期：

- debug 日志可见。
- 默认 info 下 debug 不写入。

## 15. UI 与资源

### TC-UI01 resources.zip 加载

预期：

- UI 资源从嵌入资源解压并加载。
- 不依赖 packager.exe 所在目录 resources。

### TC-UI02 多语言

预期：

- 默认语言按配置。
- 进度阶段前缀显示本地化文本，而不是资源 key。

### TC-UI03 长路径显示

预期：

- 进度条上方路径超过 60 字符时中间省略。
- tooltip 显示完整路径。

## 16. 并发与解压

### TC-T01 统一并发预算

预期：

- payload worker 数由 `InstallerConcurrencyPolicy` 决定。
- cleanup worker 数由统一策略决定。
- CLI/YAML 不影响安装器运行期线程数。

### TC-T02 单 payload folder LZMA 多线程解码

准备：

- 只有一个 payload folder，压缩包足够大。

预期：

- 日志显示 `payloadWorkers=1`。
- LZMA decoder 可使用多线程，例如 `Using multi-threaded XZ decoder`。

### TC-T03 cleanup 与解压资源隔离

预期：

- 覆盖/升级清理同步完成后再释放文件。
- 不再使用 detached 后台删除抢占解压资源。

## 17. Smoke 流程

### TC-E2E01 完整 GUI 安装 -> 覆盖安装 -> 卸载

步骤：

1. 打包。
2. GUI 全新安装。
3. 修改 payload 后重新打包。
4. GUI 覆盖安装。
5. GUI 卸载。

预期：

- 三段流程均成功。
- 覆盖安装后旧文件按策略清理。
- 卸载后安装目录、manifest、系统痕迹按策略清理。

### TC-E2E02 旧版本 V2 -> 当前版本覆盖/升级

配置文件：

- 旧版本：`examples/configurations/acceptance-legacy-v2-packager.yaml`
- 当前版本：`examples/configurations/acceptance-upgrade-target-packager.yaml`

步骤：

1. 使用 legacy v2 配置打包并安装。
2. 使用 upgrade target 配置打包。
3. 执行当前版本 installer 覆盖安装或 `--upgrade`。

预期：

- 当前版本通过 `installer.installState.detect.legacy[legacy_v2]` 发现旧安装目录。
- 覆盖/升级安装目录固定为旧目录。
- `installer.cleanup.systemUninstallEntry.legacyEntries[]` 删除 `DisplayName=MTI Acceptance App Legacy` 的旧系统卸载项。
- 旧 manifest 可读时按旧 manifest files 精确清理旧文件。
- 安装完成后写入当前版本 installState、系统卸载项和本地 manifest。

### TC-E2E03 旧版本 V1 -> 当前版本升级

配置文件：

- 旧版本：`examples/configurations/acceptance-legacy-v1-packager.yaml`
- 当前版本：`examples/configurations/acceptance-upgrade-target-packager.yaml`

步骤：

1. 使用 legacy v1 配置打包并安装。
2. 使用 upgrade target 配置打包。
3. 执行当前版本 `setup.exe --upgrade`。

预期：

- 当前版本通过 `installer.installState.detect.legacy[legacy_v1]` 发现旧安装目录。
- `DisplayName=MTI Acceptance App Legacy Machine` 的旧系统卸载项被清理。
- 当前版本安装成功。

### TC-E2E04 旧 manifest 缺失升级

配置文件：

- 旧版本：`acceptance-legacy-v2-packager.yaml`
- 当前版本：`acceptance-upgrade-target-packager.yaml`

步骤：

1. 安装旧版本。
2. 删除旧安装目录下 `install.manifest.json`。
3. 执行当前版本 `setup.exe --upgrade`。

预期：

- `--upgrade` 不要求旧 manifest 可读。
- detect 命中旧目录后继续升级。
- 清理策略按 `uninstaller.cleanup.missingManifestFallback=safeDirectoryFallback` 执行安全目录 fallback。
- 危险目录仍被拒绝。

### TC-E2E05 静默安装 -> 静默升级 -> 静默卸载

步骤：

1. `setup.exe --silent`
2. `setup.exe --upgrade --silent`
3. `uninstall.exe --silent`

预期：

- 退出码均正确。
- 日志记录关键阶段。

## 18. 验收通过标准

整体通过需满足：

- 自动化构建与 `SchemaRegressionTests` 全部通过。
- v3 示例配置可成功打包。
- GUI/静默安装、覆盖、升级、卸载均完成。
- manifest、installState、系统卸载项、快捷方式、启动项行为符合配置。
- 清理不误删危险目录、不跟随 reparse point。
- 大量文件清理无无限卡死，超时可降级。
- 日志数量受控，子进程不生成重复日志。
- 文档和示例不再提供已废弃配置作为正常用法。
