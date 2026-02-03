# Packager 配置迁移说明（JSON -> YAML）

从当前版本开始，打包配置仅支持 YAML（`packager.yaml` / `packager.yml`）。  
`packager.json` 已不再支持。

## 1. 迁移步骤

1. 将原 `packager.json` 内容转换为 `packager.yaml`
2. 文件名改为 `packager.yaml`
3. 在 CI/脚本中移除对 `packager.json` 的引用
4. 使用 `packager_cli` 重新构建并验证

## 2. 字段映射示例

JSON:

```json
{
  "application_name": "DemoApp",
  "version": "1.0.0",
  "default_install_dir": "%ProgramFiles%\\DemoApp",
  "flow_file": "flow.yaml",
  "script_files": ["scripts/precheck.js"]
}
```

YAML:

```yaml
application_name: "DemoApp"
version: "1.0.0"
default_install_dir: "%ProgramFiles%\\DemoApp"
flow_file: "flow.yaml"
script_files:
  - "scripts/precheck.js"
```

## 3. 常见问题

- 问：还能继续使用 `packager.json` 吗？  
  答：不能。新版已移除 JSON 配置支持。

- 问：flow/script 会不会被重复打入 payload？  
  答：不会。内嵌 flow/script 会自动排除，避免重复打包。

- 问：如何覆盖 flow/script？  
  答：优先在 YAML 中设置 `flow_file` 与 `script_files`。

## 4. 验证清单

- [ ] `packager.yaml` 存在且可解析
- [ ] 产物可安装
- [ ] 嵌入 flow/script 可被安装端识别
- [ ] payload 中不存在重复 flow/script 业务文件
