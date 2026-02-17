# YAML + 组件化安装落地 TODO 计划

## 实施进展（2026-02-16）
- 已完成 C1：配置层 `JSON + YAML` 双栈接入。
- 已完成 C2：元数据 `v13` 组件字段落地，并兼容 `v12` 读取。
- 已完成 C3：组件配置校验与关键安全约束（下载/本地路径）落地。
- 已更新配置文档：`docs/configuration_reference.md` 增加 YAML 支持说明与优先级。
- 下一步进入 C4：安装执行层动作化与组件执行管线。

## 1. 目标与范围

### 1.1 目标
- 将配置输入从仅 `JSON` 扩展为 `JSON + YAML`（先双栈，后可切换默认）。
- 落地组件化安装能力：`embedded` / `local` / `download`。
- 支持“现有页面嵌入勾选”绑定（`userdata="component:<id>"`）。
- 将组件执行纳入安装流程与进度，并支持卸载回放。
- 保持旧安装包可安装（元数据向后兼容）。

### 1.2 非目标（本轮不做）
- 不重做整套 GUI 视觉样式。
- 不引入远程配置中心。
- 不在首版实现过于复杂的条件表达式引擎（`when` 仅支持最小子集）。

## 2. 当前基线（用于对齐）
- 配置读取仅支持 `packager.json/.packager.json`，并走 `parseJsonConfig`。
- `Constants::VERSION` 当前为 `12`。
- 安装服务为固定阶段流程（Precheck/Cleanup/Installing/Finalizing）。
- GUI 安装页仅透传 `autoRun/desktopIcons/language/cleanupOldInstall`，未透传组件选择。
- 仓库已有 YAML 草案与示例，但未接入运行时代码。

## 3. 里程碑总览

### M0. 基线保护与设计收敛
- [ ] 冻结草案字段：确认首版必做字段与可延期字段。
- [ ] 制定兼容策略：`v12` 读写不变，新增 `v13` 字段可选。
- [ ] 约定 feature flag：`--enable-components`（默认开启或关闭需决策）。
- [ ] 新增“最小可跑通”样例配置（YAML + 组件 + 页面绑定）。

验收标准：
- 评审通过一份“字段冻结表 + 兼容矩阵 + 版本策略”。

---

### M1. 配置层：YAML 双栈接入
- [x] 引入 YAML 解析依赖（`yaml-cpp`）。
- [x] 扩展配置发现顺序：`PACKAGER_CONFIG` / `packager.yaml` / `packager.yml` / `packager.json` / `.packager.json`。
- [x] 新增 `parseYamlConfig`，字段与 JSON 对齐映射。
- [x] 统一配置中间模型，避免 JSON/YAML 两套业务分支。
- [x] 输出清晰错误信息（字段路径、类型、建议值）。

建议改动文件：
- `include/packager/configuration_loader.h`
- `src/packager/configuration_loader.cpp`
- `CMakeLists.txt`
- `docs/configuration_reference.md`

验收标准：
- 同一配置语义下，JSON 与 YAML 产出完全一致的 `PackagerConfiguration`。
- 解析失败报错包含文件名 + 字段路径。

---

### M2. 数据模型与元数据版本升级（v13）
- [x] 在 `types.h` 增加组件结构。
- [x] `ComponentSourceType`、`LocalInstallerConfig`、`DownloadInstallerConfig`。
- [x] `ComponentConfig`、`UiComponentSelectionConfig`。
- [x] 在 `PackagerConfiguration` 增加 `components` 与 `componentUi`。
- [x] 在 `ExtendedInstallationMetadata` 增加组件相关序列化字段。
- [x] `Constants::VERSION` 从 `12` 升到 `13`。
- [x] 解析器兼容 `v12`（缺失组件字段时使用默认空配置）。

建议改动文件：
- `include/common/types.h`
- `src/packager/metadata_generator.cpp`
- `src/installer/metadata_parser.cpp`

验收标准：
- `v12` 安装包可继续安装。
- `v13` 可正确读写组件与 UI 绑定配置。

---

### M3. 打包器层：组件配置校验与入包
- [x] 增加组件配置校验。
- [x] `id` 唯一、依赖闭包有效、`required` 不可被默认取消。
- [x] `source.local` 路径必须限制在 `%InstallDir%` 子路径。
- [x] `source.download` 必须 `https + sha256`。
- [x] 组件与 `folders` 映射关系校验。
- [x] 将组件字段写入元数据（打包阶段）。

建议改动文件：
- `include/packager/configuration_validator.h`
- `src/packager/configuration_validator.cpp`
- `src/packager/main.cpp`

验收标准：
- 非法组件配置在打包期阻断，且报错可定位。

---

### M4. 安装执行层：流程动作化（最小可用）
- [ ] 抽象安装步骤调度器（在现有 `InstallService` 上增量改造）。
- [ ] 首版支持动作：
- [ ] `CollectComponentSelection`
- [ ] `ResolveComponents`
- [ ] `InstallSelectedComponents`
- [ ] `ExtractFiles` 保留并支持按组件文件夹过滤（`embedded`）。
- [ ] 进度权重合并：组件执行进度并入总进度。

建议改动文件：
- `include/installer/install_service.h`
- `src/installer/install_service.cpp`
- `include/common/installer_parallel_install.h`
- `src/common/installer_parallel_install.cpp`

验收标准：
- 相同勾选输入下，安装结果稳定可复现。
- 未选组件不应落盘其文件/副作用。

---

### M5. 组件安装器执行器（local/download）
- [ ] 新增进程执行器（支持 `wait/timeoutSec/args`）。
- [ ] `local`：执行路径防越界（规范化后必须在 `%InstallDir%` 下）。
- [ ] `download`：下载、SHA256 校验、执行、失败回滚策略。
- [ ] 每组件支持 `onError: fail|continue`。
- [ ] 审计日志：记录组件开始/结束/耗时/退出码。

建议改动文件：
- `src/installer/install_service.cpp`
- `src/installer/installer_helpers.cpp`
- `include/installer/installer_helpers.h`

验收标准：
- 恶意路径、哈希不匹配、超时等场景均被正确拦截。

---

### M6. GUI 绑定：现有页面嵌入勾选
- [ ] 定义绑定扫描逻辑：根据 `componentUi.binding.pages` 限定范围。
- [ ] 支持 `userdata="component:<id>"` token 解析。
- [ ] `required=true` 组件置灰并强制选中。
- [ ] 勾选结果透传到 `PageController -> InstallationWorker -> InstallService`。
- [ ] 若缺失控件或 token 不匹配，提供可诊断错误并降级策略。

建议改动文件：
- `src/gui/gui_manager.cpp`
- `include/gui/page_controller.h`
- `src/gui/page_controller.cpp`
- `include/gui/installation_worker.h`
- `src/gui/installation_worker.cpp`
- `resources/skins/welcome_page.xml`（示例控件）

验收标准：
- 页面内勾选变化可直接影响安装结果。
- `required` 组件无法取消且不会被误卸载。

---

### M7. 卸载链路与 manifest 扩展
- [ ] manifest 增加组件执行记录（已安装组件、来源、卸载命令、结果）。
- [ ] 卸载优先按组件记录回放，再做文件清理。
- [ ] 保持旧 manifest 读取兼容。

建议改动文件：
- `include/installer/uninstall_manager.h`
- `src/installer/uninstall_manager.cpp`
- `tests/test_manifest_roundtrip.cpp`

验收标准：
- 卸载可准确覆盖 `local/download` 组件副作用。

---

### M8. 测试、文档、发布门禁
- [ ] 单元测试：
- [ ] YAML/JSON 等价解析
- [ ] 组件依赖解析
- [ ] 路径防越界与哈希校验
- [ ] 集成测试：
- [ ] 页面勾选 -> 安装结果
- [ ] 混合组件（embedded+local+download）安装/卸载闭环
- [ ] 回归测试：
- [ ] `v12` 包安装回归
- [ ] 静默安装参数回归
- [ ] 文档更新：
- [ ] `docs/configuration_reference.md`
- [ ] `docs/COMMAND_LINE_REFERENCE.md`
- [ ] 新增“组件化配置与排错指南”

验收标准：
- CI 全绿且关键回归场景覆盖通过。

## 4. 任务拆分建议（两周节奏示例）
- 第 1-2 天：M0 + M1（YAML 双栈）。
- 第 3-4 天：M2（模型与元数据 v13）。
- 第 5-7 天：M3 + M4（校验 + 流程动作化）。
- 第 8-9 天：M5（local/download 执行器与安全约束）。
- 第 10-11 天：M6（GUI 嵌入勾选绑定）。
- 第 12 天：M7（manifest/卸载）。
- 第 13-14 天：M8（测试、文档、发布）。

## 5. 关键风险与应对
- 风险：元数据升级导致旧包不兼容。  
  应对：严格保留 `v12` 分支解析，并加入回归测试门禁。

- 风险：GUI 控件命名与配置绑定不一致。  
  应对：启动时做绑定预校验，明确报错并允许降级到默认选择。

- 风险：`download/local` 执行带来安全面扩大。  
  应对：强制 `https+sha256`、路径防越界、脚本安装白名单开关。

- 风险：流程改造引入进度回退/抖动。  
  应对：统一权重模型，保持进度单调递增断言。

## 6. 完成定义（DoD）
- [ ] YAML 与 JSON 配置均可稳定打包。
- [ ] 组件化安装在 GUI/静默模式下均可用。
- [ ] 现有页面嵌入勾选可驱动组件安装结果。
- [ ] 卸载可回放组件安装记录并清理副作用。
- [ ] `v12` 兼容回归通过。
- [ ] 文档、示例、测试齐全并合入 CI。

## 7. 建议的首批提交顺序（便于评审）
1. `feat(config): add yaml loader and unified config mapping`
2. `feat(metadata): introduce v13 component fields with v12 compatibility`
3. `feat(validator): component schema and security validation`
4. `feat(installer): action-based flow and component executor`
5. `feat(gui): embedded component checkbox binding and selection pass-through`
6. `feat(uninstall): component execution manifest and replay`
7. `test/docs: coverage and documentation updates`

## 2026-02-16 Delivery Update (C4)
- Completed `C4 feat(installer): action-based flow and component executor` baseline.
- Implemented runtime component plan resolution:
  - default behavior = `required + defaultSelected`
  - supports CLI override by selected component ids
  - auto-resolves component dependencies and rejects invalid ids/cycles
- Implemented embedded folder filtering during extraction based on selected components.
- Implemented component-side effects merge for selected components:
  - merged `registry`, `killProcesses`, `autoStartup`, `desktopIcons`
  - manifest now records effective merged values
- Implemented local/download component execution in install phase:
  - `local`: `%InstallDir%` token expansion, base-path boundary check, process execute with wait/timeout
  - `download`: download to file, SHA256 verification, then process execute with wait/timeout
- Added installer CLI options:
  - `--component <id>` (repeatable)
  - `--components <id1,id2,...>`
  - `--all-components`
- Build verification:
  - `installer` target: success
  - `packager` target: success

## 2026-02-16 Delivery Update (C5)
- Completed `C5 feat(gui): embedded component checkbox binding and selection pass-through` baseline.
- Implemented UI binding scan for existing pages using metadata:
  - respects `ui.componentSelection.mode` (`embeddedInExistingPages`/`hybrid`)
  - supports `strategy = xml_userdata`
  - supports `tokenPrefix` and `pages[].skin + pages[].controls` scope filtering
- Implemented checkbox binding contract:
  - parses `userdata="component:<id>"`
  - enforces `required=true` as checked + disabled in UI
  - applies `defaultSelected` for optional components during UI initialization
- Implemented selection pass-through chain:
  - `GUIManager -> PageController -> InstallationWorker -> InstallService`
  - selected component ids are sent via `InstallServiceOptions.selectedComponentIds`
- Added non-blocking diagnostics (`WARN`) for unknown ids/pages/invalid binding entries.
- Build verification:
  - `installer` target: success

## 2026-02-16 Delivery Update (C6)
- Completed `C6 feat(uninstall): component execution manifest and replay` baseline.
- Manifest schema extended with `componentActions[]` (backward-compatible optional field):
  - `componentId`
  - `sourceType` (`local` / `download`)
  - `uninstallCommand`
  - `workingDirectory`
  - `wait`
  - `timeoutSec`
- Installer now records component uninstall replay actions after successful local/download component install execution.
- Uninstaller now reads and replays `componentActions` before file cleanup:
  - executes command via `cmd.exe /c`
  - supports `wait/timeout`
  - supports cancellation checks
  - failure handling is non-blocking warning (continue cleanup)
- Build verification:
  - `installer` target: success

## 2026-02-16 Delivery Update (C7)
- Added and validated test coverage updates for component uninstall replay manifest compatibility.
- `tests/test_manifest_roundtrip.cpp` now verifies:
  - `componentActions[]` round-trip serialization/deserialization
  - legacy manifest compatibility (no `componentActions` field)
- Test execution (BUILD_TESTS=ON):
  - `ctest -C Release --output-on-failure` passed (2/2)
  - `manifest_roundtrip` passed
  - `utf8_utils` passed

## 2026-02-16 Delivery Update (C7.1)
- Added `tests/test_console_component_args.cpp`.
- New test validates installer CLI component selection parsing:
  - `--component <id>` (repeatable)
  - `--components <id1,id2,...>` CSV parsing and whitespace trimming
  - `--all-components` flag behavior
- Test run result (`BUILD_TESTS=ON`, Release):
  - `manifest_roundtrip` passed
  - `utf8_utils` passed
  - `console_component_args` passed
  - total `3/3` passed

## 2026-02-16 Delivery Update (C7.2)
- Added `tests/test_uninstall_component_replay.cpp`.
- New test validates uninstall replay execution from manifest `componentActions`:
  - creates a temporary manifest with one replay command
  - runs `uninstallFromManifest`
  - asserts replay marker file creation and manifest cleanup
- Test run result (`BUILD_TESTS=ON`, Release):
  - `manifest_roundtrip` passed
  - `utf8_utils` passed
  - `console_component_args` passed
  - `uninstall_component_replay` passed
  - total `4/4` passed

## 2026-02-16 Delivery Update (C7.3)
- Added `tests/test_config_yaml_json_equivalence.cpp`.
- New test validates:
  - semantic equivalence of JSON legacy config and structured YAML config parsing
  - config discovery precedence (`packager.yaml` over `packager.json`)
- Added `tests/test_configuration_validator_components.cpp`.
- New test validates:
  - local installer path restrictions (relative-only, no parent traversal)
  - download security constraints (`https://` and valid SHA256 format)
  - dependency cycle rejection and valid graph acceptance
- Documentation update:
  - added `docs/components_troubleshooting_guide.md`
  - linked guide in `docs/configuration_reference.md`

## 2026-02-17 Delivery Update (C7.4)
- Dependency source strategy for `yaml-cpp` switched to repository-local path:
  - default source: `third_party/yaml-cpp`
  - integrated via `add_subdirectory`
- Removed automatic FetchContent-by-default behavior to avoid implicit `build/_deps` download in normal builds.
- Added explicit fallback switch:
  - `YAML_CPP_FETCH_FALLBACK=ON` to allow temporary FetchContent fallback when submodule/system package is unavailable.

## 2026-02-17 Delivery Update (C7.5)
- `yaml-cpp` submodule URL updated to official upstream:
  - `https://github.com/jbeder/yaml-cpp.git`
- Submodule pointer upgraded from `0.8.0` to `0.9.0`.
- Fallback FetchContent tag aligned to `yaml-cpp-0.9.0`.
