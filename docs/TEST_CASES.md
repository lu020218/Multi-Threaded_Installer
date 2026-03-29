# 测试用例清单

## 1. 目的

本清单用于覆盖以下核心能力：

- `packager.exe` 打包
- `installer.exe` GUI 安装与静默安装
- `installer.exe` GUI 修复
- `uninstaller.exe` GUI 卸载与静默卸载
- 旧版本升级清理
- DPI 缩放与资源加载
- 多语言桌面快捷方式
- 嵌入资源、日志、并发与异常场景

## 2. 测试环境建议

- 操作系统：Windows 10 22H2 / Windows 11 23H2 或更高
- 架构：x64
- DPI 缩放：100%、150%、200%
- 权限场景：
  - 标准用户触发提权
  - 管理员直接运行
- 安装路径场景：
  - 默认路径
  - 自定义路径
  - 新旧版本不同路径

## 3. 测试准备

### 3.1 构建产物

确保以下文件位于 `packager.exe` 同目录：

- `installer.exe`
- `uninstaller.exe`
- `resources/`

### 3.2 示例配置

准备一份可覆盖以下配置点的 `packager.yaml`：

- `legacyAppIds`
- `desktopShortcutName`
- `desktopShortcutNameI18n`
- `legacyDesktopShortcutNames`
- `cleanup.onUpgrade.registry.deleteFromManifest`
- `cleanup.onUpgrade.registry.legacyKeys`
- `cleanup.onUpgrade.extraPaths`
- 业务注册表：`InstallDir`、`InstallLocation`、`ExecutablePath`
- 可选组件
- `autoCleanOldInstall=true`

### 3.3 测试数据

准备：

- 一个旧版本安装包
- 一个新版本安装包
- 旧版本与新版本的 `applicationName` 不同的场景
- `zh_CN` 与 `en_US` 两种语言配置

## 4. 执行记录模板

每条用例建议记录：

- 用例编号
- 执行日期
- 构建版本
- 测试环境
- 实际结果
- 是否通过
- 日志路径
- 截图路径

## 5. 打包测试

### PKG-001 正常打包

- 前置条件：`installer.exe`、`uninstaller.exe`、`resources/` 均存在
- 步骤：
1. 运行 `packager.exe`
2. 指定有效 `packager.yaml`
- 预期结果：
1. 打包成功
2. 输出安装包成功生成
3. 日志中出现：
   - `Embedded UI resources into uninstaller template`
   - `Embedded UI resources into installer template`
4. 安装包内包含：
   - `resources.zip`
   - `uninstaller.exe`

### PKG-002 缺少 installer 模板

- 前置条件：移走 `installer.exe`
- 步骤：
1. 运行 `packager.exe`
- 预期结果：
1. 打包失败
2. 错误明确指出缺少当前目录下的 `installer.exe`

### PKG-003 缺少 uninstaller 模板

- 前置条件：移走 `uninstaller.exe`
- 步骤：
1. 运行 `packager.exe`
- 预期结果：
1. 打包失败
2. 错误明确指出缺少当前目录下的 `uninstaller.exe`

### PKG-004 缺少 resources 目录

- 前置条件：移走 `resources/`
- 步骤：
1. 运行 `packager.exe`
- 预期结果：
1. 打包失败
2. 错误明确指出缺少当前目录下的 `resources`

### PKG-005 JSON 配置不再支持

- 前置条件：提供 `packager.json`
- 步骤：
1. 用 `.json` 配置运行 `packager.exe`
- 预期结果：
1. 失败
2. 明确提示当前只支持 YAML 配置

## 6. GUI 安装测试

### INS-GUI-001 默认 GUI 安装

- 步骤：
1. 双击安装包
2. 使用默认路径安装
- 预期结果：
1. 欢迎页正常显示
2. 安装成功
3. 安装目录存在主程序、`uninstall.exe`、`install.manifest.json`
4. 卸载注册表写入成功

### INS-GUI-002 自定义安装路径

- 步骤：
1. 打开安装包
2. 选择自定义安装路径
3. 开始安装
- 预期结果：
1. 实际安装到指定路径
2. manifest 中 `installDir` 与 UI 选择一致

### INS-GUI-003 组件选择安装

- 前置条件：配置中存在多个可选组件
- 步骤：
1. 只勾选部分组件
2. 开始安装
- 预期结果：
1. 仅选中组件被安装
2. 组件相关文件、注册表、后置动作符合选择结果

### INS-GUI-004 桌面图标与开机启动

- 步骤：
1. 勾选桌面图标与开机启动
2. 完成安装
- 预期结果：
1. 桌面存在快捷方式
2. 启动项写入成功

### INS-GUI-005 不创建桌面图标与开机启动

- 步骤：
1. 取消勾选桌面图标与开机启动
2. 完成安装
- 预期结果：
1. 不创建桌面快捷方式
2. 不写入新的开机启动项

## 7. 静默安装测试

### INS-SIL-001 最小静默安装

- 步骤：
1. 运行：
```powershell
installer.exe --silent
```
- 预期结果：
1. 安装成功
2. 不弹出 GUI
3. 使用默认安装路径

### INS-SIL-002 指定安装路径

- 步骤：
1. 运行：
```powershell
installer.exe --silent --destination "D:\Apps\MyApp"
```
- 预期结果：
1. 安装成功
2. 实际安装到 `D:\Apps\MyApp`

### INS-SIL-003 指定组件

- 步骤：
1. 运行：
```powershell
installer.exe --silent --components componentA,componentB
```
- 预期结果：
1. 仅安装指定组件

### INS-SIL-004 安装全部组件

- 步骤：
1. 运行：
```powershell
installer.exe --silent --components all
```
- 预期结果：
1. 所有可选组件均安装

### INS-SIL-005 静默安装开机自启动与桌面图标

- 步骤：
1. 运行：
```powershell
installer.exe --silent --auto-startup true --desktop-icon true
```
- 预期结果：
1. 启动项存在
2. 桌面快捷方式存在

### INS-SIL-006 非法参数

- 步骤：
1. 运行：
```powershell
installer.exe --silent --repair
```
- 预期结果：
1. 失败
2. 明确提示静默安装不支持 `--repair`

## 8. 修复安装测试

### INS-REP-001 GUI 修复成功

- 前置条件：已安装旧版本或当前版本
- 步骤：
1. 运行：
```powershell
installer.exe --repair
```
- 预期结果：
1. 进入修复模式 GUI
2. 安装路径为已安装目录
3. 安装路径控件只读，浏览按钮禁用
4. 修复成功

### INS-REP-002 未安装时修复失败

- 前置条件：系统中不存在已安装实例
- 步骤：
1. 运行：
```powershell
installer.exe --repair
```
- 预期结果：
1. 明确失败
2. 不回落成普通安装

## 9. GUI 卸载测试

### UNINS-GUI-001 默认 GUI 卸载

- 前置条件：已安装
- 步骤：
1. 运行安装目录中的 `uninstall.exe`
2. 执行卸载
- 预期结果：
1. GUI 正常显示
2. 卸载成功
3. 主程序、manifest、卸载器被清理

### UNINS-GUI-002 桌面快捷方式删除

- 前置条件：安装时创建了桌面快捷方式
- 步骤：
1. 运行 `uninstall.exe`
2. 卸载
- 预期结果：
1. 按 `desktopShortcutDisplayName` 删除真实快捷方式
2. 历史快捷方式也按 `legacyDesktopShortcutNames` 删除

### UNINS-GUI-003 启动项删除

- 前置条件：安装时启用了开机启动
- 步骤：
1. 卸载
- 预期结果：
1. 开机启动项被删除

## 10. 静默卸载测试

### UNINS-SIL-001 最小静默卸载

- 前置条件：已安装
- 步骤：
1. 运行：
```powershell
uninstall.exe --silent
```
- 预期结果：
1. 卸载成功
2. 不显示 GUI

## 11. 升级安装与旧版本清理测试

### UPG-001 旧安装目录文件清理

- 前置条件：
  - 旧版本安装在 `D:\Apps\OldApp`
  - 新版本安装到 `D:\Apps\NewApp`
- 步骤：
1. 安装旧版本
2. 安装新版本
- 预期结果：
1. 旧目录中 manifest 记录的文件被删除
2. `uninstall.exe`、`install.manifest.json` 被删除
3. 空目录被清理

### UPG-002 旧桌面快捷方式清理

- 前置条件：
  - 旧版本快捷方式名称与新版本不同
  - `legacyDesktopShortcutNames` 已配置
- 步骤：
1. 安装旧版本
2. 安装新版本
- 预期结果：
1. 旧快捷方式被删除
2. 新快捷方式按当前语言名称创建

### UPG-003 旧开机启动项清理

- 前置条件：
  - 旧版本存在开机启动项
  - `legacyAppIds` 已配置
- 步骤：
1. 安装旧版本并开启开机启动
2. 安装新版本
- 预期结果：
1. 旧开机启动项被删除
2. 若新版本仍启用自启动，只保留新版本项

### UPG-004 旧业务注册表清理

- 前置条件：
  - 旧版本写入业务注册表
  - `cleanup.onUpgrade.registry.deleteFromManifest=true`
- 步骤：
1. 安装旧版本
2. 安装新版本
- 预期结果：
1. 旧 manifest 记录的注册表项被删除
2. 新版本本次应写入的注册表项仍存在

### UPG-005 显式 legacy registry keys 清理

- 前置条件：
  - `cleanup.onUpgrade.registry.legacyKeys` 已配置
- 步骤：
1. 手动创建旧注册表路径/值
2. 安装新版本
- 预期结果：
1. `legacyKeys` 中指定的注册表被删除

### UPG-006 额外目录清理

- 前置条件：
  - `cleanup.onUpgrade.extraPaths` 已配置
- 步骤：
1. 预先创建 `%AppData%` / `%LocalAppData%` 下的旧目录和文件
2. 安装新版本
- 预期结果：
1. 指定目录按规则清理
2. `onlyIfEmpty=true` 的非空目录不会被误删

### UPG-007 危险路径保护

- 前置条件：在 `extraPaths` 中故意配置危险路径或根目录
- 步骤：
1. 安装新版本
- 预期结果：
1. 危险路径不会被删除
2. 日志中有 warning
3. 安装不中断

## 12. 多语言桌面快捷方式测试

### I18N-001 中文安装语言快捷方式名

- 前置条件：配置 `desktopShortcutNameI18n.zh_CN`
- 步骤：
1. 以中文安装
2. 勾选桌面图标
- 预期结果：
1. 桌面快捷方式名称为中文
2. manifest 记录对应 `desktopShortcutDisplayName`

### I18N-002 英文安装语言快捷方式名

- 前置条件：配置 `desktopShortcutNameI18n.en_US`
- 步骤：
1. 以英文安装
2. 勾选桌面图标
- 预期结果：
1. 桌面快捷方式名称为英文

### I18N-003 语言切换后的升级清理

- 前置条件：
  - 旧版本中文安装
  - 新版本英文安装
- 步骤：
1. 安装旧版本
2. 安装新版本
- 预期结果：
1. 旧中文快捷方式被删除
2. 新英文快捷方式创建成功

## 13. DPI 与资源加载测试

### DPI-001 100% 启动

- 步骤：
1. 系统缩放设为 100%
2. 打开安装器
- 预期结果：
1. 窗口与控件完整显示
2. 无裁切、重叠、空白异常

### DPI-002 150% 启动

- 步骤：
1. 系统缩放设为 150%
2. 打开安装器
- 预期结果：
1. 初始布局正常
2. 日志显示 `dpi=144 scale=150%`
3. 页面图片 `@150` 资源正常命中

### DPI-003 200% 启动

- 步骤：
1. 系统缩放设为 200%
2. 打开安装器
- 预期结果：
1. 初始布局正常
2. 无控件偏移

### DPI-004 运行中 150% -> 200%

- 步骤：
1. 在 150% 打开安装器
2. 切换系统缩放到 200%
- 预期结果：
1. 窗口不会出现双重放大
2. 控件位置同步调整
3. 无明显错位

### DPI-005 运行中 150% -> 100%

- 步骤：
1. 在 150% 打开安装器
2. 切换系统缩放到 100%
- 预期结果：
1. 窗口不会被二次缩小
2. 全部控件仍可完整显示

### DPI-006 DPI 日志校验

- 步骤：
1. 执行 DPI 切换
2. 查看日志
- 预期结果：
1. 存在 `[GUI][DPI]`
2. 存在 `WM_DPICHANGED begin/end`
3. 存在当前页面 XML scope 与控件图片快照

## 14. 资源嵌入与临时目录测试

### RES-001 GUI 资源来源

- 步骤：
1. 启动安装器
2. 查看日志与临时目录
- 预期结果：
1. 从 EXE 提取 `resources.zip`
2. DuiLib 使用 ZIP 模式加载
3. 临时目录中不再预创建多余空目录

### RES-002 独立卸载器提取

- 步骤：
1. 完成安装
2. 查看安装目录
- 预期结果：
1. 存在独立 `uninstall.exe`
2. 该卸载器可独立运行

## 15. 日志与异常测试

### LOG-001 单次启动仅生成一个主日志

- 步骤：
1. 启动安装器
2. 查看 `%LOCALAPPDATA%\\MTInstaller`
- 预期结果：
1. 单次主流程仅生成一个主日志文件

### LOG-002 关键链路日志输出

- 步骤：
1. 执行安装
2. 查看日志
- 预期结果：
1. 有 `[GUI][RES]`
2. 有 `[GUI][DPI]`
3. 有安装阶段进度日志
4. 有升级清理 warning/info 日志

### LOG-003 资源缺失错误

- 前置条件：故意构造缺少 `resources.zip` 的异常包
- 步骤：
1. 启动安装器
- 预期结果：
1. GUI 启动失败
2. 明确错误提示
3. 不再有 console fallback

## 16. 并发与取消测试

### THR-001 大量组件安装

- 前置条件：多个组件、多个 embedded folder
- 步骤：
1. 执行安装
- 预期结果：
1. 安装成功
2. 无死锁
3. 进度正常推进

### THR-002 安装中取消

- 步骤：
1. GUI 安装开始
2. 进度页点击取消
- 预期结果：
1. 取消请求能够传递到真实后台 worker
2. 后台线程退出
3. 不崩溃、不挂死

### THR-003 取消后关闭窗口

- 步骤：
1. 安装中点击取消
2. 立即关闭窗口
- 预期结果：
1. 不发生 `std::terminate`
2. 无后台消息悬空崩溃

### THR-004 卸载中关闭窗口

- 步骤：
1. 卸载中关闭窗口
- 预期结果：
1. 线程能够正常回收
2. 无崩溃

## 17. 回归测试

### REG-001 旧安装定位

- 前置条件：系统存在旧版本
- 步骤：
1. 启动安装器
- 预期结果：
1. 仅通过 `InstallDir` 和卸载注册表两条路径定位旧安装
2. 行为稳定

### REG-002 packager 仅当前目录查找模板

- 步骤：
1. 在 `packager.exe` 同目录放模板
2. 在其他目录放同名旧模板
3. 运行打包
- 预期结果：
1. 只使用当前目录模板

### REG-003 embedded-only 分发模型

- 步骤：
1. 生成安装包
2. 只分发安装包本体
- 预期结果：
1. 不依赖外部 `resources/`
2. GUI 正常启动

## 18. 建议优先执行顺序

建议优先执行以下高价值回归：

1. `PKG-001`
2. `INS-GUI-001`
3. `INS-SIL-001`
4. `INS-REP-001`
5. `UNINS-GUI-001`
6. `UPG-001`
7. `UPG-003`
8. `UPG-004`
9. `I18N-001`
10. `DPI-002`
11. `DPI-004`
12. `THR-002`

