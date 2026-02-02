# 安装流程 DSL 方案设计

## 1. 总体架构
DSL 分为两条主线：
- **UI 流程（ui_flow）**：描述页面跳转、事件响应。
- **安装流程（install_flow）**：描述后端节点执行顺序、条件与回滚。

执行模型：
1. 前端加载 YAML DSL。
2. UI 通过 `ui_flow` 驱动页面状态机。
3. 后端执行 `install_flow`，按步骤调用内置节点或脚本节点。
4. 步骤失败触发 `rollback` 流程。

## 2. DSL 结构（草案）

```yaml
version: 1

vars:
  AppName: "MyApp"
  InstallDir: "%ProgramFiles%\\MyApp"
  PackagePath: "./package.mti"

ui_flow:
  pages:
    - id: welcome
      next: directory
    - id: directory
      on_enter:
        - set_var: { key: InstallDir, from_input: "#install-dir" }
      next: progress
    - id: progress
      next: complete
    - id: complete
  events:
    cancel:
      goto: welcome

install_flow:
  steps:
    - id: check_disk
      type: check_disk
      params: { path: "${InstallDir}" }
      on_fail: rollback

    - id: extract
      type: extract_package
      params: { package: "${PackagePath}", target: "${InstallDir}" }
      on_fail: rollback

  rollback:
    - id: rollback_files
      type: rollback_files
```

## 3. 变量与表达式
- `${Var}`：变量替换。
- `when`：条件表达式（例：`${options.desktop_icons == true}`）。
- 变量来源：
  - `vars` 定义
  - `metadata`（包元数据）
  - `options`（用户选择）

## 4. 节点类型设计

### 4.1 内置节点
| type | 说明 |
|---|---|
| check_disk | 磁盘空间检查 |
| check_windows_version | 系统版本检查 |
| extract_package | 解压写入文件 |
| write_registry | 写注册表 |
| create_shortcut | 创建快捷方式 |
| configure_autostart | 配置自启 |
| emit_progress | 发送进度事件 |
| rollback_files | 清理文件 |
| rollback_registry | 清理注册表 |

### 4.2 脚本节点
```yaml
- id: custom_logic
  type: script
  engine: js
  params:
    path: "scripts/custom.js"
    args:
      key: value
```
执行器根据 `engine` 调用对应运行时。

## 5. 解析与校验
- YAML 解析 → 结构体（serde）。
- Schema 校验：
  - 必填字段存在
  - 节点类型合法
  - `when` 表达式语法检查
  - `on_fail` 指向合法回滚定义

## 6. 执行器设计
执行器核心职责：
1. 按顺序执行 `install_flow.steps`。
2. 判断 `when`，跳过不满足条件的步骤。
3. 捕获失败并触发 `rollback`。
4. 输出事件用于 UI 进度展示。

伪代码：
```
for step in steps:
  if step.when && !eval(step.when): continue
  run(step)
  if failed:
     run(rollback)
     return error
return ok
```

### 6.1 失败策略与回滚语义（补充）
- `on_fail: abort`：步骤失败后立即返回错误，不进入回滚。
- `on_fail: continue`：记录失败并继续后续步骤。
- `on_fail: rollback`：触发 `install_flow.rollback`，按**逆序**执行回滚步骤。

### 6.2 回滚错误传播（补充）
- 如果主流程失败且进入回滚，但回滚步骤本身再次失败：
  - 执行器返回 `InstallerError::Rollback(...)`
  - 错误消息包含失败的回滚 step id，便于定位。
- 若回滚成功，则返回原始主流程错误。

### 6.3 故障注入验证结论（补充）
- 场景 A：`extract_package` 成功后故障注入失败节点，`on_fail: rollback` 能清理已写入文件。
- 场景 B：故障注入使回滚节点也失败，最终返回 `Rollback` 类型错误。

## 7. 安全策略
- 脚本节点白名单 + 沙箱隔离。
- 默认禁用脚本节点（需显式开启）。
- 记录每个节点执行日志。

## 8. 与当前架构的结合点
- `installer_core` 增加 FlowExecutor。
- `installer_shared` 新增 DSL 结构体与 schema。
- `installer_gui` 前端加载 `ui_flow` 驱动页面。

## 9. 表达式能力（更新）
`when` 目前支持：
- 逻辑运算：`&&`、`||`
- 比较运算：`==`、`!=`、`>`、`>=`、`<`、`<=`
- 分组：`(...)`
- 作用域变量：`vars.*`、`options.*`、`metadata.*`

示例：

```yaml
install_flow:
  steps:
    - id: precheck
      type: emit_progress
      when: '${options.disk_gb >= 20 && (options.channel == "stable" || vars.ForceInstall == true)}'
```

注意：
- 比较两侧类型必须兼容（数字对数字、字符串对字符串、布尔对布尔）。
- 混合类型比较会直接报错并终止当前步骤执行。
