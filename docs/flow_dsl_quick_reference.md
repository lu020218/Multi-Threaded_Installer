# 安装流程 DSL 快速参考（YAML）

面向：编写安装流程配置的同学。  
目标：快速上手、减少常见配置错误。

---

## 1. 最小可运行模板

```yaml
version: 1

vars:
  AppName: "DemoApp"
  InstallDir: "C:\\Program Files\\DemoApp"

install_flow:
  steps:
    - id: check_disk
      type: check_disk
      params:
        path: "${InstallDir}"
      on_fail: rollback

    - id: extract
      type: extract_package
      params:
        target: "${InstallDir}"
      on_fail: rollback

  rollback:
    - id: cleanup_files
      type: rollback_files
```

---

## 2. 结构说明

- `version`：当前固定为 `1`
- `vars`：全局变量（可在 `params` / `when` 使用）
- `install_flow.steps`：主执行步骤（按顺序执行）
- `install_flow.rollback`：回滚步骤（主流程失败后，按**逆序**执行）
- `ui_flow`：可选，GUI 页面流转配置（非必填）

---

## 3. Step 字段说明

每个步骤常用字段：

- `id`：步骤唯一 ID（同一列表内不可重复）
- `type`：步骤类型（内置节点或 `script`）
- `params`：步骤参数（对象）
- `when`：条件表达式，结果为 `true` 才执行
- `on_fail`：失败策略，支持：
  - `abort`：立即终止（默认）
  - `continue`：忽略错误，继续后续步骤
  - `rollback`：触发回滚流程后返回失败
- `engine`：仅 `type: script` 时必填（`js` / `ts` / `wasm`）

---

## 4. 变量与模板替换

### 4.1 params 模板

- 语法：`${...}`
- 作用域：
  - `vars.*`
  - `options.*`
  - `metadata.*`
- 简写：`${InstallDir}` 等价于 `${vars.InstallDir}`（优先从 vars 取）

示例：

```yaml
params:
  target: "${InstallDir}"
  log: "Installing to ${InstallDir}"
```

### 4.2 when 条件表达式

支持：

- 逻辑：`&&`、`||`
- 比较：`==`、`!=`、`>`、`>=`、`<`、`<=`
- 分组：`(...)`

示例：

```yaml
when: '${options.disk_gb >= 20 && (options.channel == "stable" || vars.ForceInstall == true)}'
```

---

## 5. script 节点示例

```yaml
- id: custom_logic
  type: script
  engine: js
  params:
    path: "scripts/custom.js"
    args:
      mode: "repair"
```

注意：

- `type: script` 必须有 `engine`
- `params.path` 必须存在且非空
- 当前安装器仅实现 `engine: js`（通过 Node.js 执行）

### 5.1 脚本安全开关与白名单

默认禁用脚本节点。启用方式：

- CLI:
  - `--enable-scripts`
  - 至少一个 `--script-allow-root <path>`（可重复）
- 环境变量（Installer 默认策略）：
  - `MTI_ENABLE_SCRIPTS=1`
  - `MTI_SCRIPT_ALLOWLIST=C:\\flows\\scripts;D:\\trusted`

补充：

- **包内嵌脚本（随 package metadata 内嵌并落盘到临时目录）默认可执行**；
- **外部脚本**仍必须显式启用并通过白名单校验。

### 5.2 脚本上下文（Node.js 环境变量）

执行脚本时会注入以下环境变量（JSON 字符串）：

- `MTI_ARGS_JSON`
- `MTI_VARS_JSON`
- `MTI_METADATA_JSON`
- `MTI_OPTIONS_JSON`

---

## 6. 常见错误与修复

### 错误 1：`on_fail: rollback` 但未定义 rollback

错误写法：

```yaml
install_flow:
  steps:
    - id: extract
      type: extract_package
      on_fail: rollback
```

修复：补充 `install_flow.rollback` 列表。

---

### 错误 2：script 节点缺少 engine/path

错误写法：

```yaml
- id: run_custom
  type: script
  params: {}
```

修复：补 `engine` 与 `params.path`。

---

### 错误 3：表达式语法不完整

错误写法：

```yaml
when: '${options.flag &&}'
```

修复：补全右侧操作数，例如：

```yaml
when: '${options.flag == true && options.mode == "full"}'
```

---

### 错误 4：比较类型不兼容

错误写法（数字和字符串做大小比较）：

```yaml
when: "${options.disk_gb > options.label}"
```

修复：确保比较两侧类型一致（数字对数字、字符串对字符串、布尔对布尔）。

---

### 错误 5：回滚步骤失败导致最终错误类型变化

说明：

- 当主流程失败并触发回滚时，如果回滚步骤本身又失败，最终返回的是 `Rollback` 类型错误，而不是主流程原始错误。

建议：

- 回滚步骤尽量使用稳定、幂等的节点；
- 回滚流程中避免引入高风险外部依赖；
- 为回滚步骤单独做故障注入测试。

---

## 7. 编写建议（实践）

- 为每个步骤设置清晰 `id`，便于日志定位
- 将“可变配置”放到 `vars`，避免硬编码路径
- 关键步骤统一使用 `on_fail: rollback`
- 回滚步骤保持“少而稳”，优先保证可执行成功
- 表达式尽量加括号，避免歧义
- 先本地跑最小流程，再逐步增加复杂分支
