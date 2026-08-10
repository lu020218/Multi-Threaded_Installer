# Multi-Threaded Installer 自动化测试工程

对 **打包器 / 安装器 / 卸载器** 的端到端自动化测试（Python + pytest）。

## 前置条件

1. 先构建 Release 三件套（`build/Release/{packager,installer,uninstaller}.exe`）；
   锁定文件用例还需 `build/test/Release/FileLockSimulator.exe`。
2. **以管理员身份运行**（测试真实读写 HKLM / 桌面 / 文件系统）。
3. 安装依赖：`pip install -r requirements.txt`。

## 运行

```bat
run_tests.bat                 :: 默认矩阵（合成语料），生成 HTML 报告到 reports\
run_tests.bat --real          :: 附加真实包全周期（需环境变量 MTI_REAL_INPUT=真实input目录）
run_tests.bat -k upgrade      :: 透传 pytest 过滤
python -m pytest -m hooks     :: 按标记只跑某类
```

报告：`reports\report_YYYYMMDD_HHMMSS.html`（自包含，含每用例说明/耗时/日志）。

## 覆盖矩阵（标记）

| 标记 | 模块 | 覆盖 |
|---|---|---|
| packager | test_packager | 缺 appName / .exe 后缀拦截、图标嵌入、分帧聚合 |
| fresh | test_install_fresh | 全树 md5、双 folder、中文路径、manifest app 对象、注册表/ARP/自启；快捷方式（交互桌面） |
| upgrade | test_upgrade | 同内容全跳过、增量精准写、同版本拒绝、注册表版本优先+迁移 |
| hooks | test_hooks | pre/post/兄弟脚本/keep、onFailure=abort 回滚 |
| components | test_components | 默认装、缺失跳过、--components 空集仅 required |
| uninstall | test_uninstall | 五项全清、旧 schema manifest 兼容 |
| locked | test_locked_files | 锁定 exe → pending-replace + reboot(exit=4) |
| language | test_language | --language 归一/注册表落盘/升级继承+覆盖 |
| progress | test_progress | 落盘段中途事件、全序列单调 |
| standard | test_coding_standard | 禁用函数扫描（tools/check_banned_functions.py） |
| gui | test_gui | Tier A 零点击升级/单实例；Tier B 后门驱动欢迎页交互+浏览注入 |
| real | test_real_package | 真实包打包+装+升级+卸载（默认 skip） |

## GUI 自动化（Tier A + B）

DuiLib 为自绘 UI，标准工具（pywinauto/UIA）无法识别控件，故采用：

- **Tier A（零点击）**：`--upgrade` 的 GUI 自启模式跑完整真实 GUI 管线；单实例互斥。
- **Tier B（引擎后门，env 门控，发布行为零变化）**：
  - `MTI_AUTOTEST_SCRIPT=脚本`：窗口就绪后按 300ms 定时器逐条执行
    `settext/setcheck/click/wait`，经 DuiLib 通知路由（等价真实交互）；
  - `MTI_AUTOTEST_BROWSE_RESULT=路径`：`ShowFolderBrowserDialog` 直接返回该路径、
    不弹原生对话框，覆盖"浏览选目录 → 追加产品目录名"链路（含盘符根处理）。

  控件名见皮肤 `resources/skins/welcome_page.xml`（`license_checkbox`/`editDir`/
  `btnInstall`/`btnSelectDir`/`chkChrome` 等）。

## 隔离与安全

- 每用例独立产品名 `MtiAutoTest_<用例名>`；`conftest` 在用例结束/失败时强制卸载 +
  五类系统状态清扫。
- 清扫仅允许作用于 `MtiAutoTest*` 产品（`sysstate._guard` 前缀防护），杜绝误删真实产品状态。
- 串行执行（单实例互斥 + HKLM 写入决定不并行）。

## 已知环境限制

- **桌面/开始菜单快捷方式** 依赖交互式桌面（`IShellLink::Save`）；在非交互/服务会话
  （含部分 CI）下 API 失败，`test_shortcuts` 会据日志自动 `skip`（非产品缺陷）。
