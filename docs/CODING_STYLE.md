# 编码规范（Multi-Threaded Installer）

本文件固化项目现有约定，作为评审与新代码基线。自动化部分由仓库根的
`.clang-format` 与 `.editorconfig` 强制；本文档解释**约定与取舍**。

> 原则：新代码一律遵循本规范；存量不一致**随手改（碰到即改）**，不做一次性大规模 rename。

---

## 1. 格式（由 .clang-format / .editorconfig 强制）
- 缩进 4 空格，不用 Tab；行宽约 100 列。
- 大括号 K&R（附着同一行）：`if (x) {` / `void f() {`。
- 指针/引用左对齐：`Type* p`、`const T& r`。
- 命名空间不额外缩进，结尾保留 `// namespace` 注释。
- `switch` 的 `case` 缩进一层。
- 提交前建议 `clang-format -i` 仅格式化**改动的文件**（勿全量重排历史）。

---

## 2. 命名
| 实体 | 约定 | 示例 |
|------|------|------|
| 类型（class/struct/enum/enum class） | PascalCase | `InstallExecutionPlan`、`HookOnFailure` |
| **自由函数 / 静态函数 / 方法** | **PascalCase（新代码统一）** | `RunHook`、`BuildInstallExecutionPlan`、`ApplyInstallState` |
| 局部变量 / 函数参数 | camelCase | `installDir`、`folderId` |
| 常量 / 编译期值 | `k` 前缀 PascalCase | `kRequireAdmin`、`kMinWindowsBuild` |
| 命名空间 | PascalCase | `MultiThreadedInstaller`、`migration` |
| 宏 | 全大写下划线（尽量少用） | `WM_INSTALLATION_PROGRESS` |

**成员变量前缀**（历史遗留两套，按模块沿用、勿混用）：
- GUI 层（`src/gui`、`include/gui`）：`m_` 前缀 —— `m_installMetadata`。
- 引擎/打包层（其余）：尾下划线 —— `lastError_`。

> 注：存量自由函数有 camelCase（`writeRegistryValue`、`readManifest` 等老工具函数）。
> 不强制立即重命名；**新增**一律 PascalCase。

---

## 3. 头文件
- 一律 `#pragma once`。
- **头文件中禁止 `using namespace`**（包括 `using namespace DuiLib;`）。
  头里第三方/系统类型一律**全限定**：`DuiLib::CButtonUI`、`std::string`。
  `using namespace DuiLib;` 只允许出现在 `.cpp` 内。
- 重类型（如 `<windows.h>`、`UIlib.h`）尽量只在 `.cpp` 包含；头里能前置声明就前置声明。
- include 顺序：①本文件对应头 → ②项目内头 → ③第三方 → ④标准库 → ⑤平台头（`<windows.h>`）。
- **include 路径统一用模块前缀形式**：`"<module>/<subdir>/<file>.h"`，
  例如 `"installer/pipeline/install_service.h"`、`"common/utf8_utils.h"`、`"gui/core/gui_manager.h"`。
  **禁止相对路径**（`"../../include/..."`、`"../common/..."`）与裸同目录形式（`"foo.h"`）。

---

## 4. 目录结构
源码按模块 + 模块内职责分层（`src/` 与 `include/` 镜像）：
```
common/                       公共契约、编解码、工具
packager/                     打包器（构建期）
installer/  app pipeline payload state uninstall hooks platform migration
gui/        core pages dialogs presenters workers install
```
新增文件放进对应职责子目录；新增 `.cpp` 记得加进 `CMakeLists.txt` 的对应 `*_SOURCES`
（项目用显式列表，非 GLOB）。

---

## 5. 命名空间与可见性
- 顶层一律 `namespace MultiThreadedInstaller { ... }`；子系统用嵌套命名空间
  （`migration`、`MultiThreadedInstaller::GUIStatusPresenter`）。
- 文件私有的函数/类型放进**匿名命名空间**，不要用 `static` 文件级函数做新代码。
- 未引用的私有函数及时删除（避免 MSVC C4505）。

---

## 6. 错误处理（三种模式，按场景选用）
1. **`bool` 返回 + 出参 + `std::string& error`**：可恢复的校验/解析（loader、validator）。
2. **`std::error_code`**：文件系统等标准库可选错误（`std::filesystem::remove(p, ec)`）。
3. **异常**：仅在**边界**捕获兜底（如 `ExecuteInstallService` 的 `try/catch`），
   不用异常做常规控制流。
- 失败要记日志（见 §8），不要静默吞错。

---

## 7. 资源管理
- 优先 RAII；本地一次性资源用作用域守卫（参考 `ScopedComInit`、hook_runner 的 `ScopedRemove`）。
- 动态对象优先 `std::unique_ptr`/`std::shared_ptr`，避免裸 `new/delete`
  （DuiLib 由其框架托管的控件除外）。
- Win32 句柄（`HANDLE`/`HKEY`）务必在所有返回路径关闭；用 `common/win32_raii.h` 的
  `UniqueHandle`/`UniqueHKey`（move-only，`receive()` 配合 `RegOpenKeyExW` 等出参），
  避免手工 `CloseHandle`/`RegCloseKey` 在分支里漏掉。
  例外：刻意转移所有权、生命周期超出函数的句柄（如 `acquireInstallMutex` 返回、
  经 `PostMessage` 传递的负载指针）保持裸句柄/裸指针，不要用 RAII 套。

---

## 8. 日志
- 用 `logInstallerInfo/Warning/Error/Debug`（`common/installer_logger.h`）。
- 消息带 `[模块]` 前缀，便于过滤：`[Migration]`、`[InstallFlow][Cleanup]`、`[Hook]`、`[Rollback]`。

---

## 9. 引擎写死值 vs 配置
- 稳定机制取值写进 `common/engine_defaults.h`（`EngineDefaults::...`），不要散落魔数。
- 跨版本兼容逻辑一律走 `installer/migration` 的迁移表，**不**写进 YAML、**不**写进 hook bat。
- 状态/模式避免 stringly-typed；新代码优先 `enum class`，仅在序列化边界做字符串↔枚举转换。

---

## 10. 编码 / 文件
- 源文件 UTF-8、**不带 BOM**（编译器已加 `/utf-8`）。新文件勿引入 BOM。
- 行尾：代码文件随仓库现状；`.bat`/`.cmd` 用 CRLF（见 `.editorconfig`）。
- 文末保留一个换行；不留行尾空白。

---

## 11. 提交前自查清单
- [ ] `clang-format` 仅格式化改动文件，无多余 diff。
- [ ] 头文件无 `using namespace`、无相对 include。
- [ ] 新增 `.cpp` 已加进 `CMakeLists.txt`。
- [ ] `cmake --build build --config Release` 0 error、0 warning（含 C4505）。
- [ ] 失败路径有日志；句柄/内存无泄漏。
