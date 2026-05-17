# packager.yaml v3 重构任务拆分

## 目标

依据 [packager-yaml-v3-implementation-plan.md](./packager-yaml-v3-implementation-plan.md) 分阶段落地 `schemaVersion: 3`。

本轮不兼容 v2：

- 打包器只接受 `schemaVersion: 3`。
- 旧 v2 配置直接报错。
- 安装器和卸载器只读取新 manifest，不保留旧字段 fallback。

## 阶段 0：准备与边界确认

### 任务

- [x] 确认 `docs/packager-yaml-v3.example.yaml` 是 v3 配置目标样例。
- [x] 确认 `docs/packager-yaml-v3-implementation-plan.md` 是落地方案基准。
- [x] 记录当前所有 v2 测试依赖点，作为后续删除或重写清单。
- [x] 确认不再保留 v2 loader、v2 metadata 兼容读取和 v2 docs 示例。

### 验收

- [x] 形成 v2 依赖清单。
- [x] 后续任务不再以 v2 兼容为约束。

## 阶段 1：配置模型重定义

### 任务

- [x] 重定义 `PackagerConfiguration` 为四块结构：
  - `app`
  - `package`
  - `installer`
  - `uninstaller`
- [x] 新增或重构以下配置结构：
  - `AppConfig`
  - `PackageConfig`
  - `InstallerConfig`
  - `UninstallerConfig`
  - `InstallerDefaultsConfig`
  - `InstallerUiConfig`
  - `UninstallerUiConfig`
  - `PayloadConfig`
  - `ComponentConfig`
  - `SystemUninstallEntryConfig`
  - `InstalledInstanceDetectConfig`
  - `UninstallerCleanupConfig`
  - `InstallStateConfig`
  - `InstallStateRegistryStoreConfig`
  - `InstallStateFileStoreConfig`
  - `InstallStateValueConfig`
- [x] 移除或隔离旧 v2 专用结构：
  - `install`
  - `ui`
  - `layout`
  - `lifecycle`
  - `installInfo`
  - `layout.folders.destination`

### 验收

- [x] 项目可编译到配置模型层。
- [x] 没有业务代码继续直接依赖旧顶层结构。

备注：M1 为控制改动面，旧 `install/ui/layout/lifecycle/installInfo` 结构暂作为现有 manifest builder 的桥接投影保留，不再作为 v3 loader/validator/packager 主路径输入。

## 阶段 2：v3 ConfigurationLoader

### 任务

- [x] 修改 `ConfigurationLoader::parseConfigObject()`，只接受 `schemaVersion: 3`。
- [x] 非 v3 schema 返回明确错误：
  - `Unsupported schemaVersion. Only schemaVersion 3 is supported.`
- [x] 新增解析函数：
  - `ParseV3App`
  - `ParseV3Package`
  - `ParseV3Installer`
  - `ParseV3Uninstaller`
  - `ParseV3InstallState`
  - `ParseV3Payload`
  - `ParseV3Components`
  - `ParseV3SystemUninstallEntry`
  - `ParseV3UninstallerCleanup`
  - `ParseV3Detect`
- [x] 删除旧字段解析入口：
  - `ParseInstallConfig`
  - `ParseUiConfig`
  - `ParseLayoutConfig`
  - `ParseLifecycleConfig`
  - 或将其移出主构建目标。

### 验收

- [x] v3 示例配置可成功加载。
- [x] v2 示例配置会失败且错误明确。
- [x] 配置错误路径能指出具体字段。

备注：旧字段解析函数暂留在源码中作为非主路径历史代码，`parseConfigObject()` 已只走 v3 解析并拒绝 v2。

## 阶段 3：配置校验重写

### 任务

- [x] 重写 `ConfigurationValidator` 以 v3 字段为准。
- [x] 校验 `app.id/name/version` 必填。
- [x] 校验 `installer.defaultDir` 必填。
- [x] 校验 `installer.payload[].id/source/target` 必填且 id 唯一。
- [x] 校验 `installer.payload[].source` 相对 input directory 存在。
- [x] 校验 `installer.components[].payload[]` 引用已声明 payload id。
- [x] 校验组件依赖无环。
- [x] 校验 `installer.requireAdmin=false` 时拒绝明显需要管理员权限的配置：
  - `%ProgramFiles%`
  - HKLM registry write
  - machine uninstall entry
- [x] 校验 `installer.installState.registries[]`：
  - id 唯一
  - path 必填
  - values 非空
  - registry value type 合法
- [x] 校验 `installer.installState.files[]`：
  - id 唯一
  - path 必填
  - format 支持 `json`
  - values 非空
- [x] 校验 `installer.installState.detect.primary/legacy` 能解析出安装目录来源。
- [x] 校验 `uninstaller.cleanup.paths[]` 结构合法。

### 验收

- [x] v3 合法配置通过校验。
- [x] 关键非法配置均有测试覆盖。

## 阶段 4：打包器主流程适配 v3

### 任务

- [x] 更新 `packager/main.cpp` 使用 v3 配置字段。
- [x] 图标路径改为 `app.icon`。
- [x] 版本资源改为 `app.versionInfo`。
- [x] 权限 manifest 使用 `installer.requireAdmin`。
- [x] 压缩配置使用 `package.compression`。
- [x] payload 扫描使用 `installer.payload[]`。
- [x] 移除旧 `layout.folders` 扫描路径。
- [x] 移除旧 `layout.components` 主路径依赖。

### 验收

- [x] 使用 v3 示例配置可以完成打包。
- [x] 旧配置不能打包。

备注：M1 主流程已改为 `installer.payload[]` 驱动扫描；旧 `layout.*` 仍作为桥接投影供现有 manifest builder 过渡使用。

## 阶段 5：metadata / PackageManifest 生成适配

### 任务

- [x] 修改 `PackageManifestBuilder` 从 v3 `PackagerConfiguration` 构建 manifest。
- [x] 写入 `app` 身份信息。
- [x] 写入 `installer` policy：
  - defaultDir
  - requireAdmin
  - minWindows
  - mutex
  - killBeforeInstall
  - defaults
  - installState
  - systemUninstallEntry
  - registry.write
- [x] 写入 payload manifest：
  - payload id
  - source
  - target
  - required
  - file index
- [x] 写入 components manifest：
  - id/name/description
  - required/defaultSelected
  - dependsOn
  - payload refs
  - install action
  - uninstall action
- [x] 写入 uninstaller policy：
  - requireAdmin
  - detect
  - killBeforeUninstall
  - cleanup
  - ui
- [x] 删除旧 metadata 字段映射：
  - `installInfo`
  - `layoutComponents`
  - `lifecycleUpgradeCleanup`
  - `lifecycleUninstallCleanup`
  - 或将其迁移为新 manifest 子结构。

### 验收

- [x] manifest codec round-trip 覆盖 v3 字段。
- [x] v3 自定义 installState values 不丢失。

备注：M2 已将旧 metadata 映射收敛为兼容投影，`PackageManifestBuilder` 不再通过 `MetadataGenerator` 反向构建 manifest；安装/卸载执行链路仍在后续阶段迁移。

## 阶段 6：installState 多存储实现

### 任务

- [x] 新增 `install_state_store` 模块。
- [x] 实现 token 展开：
  - `%InstallDir%`
  - `%Version%`
  - `%AppName%`
  - `%AppId%`
  - `%InstallState%`
  - `%UserName%`
  - `%InstallSource%`
- [x] 实现 `applyInstallState()`：
  - 写多个 registry stores。
  - 写多个 json file stores。
  - 支持自定义 values。
- [x] 实现 `cleanupInstallState()`：
  - `delete`
  - `markUninstalled`
  - `keep`
- [x] 替换旧 `applyCoreInstallInfo()` 主流程调用。
- [x] 替换旧 `removeInstallInfoArtifacts()`。

备注：阶段 6 已实现 v3 installState 在安装和卸载主流程中的 registry/file 多存储写入与清理；旧 installInfo 仍保留为兼容投影与 fallback。

补充：安装目录发现已迁移到 `installer.installState.detect`，`legacy[]` 用于按顺序发现历史版本安装目录；主 `primary` 仍优先。

### 验收

- [x] 安装时 registry/file installState 均被写入。
- [x] 卸载时 `delete` 删除 registry/file。
- [x] 卸载时 `markUninstalled` 正确写入状态。

## 阶段 7：安装流程适配 v3

### 任务

- [x] 安装默认目录读取 `installer.defaultDir`。
- [x] 互斥量读取 `installer.mutex`。
- [x] 安装前关闭进程读取 `installer.killBeforeInstall`。
- [x] 默认 UI 选择读取 `installer.defaults`。
- [x] payload 解压使用 `installer.payload`。
- [x] 组件安装使用 `installer.components[].install`。
- [x] 安装时 registry 写入使用 `installer.registry.write`。
- [x] 系统卸载项写入使用 `installer.systemUninstallEntry`。
- [x] 安装中/成功/失败状态写入使用 `applyInstallState()`。

### 验收

- [x] GUI 安装可进入安装流程。
- [x] 静默安装可完成。
- [x] 组件选择影响 payload 与组件安装动作。
- [x] 安装完成 manifest 包含实际安装快照。

## 阶段 8：升级/覆盖发现与清理适配 v3

### 任务

- [x] 新增统一安装实例发现入口：
  - `ResolveInstalledInstanceFromInstallState`
  - `ResolveInstallDirFromInstallStateStore`
- [x] 覆盖安装发现使用 `installer.installState.detect`。
- [x] `--upgrade` 发现使用 `installer.installState.detect`，旧 manifest 不可读时使用当前默认安装参数。
- [x] manifest 可读时按旧 manifest files 清理旧文件。
- [x] manifest 缺失时按 `uninstaller.cleanup.missingManifestFallback` 决定是否安全 fallback。
- [x] 清理逻辑继续保持：
  - 不 rename 整个安装根。
  - 只处理安装根下子目录或 manifest 文件。
  - 多线程同步删除。

### 验收

- [x] 覆盖安装能发现旧安装目录。
- [x] `--upgrade` 能从 installState 发现旧安装目录。
- [x] 旧 manifest 文件清理完整。
- [x] manifest 缺失 fallback 受危险目录保护。

## 阶段 9：卸载流程适配 v3

### 任务

- [x] 卸载上下文解析优先使用 manifest 快照；manifest 缺失 fallback 借用 `installer.installState.detect`。
- [x] 卸载前关闭进程读取 `uninstaller.killBeforeUninstall`。
- [x] 文件清理读取 `uninstaller.cleanup.installedFiles`。
- [x] installState 清理读取 `uninstaller.cleanup.installState`。
- [x] 桌面快捷方式、启动项、系统卸载项 `auto` 从 manifest 实际安装结果清理。
- [x] legacy 清理读取：
  - `uninstaller.cleanup.legacy.desktopShortcutNames`
  - `uninstaller.cleanup.legacy.startupNames`
  - `uninstaller.cleanup.systemUninstallEntry.legacyEntries`
- [x] registry 清理读取：
  - `deleteKeys`
  - `deleteValues`
- [x] extra paths 清理读取 `uninstaller.cleanup.paths`。
- [x] 组件卸载动作读取 manifest 快照中的组件 uninstall action。

### 验收

- [x] manifest 可读卸载完整清理。
- [x] manifest 缺失 fallback 能安全卸载。
- [x] 卸载完成后没有后台删除继续运行。
- [x] registry/file installState 按配置清理。

## 阶段 10：manifest 写入与卸载快照

### 任务

- [x] 安装完成 manifest 写入：
  - app
  - installer.installState
  - installer.systemUninstallEntry
  - installer.defaults 实际选择
  - payload 实际释放文件
  - components 实际选择与执行结果
  - installer.installState.detect
  - uninstaller.cleanup
  - 实际创建的桌面快捷方式
  - 实际创建的开机启动项
  - 实际写入的系统卸载项
- [x] 卸载只消费 manifest 快照。
- [x] 不依赖当前 installer metadata 做卸载主逻辑。

### 验收

- [x] manifest 缺字段时卸载明确失败或进入安全 fallback。
- [x] 不同版本配置变化不会影响旧版本卸载。

## 阶段 11：文档与示例替换

### 任务

- [x] 将 `docs/packager-yaml-v3.example.yaml` 作为正式示例。
- [x] 替换 `examples/packager.yaml` 为 v3。
- [x] 更新 `docs/USER_GUIDE.md`。
- [x] 更新 `docs/打包器流程图.md`。
- [x] 更新 `docs/安装器流程图.md`。
- [x] 新增 v2 不兼容说明。

### 验收

- [x] 文档不再介绍 v2 字段。
- [x] 示例配置能直接打包。

## 阶段 12：测试收敛

### 任务

- [x] 删除 v2 loader 测试。
- [x] 重写 `MinimalValidYaml()` 为 v3。
- [x] 更新 metadata round-trip 测试。
- [x] 更新 package manifest codec 测试。
- [x] 更新 install plan 测试。
- [x] 更新 upgrade cleanup 测试。
- [x] 更新 uninstall context/fallback 测试。
- [x] 新增 installState 多注册表/多文件测试。
- [x] 新增 v2 被拒绝测试。

### 验收

- [x] `cmake --build build_codex --config Release` 通过。
- [x] `build_codex\test\Release\SchemaRegressionTests.exe` 通过。
- [x] 手工使用 v3 示例完成打包 smoke。

## 里程碑建议

### M1：打包器可读取 v3

- [x] 阶段 0-4 完成。
- [x] v3 yaml 可加载、校验、进入 packager 主流程。

### M2：manifest 可表达 v3

- [x] 阶段 5 完成。
- [x] v3 installState 和 uninstall policy 可 round-trip。
- [x] 阶段 6 执行链路完成。

### M3：安装流程可用

- [x] 阶段 7-8 完成。
- [x] GUI/静默安装、覆盖/升级安装可用。

### M4：卸载流程可用

- [x] 阶段 9-10 完成。
- [x] manifest 卸载和 fallback 卸载可用。

### M5：文档与测试收敛

- [x] 阶段 11-12 完成。
- [x] v2 从主文档、示例和测试中移除。
