# Multi-Threaded Installer 自动化测试工程

对 **打包器（packager）/ 安装器（installer）/ 卸载器（uninstaller）** 的端到端自动化
测试工程，基于 Python + pytest，覆盖打包校验、安装/升级/卸载全生命周期、钩子脚本、
组件、锁定文件、语言参数、进度契约、编码规范与 GUI 自动化（Tier A + B）。

---

## 一、工程结构

```
tests/
├── run_tests.bat            一键入口（必须管理员）；报告输出到 reports/
├── pytest.ini               标记定义、插件屏蔽（串行执行）
├── requirements.txt         pytest + pytest-html
├── conftest.py              会话前置检查 / 用例隔离 / 失败兜底清扫 / 报告增强
├── helpers/                 工装层
│   ├── env.py               仓库路径、二进制定位、管理员检测、产品名前缀常量
│   ├── corpus.py            语料生成（可压缩文本 / 大文件独帧 / 中文路径），固定种子可重复
│   ├── packager.py          yaml 生成（单引号规避转义坑）+ packager.exe 调用
│   ├── installer.py         静默安装/升级/卸载调用 + CLI 进度帧捕获（\r 切分）
│   ├── logparse.py          安装器日志解析（TimingSummary / framed install / snapshot / 迁移行）
│   ├── sysstate.py          注册表·ARP·自启·快捷方式断言 + 全树 md5 + 五类残留扫描/清扫
│   └── gui.py               GUI 启动/关窗（ctypes WM_CLOSE，无外部依赖）+ 后门脚本写入
├── assets/
│   ├── fake_component.exe   假组件安装器（--sleep 秒数 / --rc 退出码），组件与脉冲测试用
│   └── fake_component.cpp   其源码
└── test_*.py                12 个测试模块（见下方用例清单）
```

### 断言来源三层

1. **exit code**（安装器退出码语义：0 成功 / 1 失败 / 4 rebootRequired）；
2. **安装器日志**（结构化解析 TimingSummary、`framed install ... skipped/written`、
   探测快照、迁移 fromVersion）；
3. **系统真实状态**（注册表 winreg 直读、ARP 条目、HKCU Run 自启、桌面 .lnk、
   安装目录全树逐文件 md5）。

### 隔离与安全

- 每用例独立产品名 `MtiAutoTest_<用例名>`；conftest 在用例结束（**含失败/异常**）时
  先尝试正常卸载，再强制清扫五类系统状态；
- 清扫带前缀防护（`sysstate._guard`）：仅允许作用于 `MtiAutoTest*` 产品，
  杜绝误删真实产品状态；
- 串行执行（安装器单实例互斥 + HKLM 写入决定不可并行）。

---

## 二、运行方式

前置：已构建 `build/Release/{packager,installer,uninstaller}.exe`
（锁定文件用例另需 `build/test/Release/FileLockSimulator.exe`）；
`pip install -r requirements.txt`；**管理员**命令行。

```bat
run_tests.bat                 :: 默认矩阵（合成语料，~2 分钟），生成 HTML 报告
run_tests.bat --real          :: 附加真实包全周期（需 set MTI_REAL_INPUT=<真实input目录>）
run_tests.bat -k upgrade      :: 透传 pytest 过滤表达式
python -m pytest -m hooks     :: 按标记只跑某一类
```

报告：`reports\report_YYYYMMDD_HHMMSS.html`（自包含单文件，含标题/环境信息/
每用例说明（取自 docstring）/结果/耗时/失败详情与捕获日志）。

---

## 三、用例清单（12 模块 / 26 用例）

### test_packager.py — 打包器（标记 `packager`）

| 用例 | 验证点 |
|---|---|
| test_missing_appname_rejected | yaml 缺必填 `app.appName` → 打包报错拦截 |
| test_appname_with_exe_suffix_rejected | `appName` 带 `.exe` 后缀 → 校验拦截（appName 为程序名不含扩展名） |
| test_full_package_with_icon_and_frames | 图标嵌入 + 分帧聚合（大文件独帧/小文件批帧，`mode=per-file-frames`）完整打包成功 |

### test_install_fresh.py — 全新安装（标记 `fresh`）

| 用例 | 验证点 |
|---|---|
| test_fresh_install_full | 全树逐文件 md5 零差异；双 folder 落点（app + plugins）；中文路径文件；manifest 顶层 `app` 对象与 yaml 字段逐一相等；产品注册表 Version/InstallDir；ARP DisplayName/DisplayVersion；HKCU Run 自启值 |
| test_shortcuts | 桌面快捷方式创建（`IShellLink::Save` 依赖交互式桌面；headless/服务会话按日志自动 skip，非产品缺陷） |

### test_upgrade.py — 升级（标记 `upgrade`）

| 用例 | 验证点 |
|---|---|
| test_same_content_all_skipped | 同内容升级：全部文件零读跳过（`skipped=N, written=0`） |
| test_incremental_precise_write | 改 3 个文件后升级：精准 `written=3`，升级后全树 md5 零差异 |
| test_same_version_rejected | 同版本包静默安装被版本门控拒绝（exit≠0） |
| test_registry_version_priority | 注册表 Version 改低（0.5.0）后同版本包放行；迁移日志 `from 0.5.0`（验证"注册表优先于 manifest"双读与迁移 fromVersion 贯通） |

### test_hooks.py — 钩子脚本（标记 `hooks`）

| 用例 | 验证点 |
|---|---|
| test_pre_post_sibling_keep | preInstall/postInstall 执行；`%VERSION%`/`%INSTALL_DIR%` 环境注入；主脚本 `call` 兄弟脚本（子目录）；`keep: true` 把主脚本+兄弟脚本保留到 keepDir |
| test_preinstall_abort_rolls_back | preInstall 非零退出 + `onFailure: abort` → 安装失败；无 ARP/manifest/自启残留；产品状态非 installed（引擎保留 install_failed 状态键供诊断） |

### test_components.py — 组件（标记 `components`，用 assets/fake_component.exe 冒充 chrome 槽）

| 用例 | 验证点 |
|---|---|
| test_default_component_installed | defaultSelected 组件默认安装（日志 `Component installed: chrome`） |
| test_missing_component_skipped | 组件安装器缺失：非 required → 跳过不失败（`skipped(missing)`） |
| test_components_empty_selects_required_only | `--components ""` 显式空集 → 仅装 required；chrome 被 `skipped(not-selected)` |

### test_uninstall.py — 卸载（标记 `uninstall`）

| 用例 | 验证点 |
|---|---|
| test_uninstall_full_sweep | 卸载后五项全清：产品注册表 / ARP / 自启 / 桌面快捷方式 / 安装目录（manifest 同删） |
| test_legacy_schema_manifest_uninstall | 把 manifest 降级为旧 schema（顶层 appId/appVersion + uninstallEntries.scope）后卸载：兼容双读，仍全清 |

### test_locked_files.py — 锁定文件（标记 `locked`）

| 用例 | 验证点 |
|---|---|
| test_locked_exe_pending_replace | FileLockSimulator 独占锁定已装 .exe 后升级：走 pending-replace（`RebootReplace registered`），`rebootRequired=true`，退出码 4 |

### test_language.py — 语言参数（标记 `language`）

| 用例 | 验证点 |
|---|---|
| test_language_normalized_and_written | `--language en-US` 归一为 `en_US`；注册表 `Language` 与 manifest `language` 双落盘 |
| test_upgrade_inherit_then_override | 升级不带参数 → 继承旧 manifest 语言；带 `--language zh_CN` → 显式覆盖 |

### test_progress.py — 进度契约（标记 `progress`）

| 用例 | 验证点 |
|---|---|
| test_fresh_progress_monotonic_with_midframes | CLI 进度帧捕获：落盘段（总进度 25%~70%）存在中途事件（分帧逐帧上报生效）；全序列单调不回退 |

### test_coding_standard.py — 编码规范（标记 `standard`）

| 用例 | 验证点 |
|---|---|
| test_no_banned_functions | `tools/check_banned_functions.py` 扫描 26 个禁用非安全 CRT 函数（memcpy/strcpy/sprintf/getenv 等）0 命中 |

### test_gui.py — GUI 自动化（标记 `gui`，Tier A + B）

| 用例 | 层 | 验证点 |
|---|---|---|
| test_gui_backdoor_install_to_custom_dir | B | 后门脚本驱动：勾选许可 → `editDir` 改路径 → 点 `btnInstall`，实际装入自定义目录、注册表 InstallDir 匹配 |
| test_gui_browse_injection_appends_product_dir | B | 点 `btnSelectDir` + 浏览结果注入 → `ResolveSelectedInstallPath` 追加产品目录名后安装（覆盖"浏览选目录"全自有代码链路） |
| test_gui_upgrade_autostart_zero_click | A | 先静默装 v1，GUI `--upgrade` 零点击自动升级 v2：走真实 GUI 管线（worker/进度页/完成页），注册表版本升至 2.0.0 |
| test_gui_single_instance | A | 双开 GUI 仅一个实例存活（单实例互斥 + 置顶） |

### test_real_package.py — 真实包全周期（标记 `real` + `slow`，默认不跑）

| 用例 | 验证点 |
|---|---|
| test_real_package_full_cycle | `MTI_REAL_INPUT` 指向真实 input：打包 v1/v2 → 全新安装 → 同内容升级全跳过 → 卸载全清（`run_tests.bat --real` 触发） |

---

## 四、GUI 自动化方案（Tier A + B）

DuiLib 为自绘 UI（所有控件画在同一 HWND，无 UIA/MSAA 支持），标准工具
（pywinauto/WinAppDriver）无法识别控件。本工程采用：

- **Tier A（零点击）**：`--upgrade` 的 GUI 自启模式驱动完整真实 GUI 管线；
  进程/窗口生命周期与单实例互斥。
- **Tier B（引擎测试驱动，env 门控，发布行为零变化）**：
  - `MTI_AUTOTEST_SCRIPT=<脚本>`：窗口就绪后按 300ms 定时器逐条执行
    `settext / setcheck / click / wait` 命令，经 `CPaintManagerUI::SendNotify`
    走与真实用户交互**完全相同**的 DuiLib 通知路由（非坐标点击，不受 DPI/皮肤改版影响）；
  - `MTI_AUTOTEST_BROWSE_RESULT=<路径>`：`ShowFolderBrowserDialog` 直接返回该路径、
    不弹原生对话框，使"浏览选目录 → 追加产品目录名"链路（含盘符根处理）可被覆盖。

  常用控件名（见 `resources/skins/welcome_page.xml`）：`license_checkbox`、`editDir`、
  `btnInstall`、`btnSelectDir`、`chkChrome`、`chkAutoRun`、`chkShotcut`。

  未选 UIA Provider 方案的原因：工作量约 20 倍（完整 Provider 树 + COM 生命周期/线程
  封送）、Provider 常驻发布版本扩大风险面（后门为 env 门控死代码）、需深改第三方框架、
  且对"自动化测试"目标的能力与后门重叠。若未来出现无障碍合规需求，可另行投入并迁移。

---

## 五、最近一轮结果与已知限制

- 最近全量：**24 passed / 1 skipped / 1 deselected，122s**（skipped =
  test_shortcuts，交互桌面限制；deselected = 真实包）。
- 已知环境限制：桌面/开始菜单快捷方式创建依赖交互式桌面；在服务/headless 会话
  （含部分 CI runner）下 `IShellLink::Save` 失败，用例自动 skip。

### 待补用例（工装已就绪，均为小增量）

- hook 超时 kill（timeoutSec 到点按失败处理）
- `onFailure: continue` 失败继续语义
- 组件失败退出码 / reboot 码（3010）聚合
- GUI 组件勾选框取消勾选（`setcheck chkChrome 0` → `skipped(not-selected)`）
- 盘符根目录浏览回归（`MTI_AUTOTEST_BROWSE_RESULT=D:\` → 回填 `D:\<产品名>` 且 UI 不冻结）
