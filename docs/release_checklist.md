# 发布前检查清单（当前状态）

更新时间：2026-02-03

## P0（必须完成）

- [x] 端到端验收（GUI + 静默）
  - [x] 主程序安装成功
  - [x] 可选组件批处理安装成功
  - [x] 人为注入失败后触发完整回滚
  - [x] 卸载后无残留（文件/目录/注册表）

- [x] 安全基线锁定
  - [x] 脚本默认禁用，需显式启用与白名单
  - [x] 组件下载支持 host allowlist
  - [x] 组件 SHA256 校验
  - [x] 组件签名校验（Ed25519）
  - [x] 公钥轮换与发布流程文档化

- [x] 回滚一致性
  - [x] 支持 `rollback_component` + `rollback_files`
  - [x] 覆盖多失败点回归（下载失败/校验失败/安装失败）
  - [x] 形成固定回归用例矩阵

- [x] 打包稳定性
  - [x] YAML-only 配置路径可用
  - [x] Flow/Script 内嵌与排除 payload 重复打包
  - [x] 重复构建哈希稳定性基准报告

- [x] 迁移与版本说明
  - [x] 新版仅支持 YAML
  - [x] 旧配置迁移说明（对外发布版）

## 发布门禁（Go / No-Go）

- [x] `cargo test --workspace -q`（发布候选版本）通过
- [x] 3 套样例包验收通过（基础包/组件包/失败回滚包）
- [x] 安全审查通过（脚本、下载白名单、签名校验）
- [x] 发布文档齐全（安装、打包、迁移、排障）

## P0 验收产物

- 回滚失败点回归用例：`installer_core/tests/integration_tests.rs`
  - `test_component_download_failure_triggers_rollback_cleanup`
  - `test_component_verify_failure_triggers_rollback_cleanup`
  - `test_component_install_failure_triggers_rollback_cleanup`
- 打包稳定性报告：`docs/package_determinism_report.md`
- 密钥轮换文档：`docs/component_key_rotation_runbook.md`
- YAML 迁移文档：`docs/packager_yaml_migration.md`
- 全仓测试：`cargo test --workspace -q` 通过

## P1 / P2（后续）

- [ ] MSI/EXE 退出码分类与错误映射
- [ ] ZIP 深度支持（安全解压、覆盖策略、权限策略）
- [ ] UI 动态控件模板（依赖、互斥、必选）
- [ ] 内置脚本运行时（替代 Node 依赖）
