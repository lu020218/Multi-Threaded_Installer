# 重构优化 TODO（代码层面）

## 目标
- 降低核心模块耦合度，提升可维护性与可测试性。
- 收敛重复逻辑，减少后续功能扩展成本。
- 在不改变现有对外行为的前提下，完成结构化重构。

## 范围
- `installer_gui`
- `installer_core`
- `installer_cli`
- `packager_cli`

## 里程碑

### M1：GUI 启动层解耦（高优先级）
- [x] 拆分 `installer_gui/src/main.rs`，提取以下模块：
  - `bootstrap`（日志初始化、环境变量注入、主流程编排）
  - `embedded_package`（内嵌包检测与提取）
  - `custom_ui`（自定义 UI 资源加载与注入）
  - `app_builder`（Tauri Builder 注册与 setup）
- [x] 将长内联 JS 注入脚本迁移为 `include_str!` 管理的独立脚本文件。
- [x] 为 `custom_ui` 添加单元测试：
  - 路径存在/缺失
  - `index.html` 缺失
  - css/js 缺失回退
- [x] 验收：`main.rs` 行数下降到可读范围（建议 < 250 行），启动行为保持一致。

### M2：Flow Step 执行器解耦（高优先级）
- [x] 将 `installer_core/src/installer.rs` 中内置 step 分派从 `match` 迁移为注册表机制：
  - 设计 `StepHandler` trait
  - 设计 `StepRegistry`（`HashMap<String, Box<dyn StepHandler>>`）
- [x] 将以下 step 处理器拆分到独立模块：
  - 基础安装：`check_disk`、`extract_package`、`rollback_files`
  - 系统配置：`create_shortcut`、`write_registry`、`configure_autostart`
  - 组件流：`load_component_manifest`、`resolve_selected_components`、`download/verify/install/rollback_component`
- [x] 保留兼容：未知 step 类型返回现有错误语义。
- [x] 验收：新增 step 时无需修改核心 `Installer` 主文件。

### M3：组件安装子系统内聚化（中高优先级）
- [x] 新建 `installer_core/src/components/runtime.rs`：
  - 管理 `ComponentRuntimeState` 生命周期
  - 统一缓存目录创建/回收
- [x] 新建 `installer_core/src/components/installer.rs`：
  - `download_component_by_id`
  - `verify_component_by_id`
  - `install_component_by_id`
  - `rollback_component`
- [x] 抽离命令执行工具到 `installer_core/src/process.rs`：
  - 参数拆分
  - 程序执行
  - 错误标准化
- [x] 验收：组件相关逻辑可独立测试，不依赖完整 `Installer` 初始化。

### M4：脚本执行器独立化（中优先级）
- [x] 提取 `ScriptExecutor`：
  - 路径解析（含 embedded script）
  - 安全策略校验（enable + allow roots）
  - Node 进程启动与环境变量注入
- [x] 对外暴露清晰接口：`execute(step, context, policy) -> Result<()>`。
- [x] 增加测试：
  - 禁用脚本时拒绝执行
  - 非 allowlist 路径拒绝执行
  - `engine != js` 返回未实现错误
- [x] 验收：`Installer` 中脚本逻辑仅保留一次委托调用。

### M5：默认 Flow 配置外置化（中优先级）
- [x] 将 `build_default_install_flow_definition` 改为读取内置 YAML（`include_str!`）。
- [x] 默认 Flow 与自定义 Flow 统一走 `FlowDefinition::from_yaml_str` + `validate`。
- [x] 增加回归测试：默认 flow 加载失败时给出明确错误上下文。
- [x] 验收：默认 flow 修改不再需要改 Rust 结构体构造代码。

### M6：CLI/Packager 共性逻辑收敛（中优先级）
- [x] 提取公共进度输出模块（建议放 `installer_shared` 或新建 `installer_utils`）。
- [x] 提取通用格式化方法：
  - 大小格式化
  - 速度格式化
  - 路径截断
- [x] 统一错误打印与退出码处理风格。
- [x] 验收：`installer_cli` 与 `packager_cli` 重复函数显著减少。

### M7：常量与类型安全提升（中低优先级）
- [x] 将字符串型 step 名称集中为常量或枚举映射层。
- [x] 将关键命令名（如 `msiexec`、`node`）统一常量管理。
- [x] 收敛 `params` 解析逻辑，减少散落的 `params.get(...)`。
- [x] 验收：字符串拼写错误风险下降，review 可读性提升。

## 质量门禁（每个里程碑都需满足）
- [x] `cargo test --all` 通过。
- [x] 关键模块新增或更新单元测试。
- [x] 不引入行为变更（除非任务明确标注）。
- [x] 关键变更附带最小迁移说明。

## 动态进度（2026-02-13）
- [x] 已完成：M1 全部子项。
- [x] 已完成：M2 全部子项。
- [x] 已完成：M3 全部子项。
- [x] 已完成：M4 全部子项。
- [x] 已完成：M5 全部子项。
- [x] 已完成：M6 全部子项。
- [x] 已完成：M7 全部子项。
- [x] 质量门禁进展：`cargo test --all` 已通过（当前仍有 warning 待清理）。

## 验收说明（2026-02-13）
- M7：通过 `StepParams` 统一参数访问入口，减少 `params` 键名硬编码分散点，降低拼写风险并提升 review 可读性。
- 迁移说明：见 `docs/refactor_migration_notes.md`。

## 风险与应对
- [ ] 风险：重构导致行为回归。
  - 应对：先加测试再迁移，分阶段小步提交。
- [ ] 风险：模块拆分后循环依赖。
  - 应对：先定义 trait/interface，再迁移实现。
- [ ] 风险：GUI 注入链路脆弱。
  - 应对：增加启动烟测与 UI 资源缺失场景测试。

## 执行顺序建议
1. M1（GUI 解耦）
2. M2（Flow Step 解耦）
3. M3（组件子系统）
4. M4（脚本执行器）
5. M5（默认 Flow 外置）
6. M6（CLI 共性收敛）
7. M7（常量与类型安全）

## 预估产出
- 更清晰的模块边界。
- 更低的新增功能改动面。
- 更稳定的测试基线与发布质量。
