# 安装流程 DSL 需求文档

## 1. 背景
当前项目主要依赖配置文件生成安装程序，安装流程的可自定义能力有限。为满足更复杂的安装流程（条件分支、回滚、脚本扩展、UI 状态机驱动），需要定义一套 **流程 DSL**，并在前后端落地执行框架。

## 2. 目标
1. 提供可读性高的流程定义（选择 YAML）。
2. 支持 UI 流程与安装执行流程解耦。
3. 支持条件分支、失败回滚策略。
4. 支持脚本节点扩展（JS/TS/Rust/WASM）。
5. 保持安全可控、可验证、可测试。

## 3. 范围
- **包含**
  - DSL 语法与字段定义（YAML）。
  - 解析与校验（schema + runtime validation）。
  - 后端执行器（核心节点 + 回滚机制）。
  - UI 状态机驱动（页面/事件）。
- **不包含**
  - 具体脚本运行时实现细节（留为后续扩展）。
  - 远程更新分发平台。

## 4. 关键需求

### 4.1 DSL 格式
- MUST 使用 YAML。
- MUST 支持版本号字段 `version`。
- MUST 支持变量定义 `vars`。
- MUST 支持 UI 流程 `ui_flow`。
- MUST 支持 安装流程 `install_flow`。

### 4.2 条件分支
- MUST 支持 `when` 条件表达式（基于变量/元数据/选项）。
- MUST 支持 `on_fail` 指定回滚策略。

### 4.3 回滚策略
- MUST 支持全局 `rollback` 流程定义。
- MUST 保证安装失败触发回滚节点执行。

### 4.4 脚本节点
- MUST 支持 `type: script` 节点。
- MUST 支持 `engine: js | wasm | native`。
- MUST 支持传入参数 `params`。

### 4.5 内置节点
至少支持以下节点类型：
- check_disk
- check_windows_version
- extract_package
- write_registry
- create_shortcut
- configure_autostart
- rollback_files
- rollback_registry
- emit_progress

### 4.6 安全与隔离
- 脚本节点必须在可控的沙箱或白名单机制下运行。
- 节点执行必须记录日志，便于追踪与回滚。

## 5. 质量属性
- **可读性**：非开发人员可理解流程结构。
- **可验证性**：解析阶段可发现字段/类型错误。
- **可扩展性**：新增节点无需改动 DSL 结构。
- **可测试性**：可模拟节点执行并断言输出。

## 6. 交付物
1. DSL 语法文档与示例。
2. 解析与 schema 校验实现。
3. 后端执行器骨架。
4. UI 状态机对接示例。

