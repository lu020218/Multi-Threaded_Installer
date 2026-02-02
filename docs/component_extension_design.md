# 可选组件安装扩展设计（UI + DSL + 下载安全）

本文档给出“安装界面勾选组件 -> 联网下载 -> 校验 -> 安装 -> 回滚”的一版可落地方案。

## 1. 目标

- 支持在安装界面动态增加“可选组件”控件（复选框/单选）
- 支持按用户选择执行组件下载与安装
- 支持失败回滚（主程序与组件）
- 强制供应链安全（来源白名单、哈希、签名、TLS）

## 2. 总体分层

- UI 层：`ui_schema` 驱动动态控件，输出 `options.components.*`
- 编排层：`flow.yaml` 条件分支 + 内置节点串联
- 组件层：`component_manifest.yaml` 描述组件包、哈希、签名、安装动作
- 安全层：下载域名白名单、签名公钥、哈希校验、重试与超时策略

## 3. component_manifest.yaml（建议结构）

```yaml
version: 1
channel: stable
public_key_id: "main-release-key-2026"

components:
  - id: "extra-tools"
    display_name: "Extra Tools"
    version: "2.1.0"
    required: false
    package:
      url: "https://downloads.example.com/components/extra-tools-2.1.0.zip"
      size: 24567890
      sha256: "8a2f...f1c9"
      signature: "base64-signature-over-component-metadata"
    install:
      kind: "archive"
      target_subdir: "components/extra-tools"
      entrypoint: "install.ps1"
    rollback:
      remove_paths:
        - "components/extra-tools"

  - id: "vc-runtime"
    display_name: "VC++ Runtime"
    version: "14.40.0"
    required: false
    package:
      url: "https://downloads.example.com/components/vc-runtime-14.40.0.msi"
      size: 14680064
      sha256: "5df0...a812"
      signature: "base64-signature-over-component-metadata"
    install:
      kind: "msi"
      args: "/quiet /norestart"
    rollback:
      uninstall_product_code: "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}"
```

## 4. UI schema（动态控件）

```yaml
version: 1
forms:
  - id: "components"
    page: "options"
    controls:
      - type: "checkbox_group"
        id: "optional_components"
        label: "Optional Components"
        bind: "options.components"
        items:
          - id: "extra-tools"
            label: "Extra Tools"
            default: false
          - id: "vc-runtime"
            label: "VC++ Runtime"
            default: false
```

绑定规则：

- `bind: options.components` 时，前端输出建议结构：
  - `options.components.extra-tools = true/false`
  - `options.components.vc-runtime = true/false`

## 5. Flow 执行节点（建议新增）

- `load_component_manifest`：加载并解析组件清单
- `resolve_selected_components`：将 `options.components.*` 解析为待安装列表
- `download_component`：下载到缓存目录（支持重试）
- `verify_component`：SHA256 + 签名校验
- `install_component`：执行组件安装动作（archive/msi/exe/script）
- `rollback_component`：按 manifest 回滚

## 6. 端到端流程示例

```yaml
version: 1

install_flow:
  steps:
    - id: base_extract
      type: extract_package
      on_fail: rollback

    - id: load_manifest
      type: load_component_manifest
      params:
        path: "${InstallDir}/resources/component_manifest.yaml"
      on_fail: abort

    - id: select_components
      type: resolve_selected_components
      on_fail: abort

    - id: download_extra_tools
      type: download_component
      when: "${options.components.extra-tools == true}"
      params:
        component_id: "extra-tools"
      on_fail: rollback

    - id: verify_extra_tools
      type: verify_component
      when: "${options.components.extra-tools == true}"
      params:
        component_id: "extra-tools"
      on_fail: rollback

    - id: install_extra_tools
      type: install_component
      when: "${options.components.extra-tools == true}"
      params:
        component_id: "extra-tools"
      on_fail: rollback

    - id: download_vc_runtime
      type: download_component
      when: "${options.components.vc-runtime == true}"
      params:
        component_id: "vc-runtime"
      on_fail: rollback

    - id: verify_vc_runtime
      type: verify_component
      when: "${options.components.vc-runtime == true}"
      params:
        component_id: "vc-runtime"
      on_fail: rollback

    - id: install_vc_runtime
      type: install_component
      when: "${options.components.vc-runtime == true}"
      params:
        component_id: "vc-runtime"
      on_fail: rollback

  rollback:
    - id: rollback_components
      type: rollback_component
      on_fail: continue
    - id: rollback_files
      type: rollback_files
      on_fail: continue
```

## 7. 安全基线（必须）

- 下载域名白名单（禁止任意 URL）
- HTTPS + 证书校验（禁明文）
- `sha256` 必须匹配
- 组件元数据签名校验（公钥内置或包内受信锚点）
- 下载缓存目录不可执行（避免直接执行未校验文件）
- 安装动作最小权限原则（能非管理员就不提权）

## 8. 回滚策略建议

- 基础文件回滚与组件回滚分层
- 组件安装必须写入安装记录（product code/路径/版本）
- 回滚按“已执行成功的组件逆序”执行
- 回滚失败上报 `Rollback` 错误并保留审计日志

## 9. 实施顺序建议

1. 先实现 `load_component_manifest` + `download_component` + `verify_component`
2. 再实现 `install_component(kind=archive)` 与 `rollback_component`
3. 最后实现 `msi/exe` 类型、UI 动态控件渲染与完整 E2E
