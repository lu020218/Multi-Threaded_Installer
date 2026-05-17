# packager.yaml v3 不兼容重构落地方案

## 目标

将 `schemaVersion: 3` 作为唯一支持的打包配置协议，不再兼容 v2。

新的配置结构固定为：

```yaml
schemaVersion: 3
app: {}
package: {}
installer: {}
uninstaller: {}
```

目标是让配置职责边界清晰：

- `app`：产品身份、版本、发布者等产品信息。
- `package`：打包与压缩行为。
- `installer`：安装阶段配置，包括安装策略、UI、payload、组件、安装状态持久化。
- `uninstaller`：卸载阶段配置，包括安装目录发现、清理、UI、组件回滚。

## 总体策略

- `ConfigurationLoader` 只接受 `schemaVersion: 3`。
- 删除或隔离 v2 字段解析逻辑，不再支持旧的 `install/ui/layout/lifecycle` 顶层结构。
- 内部 `PackagerConfiguration` 直接改成 v3 语义。
- 打包器生成的新 metadata/manifest 只包含 v3 语义。
- 安装器和卸载器只读取新 manifest，不做旧字段 fallback。

## 配置模型

重定义 `PackagerConfiguration`：

```cpp
struct PackagerConfiguration {
    AppConfig app;
    PackageConfig package;
    InstallerConfig installer;
    UninstallerConfig uninstaller;
};
```

核心子结构：

```cpp
struct InstallStateConfig {
    std::vector<InstallStateRegistryStore> registries;
    std::vector<InstallStateFileStore> files;
};

struct InstallerConfig {
    bool requireAdmin = false;
    std::string defaultDir;
    Version minWindows;
    uint64_t largeFileThresholdBytes = 0;
    std::string mutex;
    std::vector<std::string> killBeforeInstall;
    InstallerDefaults defaults;
    InstallStateConfig installState;
    SystemUninstallEntryConfig systemUninstallEntry;
    InstallerUiConfig ui;
    std::vector<PayloadConfig> payload;
    std::vector<ComponentConfig> components;
    RegistryWriteConfig registry;
};

struct UninstallerConfig {
    bool requireAdmin = false;
    InstalledInstanceDetectConfig detect;
    std::vector<std::string> killBeforeUninstall;
    UninstallerCleanupConfig cleanup;
    UninstallerUiConfig ui;
};
```

## 配置解析

`ConfigurationLoader::parseConfigObject()` 改为只分发 v3：

```cpp
schemaVersion = readRequiredInt(root, "schemaVersion");
if (schemaVersion != 3) {
    error = "Unsupported schemaVersion. Only schemaVersion 3 is supported.";
    return std::nullopt;
}

ParseApp(root["app"]);
ParsePackage(root["package"]);
ParseInstaller(root["installer"]);
ParseUninstaller(root["uninstaller"]);
```

不再解析：

- `install`
- `ui`
- `layout`
- `lifecycle`
- `install.installInfo`
- `layout.folders.destination`

## v3 字段到打包结果的映射

- `installer.payload[]` 生成 payload manifest。
- `installer.components[]` 生成 component manifest。
- `installer.installState` 写入 install policy。
- `uninstaller.cleanup` 写入 uninstall policy。
- `installer.installState.detect` 写入 discovery policy。
- `installer.registry.write` 写入安装时 registry actions。
- `installer.systemUninstallEntry` 写入系统卸载项策略。

## 安装器行为

安装器读取新 metadata 后使用以下字段：

- 默认安装目录：`installer.defaultDir`
- 权限策略：由 packager 写入 exe manifest，安装器不再动态判断。
- 互斥量：`installer.mutex`
- 安装前关闭进程：`installer.killBeforeInstall`
- 默认安装选项：`installer.defaults.autoStartup`、`installer.defaults.desktopShortcut`
- payload：`installer.payload`
- components：`installer.components`
- 安装状态持久化：`installer.installState`
- 安装时注册表写入：`installer.registry.write`
- 系统卸载项：`installer.systemUninstallEntry`

安装状态写入统一通过：

```cpp
applyInstallState(installer.installState, state=installing);
applyInstallState(installer.installState, state=installed);
applyInstallState(installer.installState, state=install_failed);
```

## 卸载器行为

卸载流程优先读取 `install.manifest.json`。

使用字段：

- 安装目录发现：`installer.installState.detect`
- 卸载前关闭进程：`uninstaller.killBeforeUninstall`
- 文件清理：`uninstaller.cleanup.installedFiles`
- manifest 缺失降级：`uninstaller.cleanup.missingManifestFallback`
- installState 清理：`uninstaller.cleanup.installState`
- 快捷方式、启动项、系统卸载项：`auto` 模式读取 manifest 中的实际安装结果。
- registry 清理：`uninstaller.cleanup.registry.deleteKeys/deleteValues`
- extra paths：`uninstaller.cleanup.paths`
- 组件卸载：manifest 快照中的组件卸载动作。

卸载状态清理统一通过：

```cpp
cleanupInstallState(uninstaller.cleanup.installState);
```

## installState 新实现

替换旧 `InstallInfoConfig`，新增通用安装状态持久化模型。

```cpp
struct InstallStateValueConfig {
    std::string name;
    std::string key;
    std::string value;
    RegistryValueType type;
};

struct InstallStateRegistryStoreConfig {
    std::string id;
    std::string path;
    std::map<std::string, InstallStateValueConfig> values;
};

struct InstallStateFileStoreConfig {
    std::string id;
    std::string path;
    std::string format;
    std::map<std::string, InstallStateValueConfig> values;
};

struct InstallStateConfig {
    std::vector<InstallStateRegistryStoreConfig> registries;
    std::vector<InstallStateFileStoreConfig> files;
};
```

执行接口：

```cpp
bool applyInstallState(
    const InstallStateConfig& config,
    const InstallStateContext& context
);

bool cleanupInstallState(
    const InstallStateConfig& config,
    InstallStateCleanupMode mode,
    const InstallStateContext& context
);
```

支持能力：

- 多 registry stores。
- 多 file stores。
- 自定义 values。
- token 展开：
  - `%InstallDir%`
  - `%Version%`
  - `%AppName%`
  - `%AppId%`
  - `%InstallState%`
  - `%UserName%`
  - `%InstallSource%`

## manifest 写入

`install.manifest.json` 必须保存安装结果快照：

- `app`
- `installer.installState`
- `installer.systemUninstallEntry`
- `installer.defaults` 的实际选择结果
- `installer.payload` 实际释放文件列表
- `installer.components` 实际选择与执行结果
- `installer.installState.detect`
- `uninstaller.cleanup`
- 实际创建的桌面快捷方式
- 实际创建的开机启动项
- 实际写入的系统卸载项

卸载流程必须基于 manifest 快照执行，不依赖当前打包器配置。

## 构建与测试更新

需要同步修改：

- `examples/packager.yaml` 改成 v3。
- `docs/packager-yaml-v3.example.yaml` 作为正式示例。
- `SchemaRegressionTests` 中旧 v2 yaml 全部替换成 v3。
- 删除或重写 v2 loader 测试。
- `docs/打包器流程图.md` 更新为 v3。
- `docs/安装器流程图.md` 更新为 v3 manifest/installState 逻辑。

## 测试计划

新增或更新回归测试：

- v3 基础 yaml 可加载。
- 非 v3 schema 被明确拒绝。
- `installer.installState.registries[]` 多注册表解析正确。
- `installer.installState.files[]` 多文件解析正确。
- installState 自定义 values round-trip。
- 安装时 registry/file installState 都被写入。
- 卸载 `installState: delete` 删除 registry/file。
- 卸载 `installState: markUninstalled` 写入 `uninstalled`。
- `installer.installState.detect.primary/legacy` 能发现安装目录。
- payload target 展开正确。
- component install/uninstall 命令进入 manifest 快照。
- manifest 缺失 fallback 仍受危险目录保护。

## 推荐落地顺序

1. 重定义配置结构和 loader，只接受 v3。
2. 修改 packager metadata builder，让 v3 配置能生成新 manifest。
3. 实现 installState registry/file 多存储。
4. 修改安装执行链路读取新字段。
5. 修改卸载执行链路读取 manifest 快照中的新字段。
6. 替换 examples、docs、tests。
7. 删除 v2 解析死代码。

## 风险点

- 改动范围较大，必须分阶段保证可编译。
- v2 不兼容后，旧配置会直接打包失败。
- installState 多存储会影响安装发现、升级、卸载 fallback，需要统一入口。
- manifest 快照字段必须完整，否则卸载会丢失上下文。
