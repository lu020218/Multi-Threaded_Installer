# YAML + 组件化安装提交级任务单（含工时与依赖）

## 1. 估算口径
- 工时单位：人时（h），按单人开发估算。
- 估算包含：编码 + 本地验证 + 基本文档更新。
- 不含：代码评审等待时间、CI 排队时间、跨团队评审会议。

## 2. 提交顺序总览

| 提交ID | 提交标题 | 预估工时 | 前置依赖 | 风险等级 | 状态 |
|---|---|---:|---|---|---|
| C1 | `feat(config): add yaml loader and unified config mapping` | 12h | 无 | 中 | 已完成（2026-02-16） |
| C2 | `feat(metadata): introduce v13 component fields with v12 compatibility` | 14h | C1 | 高 | 已完成（2026-02-16） |
| C3 | `feat(validator): component schema and security validation` | 10h | C1, C2 | 中 | 已完成（2026-02-16） |
| C4 | `feat(installer): action-based flow and component executor` | 18h | C2, C3 | 高 | 待开始 |
| C5 | `feat(gui): embedded component checkbox binding and selection pass-through` | 14h | C2, C4 | 中 | 待开始 |
| C6 | `feat(uninstall): component execution manifest and replay` | 12h | C4, C5 | 高 | 待开始 |
| C7 | `test/docs: coverage and documentation updates` | 10h | C1-C6 | 中 | 待开始 |

总计：约 90h（建议按 2 周迭代执行）。

## 3. 逐提交任务拆解

## C1. `feat(config): add yaml loader and unified config mapping`

目标：
- 支持 YAML 配置读取，且与 JSON 语义一致。

子任务：
1. 在构建系统接入 YAML 解析库（建议 `yaml-cpp`）。
2. 扩展配置发现优先级：
   `PACKAGER_CONFIG` > `packager.yaml` > `packager.yml` > `packager.json` > `.packager.json`。
3. 在 `ConfigurationLoader` 增加 `parseYamlConfig`。
4. 抽取统一映射层（避免 JSON/YAML 双份业务逻辑）。
5. 增加错误信息标准化（字段路径、期望类型、当前值）。
6. 更新配置说明文档，补充 YAML 示例。

预估工时：
- 依赖接入与编译验证：2h
- Loader 改造与统一映射：6h
- 错误处理与文档：2h
- 本地验证：2h

建议改动文件：
- `CMakeLists.txt`
- `include/packager/configuration_loader.h`
- `src/packager/configuration_loader.cpp`
- `docs/configuration_reference.md`

验收标准：
- 同一配置语义下，JSON/YAML 解析结果一致。
- YAML 解析报错包含具体字段位置。

---

## C2. `feat(metadata): introduce v13 component fields with v12 compatibility`

状态：已完成（2026-02-16）

目标：
- 新增组件相关模型并写入元数据，升级到 v13，同时兼容 v12。

子任务：
1. 在 `types.h` 增加组件结构体与 UI 绑定结构体。
2. 在 `PackagerConfiguration`、`ExtendedInstallationMetadata` 增加对应字段。
3. `Constants::VERSION` 从 `12` 升到 `13`。
4. 更新 `MetadataGenerator`：序列化组件字段。
5. 更新 `MetadataParser`：反序列化 v13，且保留 v12 分支默认值。
6. 为关键字段设置默认值，避免旧包安装崩溃。

预估工时：
- 模型设计与头文件改造：4h
- 序列化/反序列化改造：7h
- v12 兼容回归验证：3h

建议改动文件：
- `include/common/types.h`
- `src/packager/metadata_generator.cpp`
- `src/installer/metadata_parser.cpp`

验收标准：
- v12 包安装路径不回归。
- v13 包可读写组件与 UI 绑定元数据。

---

## C3. `feat(validator): component schema and security validation`

状态：已完成（2026-02-16）

目标：
- 在打包阶段阻断无效/不安全组件配置。

子任务：
1. 校验 `components[].id` 唯一性。
2. 校验 `dependsOn` 引用存在且无环（拓扑检测）。
3. 校验 `required + defaultSelected` 约束。
4. 校验 `source.local` 路径不越界 `%InstallDir%`。
5. 校验 `source.download` 强制 `https + sha256`。
6. 校验组件与 `folders` 映射关系有效。

预估工时：
- 规则实现：6h
- 错误信息优化：2h
- 本地测试样例与验证：2h

建议改动文件：
- `include/packager/configuration_validator.h`
- `src/packager/configuration_validator.cpp`
- `src/packager/main.cpp`

验收标准：
- 非法配置在打包阶段直接失败并给出可定位信息。

---

## C4. `feat(installer): action-based flow and component executor`

目标：
- 将安装流程升级为可扩展动作流，接入组件选择执行。

子任务：
1. 在 `InstallService` 增加动作调度骨架。
2. 首版实现动作：
   - `CollectComponentSelection`
   - `ResolveComponents`
   - `InstallSelectedComponents`
3. 保留 `ExtractFiles`，并支持按组件过滤 `embedded` 文件夹。
4. 统一进度权重，确保总进度单调递增。
5. 兼容 silent 模式默认组件选择策略。
6. 增加执行日志（动作开始/结束/失败）。

预估工时：
- 调度器改造：8h
- 三个动作实现：7h
- 进度与日志整合：3h

建议改动文件：
- `include/installer/install_service.h`
- `src/installer/install_service.cpp`
- `include/common/installer_parallel_install.h`
- `src/common/installer_parallel_install.cpp`

验收标准：
- 组件勾选可直接影响安装内容。
- 进度无回退。

---

## C5. `feat(gui): embedded component checkbox binding and selection pass-through`

目标：
- 支持从现有页面读取组件勾选，并透传到安装服务。

子任务：
1. 实现 `userdata="component:<id>"` 解析。
2. 支持按配置限定扫描页面/控件集合。
3. `required=true` 组件 UI 置灰且强制选中。
4. 在 GUI 层新增 `selectedComponents` 透传链路：
   `GUIManager -> PageController -> InstallationWorker -> InstallService`。
5. 缺失绑定时提供诊断日志和降级策略。
6. 更新欢迎页示例 XML（保留向后兼容控件）。

预估工时：
- UI 绑定读取与规则：6h
- 参数透传链路：5h
- 页面示例与联调：3h

建议改动文件：
- `src/gui/gui_manager.cpp`
- `include/gui/page_controller.h`
- `src/gui/page_controller.cpp`
- `include/gui/installation_worker.h`
- `src/gui/installation_worker.cpp`
- `resources/skins/welcome_page.xml`

验收标准：
- 页面勾选结果与实际安装组件一致。

---

## C6. `feat(uninstall): component execution manifest and replay`

目标：
- 记录组件安装结果并在卸载时回放。

子任务：
1. 扩展 manifest：新增组件执行记录字段。
2. 卸载优先按组件记录回放（local/download 卸载命令）。
3. 回放失败策略：支持 `continue/fail`。
4. 旧 manifest 兼容读取（无组件字段时走旧逻辑）。
5. 完整记录日志与结果状态。

预估工时：
- manifest 模型与读写：5h
- 卸载回放逻辑：5h
- 兼容分支验证：2h

建议改动文件：
- `include/installer/uninstall_manager.h`
- `src/installer/uninstall_manager.cpp`
- `tests/test_manifest_roundtrip.cpp`

验收标准：
- 组件卸载行为可重现、可追踪、可容错。

---

## C7. `test/docs: coverage and documentation updates`

目标：
- 完成测试闭环与文档闭环，达到发布门槛。

子任务：
1. 单元测试：
   - YAML/JSON 等价解析
   - 组件依赖解析
   - 路径防越界与哈希校验
2. 集成测试：
   - 页面勾选 -> 安装结果
   - embedded/local/download 混合流程
3. 回归测试：
   - v12 安装包兼容
   - 静默安装参数兼容
4. 更新文档：
   - 配置参考
   - 命令行参考
   - 组件化排错指南

预估工时：
- 测试编写与调整：7h
- 文档更新：3h

建议改动文件：
- `tests/*`（新增/修改）
- `docs/configuration_reference.md`
- `docs/COMMAND_LINE_REFERENCE.md`
- `docs/*components*guide*.md`（新增）

验收标准：
- CI 全绿。
- 核心场景可复现并有文档指引。

## 4. 并行建议（多人协作）
- 线程A（配置/元数据）：C1 -> C2 -> C3
- 线程B（安装流程）：在 C2 产出后开始 C4
- 线程C（GUI/卸载）：在 C4 稳定后并行 C5 + C6
- 收口线程：C7

## 5. 关键决策点（需尽早拍板）
1. `--enable-components` 默认开启还是关闭。
2. `when` 表达式首版支持范围（建议最小子集）。
3. 组件卸载失败策略默认值（建议 `continue` + 明确告警）。
4. `download` 缓存策略（每次下载 / 可复用缓存 + 校验）。

## 6. 建议每日交付节奏
- Day 1-2：提交 C1
- Day 3-4：提交 C2
- Day 5：提交 C3
- Day 6-8：提交 C4
- Day 9-10：提交 C5
- Day 11：提交 C6
- Day 12-14：提交 C7 + 回归修复

## 7. 发布前检查清单
- [ ] v12 安装包安装/卸载回归通过
- [ ] YAML 与 JSON 配置样例均可打包
- [ ] GUI 勾选与静默模式行为一致
- [ ] 下载组件安全策略全部生效
- [ ] manifest 可用于问题追踪
- [ ] 文档可指导外部使用与排错

## 2026-02-16 Progress Note
- C4 status updated to completed.
- Runtime component actions are now connected in installer service:
  - component selection resolution and dependency closure
  - selected embedded folders extraction filter
  - local/download component action execution
  - merged side-effects handling (`registry`, `killProcesses`, startup/shortcut)
- Pending next major items:
  - C5 GUI embedded checkbox binding (`userdata=component:<id>`) pass-through
  - C6 uninstall manifest replay for component actions

## 2026-02-16 Progress Note (C5)
- C5 status updated to completed (baseline).
- Delivered GUI embedded component binding and pass-through:
  - `userdata=component:<id>` scanning
  - required component lock in UI
  - selected component ids passed to install runtime
- Remaining major scope:
  - C6 uninstall replay for local/download component actions

## 2026-02-16 Progress Note (C6)
- C6 status updated to completed (baseline).
- Added uninstall replay pipeline based on manifest `componentActions` records.
- Maintains compatibility with old manifest files without `componentActions`.

## 2026-02-16 Progress Note (C7)
- C7 test baseline updated.
- Existing test suite is green under `BUILD_TESTS=ON` with component-action manifest assertions added.
- Remaining optional work: add dedicated integration scenario for GUI checkbox -> selected components -> install/uninstall replay chain.

## 2026-02-16 Progress Note (C7.1)
- Added command-line parser regression coverage for component flags.
- C7 current automated tests: 3 passing tests under Release configuration.

## 2026-02-16 Progress Note (C7.2)
- Added uninstall replay execution regression test.
- C7 current automated test set is 4 passing tests under Release configuration.

## 2026-02-16 Progress Note (C7.3)
- Added YAML/JSON config equivalence regression test coverage.
- Added component validator security/dependency regression test coverage.
- Added componentized install troubleshooting guide and linked it from configuration reference.

## 2026-02-17 Progress Note (C7.4)
- Updated CMake dependency resolution for `yaml-cpp` to prefer repository-local source (`third_party/yaml-cpp`).
- Default path no longer relies on implicit FetchContent download into `build/_deps`.
- Added optional fallback gate `YAML_CPP_FETCH_FALLBACK` for non-submodule environments.

## 2026-02-17 Progress Note (C7.5)
- Switched `yaml-cpp` submodule remote to official GitHub upstream.
- Upgraded pinned submodule version to `yaml-cpp-0.9.0`.
- Synced CMake fallback tag to `yaml-cpp-0.9.0`.
