# 安装流程补齐 TODO

本文档基于“安装流程缺失点补齐方案”整理，为后续实现提供清单与落点位置。

## P0 - 必须补齐（高优先级）

### 1. 权限检测与提升
- 目标：安装到 `Program Files`/HKLM 时需管理员权限，否则提示或提升
- 落点：
  - Console: `src/installer/main.cpp`
  - GUI: `src/gui/installation_worker.cpp`
- TODO：
  - 启动时判断安装路径是否可写
  - 不可写时提示并中止 / 重新选择路径
  - 可选：`ShellExecute(..., "runas")` 重新拉起

### 2. 安装前置检测
- 目标：磁盘空间、系统版本、运行库等硬性阻断
- 落点：
  - `InstallationWorker::StartInstallation`
  - GUI 点击安装前拦截
- TODO：
  - 空间不足时阻止进入安装
  - 最小系统版本检查
  - 运行库检测（可配置）

## P1 - 强建议补齐（中高优先级）

### 3. 进程占用处理
- 目标：目标程序运行时提示关闭或终止
- 落点：
  - `InstallationWorker` 或 GUI 点击安装前
- TODO：
  - 进程检测（按 exe 名或路径）
  - 提示选择“关闭/重试/取消”
  - 可选：自动结束进程

### 4. 失败回滚
- 目标：安装失败时清理已写入内容
- 落点：
  - `InstallationWorker` 失败分支
- TODO：
  - 记录已创建文件/目录
  - 失败时删除文件、撤销注册表
  - 清理 installState/mutex

## P2 - 建议补齐（中优先级）

### 5. 完整性校验
- 目标：解压完成后进行文件级校验
- 落点：
  - `MetadataGenerator` / `ExtendedMetadata`
  - 安装完成后校验环节
- TODO：
  - 元数据扩展 file hash
  - 校验失败时回滚

### 6. 标准卸载注册表项
- 目标：写入系统卸载信息
- 落点：
  - `applyRegistryEntries` 之后
- TODO：
  - 写入 `HKLM\\...\\Uninstall`
  - DisplayName/Version/InstallLocation/UninstallString

## P3 - 可选增强

### 7. 断点恢复
- 目标：失败后可继续/恢复
- TODO：
  - 记录阶段状态
  - 启动时检测并恢复

