# YAML + 组件化安装草案（含现有页面嵌入勾选）

## 目标
- 配置文件从 JSON 迁移到 YAML，提升可读性和层级表达能力。
- 组件勾选不仅支持独立组件页，也支持在现有页面（如欢迎页）直接嵌入。
- 组件支持三类来源：`embedded`（内置包内容）、`local`（安装目录子目录脚本/安装器）、`download`（在线下载执行）。
- 安装执行支持按组件选择过滤，并将组件执行进度并入总进度。

## 关键需求映射
- 页面层：通过 XML 定义勾选控件，不硬编码页面。
- 执行层：按勾选结果执行组件安装动作。
- 来源层：支持本地子目录执行和在线下载执行。

## XML 绑定规范（现有页面嵌入）
在任意页面 XML（例如 `resources/skins/welcome_page.xml`）中添加组件勾选控件，示例：

```xml
<CheckBox name="chk_component_core" text="核心组件" selected="true" userdata="component:core" />
<CheckBox name="chk_component_plugins" text="可选插件" selected="true" userdata="component:plugins" />
<CheckBox name="chk_component_cloud" text="云同步组件" selected="false" userdata="component:cloud_sync" />
```

约定：
- `userdata="component:<id>"` 为推荐绑定方式。
- `id` 必须与 YAML 的 `components[].id` 一致。
- `required: true` 的组件在 UI 中应置灰不可取消。

## YAML 顶层结构（建议）
- `schemaVersion`: 配置 schema 版本。
- `package`: 打包信息（AppName、Version、Icon 等）。
- `install`: 安装默认行为。
- `folders`: 输入目录映射。
- `components`: 组件定义。
- `ui`: 页面绑定策略（是否独立组件页、是否从现有页面提取组件勾选）。
- `flow`: 安装/卸载步骤。

## 组件模型
`components[]` 每项建议字段：
- `id`: 组件唯一标识。
- `name`: UI 显示名。
- `description`: UI 描述。
- `required`: 是否必选。
- `defaultSelected`: 默认是否选中。
- `dependsOn`: 依赖组件 id 列表。
- `sizeHintMB`: 大小提示。
- `folders`: 与内置文件安装相关的目录。
- `source`: 组件安装源配置。
- `registry` / `killProcesses` / `createDesktopShortcut` / `autoStartup`: 组件级附加行为（可选）。

### source.type = embedded
- 表示组件内容来自当前安装包中的文件目录（按 `folders` 过滤安装）。

### source.type = local
- 表示从安装目录下的子路径执行本地安装器/脚本。
- 示例字段：
- `source.local.base`: `%InstallDir%\\components`
- `source.local.installer`: `plugins\\install_plugins.bat`
- `source.local.args`: `"/silent"`
- `source.local.wait`: `true`
- `source.local.timeoutSec`: `900`
- 可选卸载命令：`source.local.uninstall`。

### source.type = download
- 表示运行时下载后执行。
- 示例字段：
- `source.download.url`
- `source.download.sha256`
- `source.download.saveAs`（例如 `%InstallDir%\\downloads\\cloud_sync_setup.exe`）
- `source.download.args`
- `source.download.wait`
- `source.download.timeoutSec`
- 可选卸载命令：`source.download.uninstall`。

## UI 配置建议
`ui` 下建议支持：
- `componentSelection.mode`:
- `dedicatedPage`（独立组件页）
- `embeddedInExistingPages`（从现有页面读取）
- `hybrid`（两者都支持）
- `componentSelection.binding.strategy`:
- `xml_userdata`（通过 `userdata="component:<id>"`）
- `componentSelection.binding.tokenPrefix`:
- 默认 `component:`
- `componentSelection.binding.pages[]`:
- 允许显式列出需要扫描的页面与控件名，便于限制范围。

## 流程模型
`flow.install` / `flow.uninstall` 均为步骤数组，每步字段：
- `id`, `action`, `enabled`, `weight`, `onError`, `when`, `params`

建议新增动作：
- `CollectComponentSelection`：从 UI 控件读取勾选结果。
- `ResolveComponents`：处理 required/dependsOn 后得到最终集合。
- `InstallSelectedComponents`：执行组件安装（embedded/local/download）。

## C++ 数据结构草案（增量）
建议在 `include/common/types.h` 扩展：

```cpp
enum class ComponentSourceType {
    Embedded,
    Local,
    Download
};

struct LocalInstallerConfig {
    std::string base;
    std::string installer;
    std::string args;
    bool wait = true;
    uint32_t timeoutSec = 900;
    std::string uninstall;
};

struct DownloadInstallerConfig {
    std::string url;
    std::string sha256;
    std::string saveAs;
    std::string args;
    bool wait = true;
    uint32_t timeoutSec = 1800;
    std::string uninstall;
};

struct ComponentSourceConfig {
    ComponentSourceType type = ComponentSourceType::Embedded;
    LocalInstallerConfig local;
    DownloadInstallerConfig download;
};

struct ComponentConfig {
    std::string id;
    std::string name;
    std::string description;
    bool required = false;
    bool defaultSelected = true;
    uint32_t sizeHintMB = 0;
    std::vector<std::string> dependsOn;
    std::vector<std::string> folders;
    ComponentSourceConfig source;
    std::vector<RegistryEntry> registry;
    std::vector<std::string> killProcesses;
    bool createDesktopShortcut = false;
    bool autoStartup = false;
};

struct UiComponentBindingPage {
    std::string skin;
    std::vector<std::string> controls;
};

struct UiComponentSelectionConfig {
    std::string mode;              // dedicatedPage | embeddedInExistingPages | hybrid
    std::string strategy;          // xml_userdata
    std::string tokenPrefix;       // component:
    std::vector<UiComponentBindingPage> pages;
};
```

并在 `PackagerConfiguration` 增加：
- `std::vector<ComponentConfig> components;`
- `UiComponentSelectionConfig componentUi;`

## 元数据版本建议
- `Constants::VERSION` 从 `12` 升到 `13`。
- 追加字段：
- `componentsCount + components[]`
- `componentUiConfig`
- `installFlow/uninstallFlow`
- 安装端按版本分支解析，保证旧包可安装。

## 执行层改造要点
- `InstallService` 从固定流程切换到步骤调度器。
- 新增 `selectedComponents` 输入（来自 UI/静默参数）。
- `InstallSelectedComponents` 支持：
- `embedded`: 按组件目录过滤安装内容。
- `local`: 从 `%InstallDir%` 子目录执行 `exe/msi/bat/cmd`。
- `download`: 下载 -> 哈希校验 -> 执行。
- 组件执行结果写入 manifest，卸载按记录执行。

## 安全策略（必须）
- `download` 必须 `https + sha256`。
- `local` 仅允许 `%InstallDir%` 子目录，不允许路径越界。
- 脚本执行默认关闭，需显式 `allowScriptInstallers: true`。
- 每组件支持 `onError: fail|continue`，并落日志。

## 分阶段实施建议
1. YAML 解析替代 JSON（不改安装行为）。
2. 引入组件模型并打包进元数据（不改 UI）。
3. 实现现有页面 XML 勾选绑定 + 组件选择透传。
4. 实现 `InstallSelectedComponents`（embedded/local/download）。
5. 补卸载链路、哈希校验、回归测试。
## 2026-02-16 Implementation Progress Snapshot

Implemented in installer runtime:

- Component selection plan resolution (`required/defaultSelected/dependsOn`)
- Embedded folder extraction filtering by selected components
- Local installer component execution (`source.type=local`)
- Downloaded installer component execution with SHA256 verification (`source.type=download`)
- Effective side-effects merge from selected components:
  - registry entries
  - kill process list
  - auto startup and desktop shortcut flags

Current gap to close next:

- GUI page embedded checkbox binding (`userdata=component:<id>`) and selection pass-through
- Uninstall replay for local/download component actions

## 2026-02-16 C5 Snapshot

Implemented GUI embedded component binding:

- Reads component checkbox binding from existing pages (`userdata=component:<id>`)
- Supports page/control scope restriction from `ui.componentSelection.binding.pages`
- Enforces required components as checked+disabled
- Passes selected component ids into installation service

This closes C5 baseline for embedded-page selection and runtime pass-through.

## 2026-02-16 C6 Snapshot

Implemented uninstall replay for component actions:

- Installer writes selected local/download component uninstall replay records into manifest.
- Uninstaller replays component uninstall commands (with timeout/cancel checks) before file cleanup.
- Old manifests without replay section remain supported.

## 2026-02-16 C7 Snapshot

Testing updates completed for current implementation:

- Added manifest round-trip assertions for `componentActions`
- Added backward-compatibility assertion for legacy manifests (without `componentActions`)
- Ran test suite in `BUILD_TESTS=ON` build; all tests passed

## 2026-02-16 C7.1 Snapshot

Added parser-focused test coverage:

- `test_console_component_args` covers component CLI flag parsing
- Confirms trimming and multi-source argument combination behavior
- Full current test set passes (`3/3`)

## 2026-02-16 C7.2 Snapshot

Added uninstall replay behavior test coverage:

- `test_uninstall_component_replay` validates manifest-driven replay command execution
- Confirms replay side-effect and uninstall manifest cleanup behavior
- Full current test set passes (`4/4`)

## 2026-02-16 C7.3 Snapshot

Added config + validator security regression coverage and troubleshooting guide:

- `test_config_yaml_json_equivalence`
  - validates JSON legacy config and structured YAML config parse to equivalent runtime config
  - validates config discovery precedence (`packager.yaml` > `packager.json`)
- `test_configuration_validator_components`
  - validates local path boundary checks and parent traversal rejection
  - validates download security requirements (`https://` + 64-char SHA256)
  - validates dependency cycle rejection and valid graph acceptance
- Added `docs/components_troubleshooting_guide.md` and linked from configuration reference

## 2026-02-17 C7.4 Snapshot

Build dependency strategy updated for `yaml-cpp`:

- CMake now prefers pinned repository source at `third_party/yaml-cpp` (`add_subdirectory`)
- Default behavior no longer auto-fetches into `build/_deps`
- Added guarded fallback option:
  - `-DYAML_CPP_FETCH_FALLBACK=ON` only when temporary fallback is explicitly needed

## 2026-02-17 C7.5 Snapshot

`yaml-cpp` source and version alignment update:

- Submodule URL switched back to official source:
  - `https://github.com/jbeder/yaml-cpp.git`
- Submodule version updated to:
  - `yaml-cpp-0.9.0`
- CMake fallback pin updated to:
  - `GIT_TAG yaml-cpp-0.9.0`
