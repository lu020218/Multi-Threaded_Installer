# 重构最小迁移说明（2026-02-13）

## 结论
- 本轮重构以模块拆分与内部抽象收敛为主，对外 CLI 参数、包格式、Flow DSL 字段未做破坏性变更。

## 代码迁移点
- `installer_gui` 启动流程拆分为 `bootstrap / app_builder / embedded_package / custom_ui` 模块，`main.rs` 仅保留入口编排。
- `installer_core` 将 step 分派迁移为注册表机制，并拆分组件安装与脚本执行子系统。
- 默认安装 Flow 改为内置 YAML（`include_str!`）加载后统一走 `FlowDefinition` 解析/校验。
- `installer_shared` 提供 CLI 共用格式化与进度输出实现，`installer_cli` 与 `packager_cli` 复用。
- 新增 `StepParams` 统一 `FlowStep.params` 读取，减少散落字符串键访问。

## 使用方影响
- 若仅调用公开 CLI 命令：无需改造。
- 若依赖 `installer_core` 内部文件路径或私有函数：需要按新模块路径更新引用。
- 若依赖默认 flow 的 Rust 结构体硬编码细节：应改为更新 YAML 内容。

## 验证
- 已通过 `cargo test --all` 全量回归（含单测、集成测试、doc tests）。
