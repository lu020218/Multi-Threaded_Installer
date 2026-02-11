# 项目代码优化分阶段清单

## 阶段 1：稳定性修复（先做）
- [x] 接通安装取消链路：`OnCancelProgressButtonClick -> InstallationWorker::RequestCancellation -> WorkerThreadFunc`
  - 文件：`src/gui/gui_manager.cpp` `src/gui/installation_worker.cpp`
- [x] 去掉手动 `char**` 分配，改为 `std::vector<std::string>` 传参
  - 文件：`src/installer/main.cpp`
- [x] 为控制台安装流程增加顶层异常兜底并统一错误输出
  - 文件：`src/installer/main.cpp`
- [x] 统一失败退出码约定（安装失败、取消、权限不足）
  - 进度：`main.cpp` 与 `installation_worker.cpp` 已移除磁盘/系统版本/进程清理/旧安装清理等安装业务细节，改为构建 `InstallServiceOptions/InstallServiceCallbacks` 并调用统一服务；两端保留参数采集、权限交互与 UI/日志映射。
  - 文件：`src/installer/main.cpp` `src/gui/installation_worker.cpp` `src/installer/install_service.cpp`
- 验收标准：可取消安装、无泄漏、异常不崩溃、退出码稳定。

## 阶段 2：编码与文案治理
- [x] 清理乱码注释/字符串，统一源码编码为 UTF-8（无 BOM）
  - 进度：已清理 `src/` 与 `include/` 的非 ASCII 注释/字符串，统一为 UTF-8（无 BOM）；Release 编译中项目代码侧 `C4819` 已消失。
  - 文件：`src/installer/main.cpp` `src/gui/*.cpp` `include/common/*.h`
- [x] UI 文案全部迁移到语言资源文件，代码中去硬编码
  - 进度：已迁移 src/installer/main.cpp 与 src/gui（welcome/license/installation/progress/gui_manager/gui_helpers）核心文案；GetLocalizedText fallback 已统一清理为资源优先；resources/lang 中英文 key 已补齐并通过覆盖校验。
  - 文件：`src/gui/*.cpp` `resources/lang/*.xml`
- [x] 路径/字符串转换只保留公共工具，删除重复 helper
  - 进度：已统一到 `include/common/utf8_utils.*`（含 `TCharToWide/PathFromTChar/AcpToUtf8`），并移除 `toWideUtf8/stringToWString/wstringToUtf8` 等仅转发 helper；`src/gui`、`src/installer/main.cpp`、`src/installer/installer_helpers.cpp`、`src/installer/uninstall_manager.cpp` 均改为直接调用公共转换接口。
  - 文件：`src/common/utf8_utils.*` `src/gui/*.cpp` `src/installer/main.cpp` `src/installer/installer_helpers.cpp` `src/installer/uninstall_manager.cpp`
- 验收标准：中英环境显示正常，无乱码，转换实现单一来源。

## 阶段 3：架构去重
- [x] 抽出统一安装服务层（GUI/Console 共用）
  - 进度：已新增 `install_service` 服务层，统一封装安装执行/状态写入/manifest 生成/注册表与快捷方式收尾；`runConsoleInstaller` 与 `InstallationWorker` 均已切换调用。
  - 文件：`include/installer/install_service.h` `src/installer/install_service.cpp` `src/installer/main.cpp` `src/gui/installation_worker.cpp`
- [x] GUI 与 Console 仅保留参数采集和 UI/日志适配
  - 进度：`main.cpp` 与 `installation_worker.cpp` 已移除磁盘/系统版本/进程清理/旧安装清理等安装业务细节，改为构建 `InstallServiceOptions/InstallServiceCallbacks` 并调用统一服务；两端保留参数采集、权限交互与 UI/日志映射。
  - 文件：`src/installer/main.cpp` `src/gui/installation_worker.cpp` `src/installer/install_service.cpp`
- [x] 统一进度、错误、状态回调接口
  - 进度：已在服务层定义统一事件模型 `InstallServiceEvent`（进度/错误/告警/信息/状态）与单一回调 `onEvent`；Console 与 GUI 均改为基于同一事件流做日志/UI 映射。
  - 文件：`include/installer/install_service.h` `src/installer/install_service.cpp` `src/installer/main.cpp` `src/gui/installation_worker.cpp`
- 验收标准：同一安装逻辑只保留一份，GUI/Console 行为一致。

## 阶段 4：构建与依赖
- [x] 统一 CRT 配置，消除 `LNK4098/LNK4217`
  - 进度：保持全项目 `/MT` 与 `DuiLib.lib` 一致；对链接预编译 `lzma.lib` 的目标增加 MSVC 链接修正（`/NODEFAULTLIB:MSVCRT` 与忽略 `4098/4217`），`Release clean-first` 全量构建已验证 `LNK4098/LNK4217` 不再出现。
  - 文件：`CMakeLists.txt` 与构建配置
- [x] 固定第三方库编译选项（lzma/duilib）并记录版本
  - 进度：已在 `CMakeLists.txt` 固定 Windows 依赖策略（GUI 强制 `/MT`、`USE_DUILIB_STATIC=ON`、`lzma.lib` 缺失即失败）并新增 SHA256 校验；已新增 `docs/build_third_party_dependencies.md` 记录依赖版本、编译约束和升级流程。
  - 文件：`third_party/*` `docs/build_*`
- [x] 补充 CI 构建矩阵（Debug/Release）
  - 进度：已新增 GitHub Actions 工作流 `.github/workflows/windows-build.yml`，在 `windows-latest` 上执行 `Debug/Release` 双配置构建，并固定 `BUILD_GUI=ON`、`STATIC_LINK_RUNTIME=ON`、`VERIFY_THIRD_PARTY_HASHES=ON`。
  - 文件：CI 配置
- 验收标准：Release 构建无关键链接告警，CI 稳定。

## 阶段 5：测试回归
- [x] 增加安装/卸载回归测试（含中文路径、空格路径、长路径）
  - 进度：已新增 `tests/stage5_install_uninstall_regression.ps1`，自动执行三类目标路径（中文/空格/长路径）的静默安装与静默卸载回归，并校验 manifest、落盘文件与卸载清理结果；本地 `Release` 已全量跑通。
  - 文件：`tests/`（新增）
- [x] 增加 manifest 读写 round-trip 测试
  - 进度：已新增 `tests/test_manifest_roundtrip.cpp` 并接入 `BUILD_TESTS` 构建，覆盖 `writeManifest/readManifest` 的写-读-再写 round-trip 断言（含 files/registry/installState/killProcesses/language 字段）；`ctest -R manifest_roundtrip` 本地已通过。
  - 文件：`src/installer/uninstall_manager.cpp` 对应测试
- [x] 增加编码转换单元测试（UTF-8/ACP 边界）
  - 进度：已新增 `tests/test_utf8_utils.cpp` 并接入 `BUILD_TESTS` 构建，覆盖 UTF-8 严格解码失败、`MultiByteToWide/WideToMultiByte` 重载边界（含嵌入 `NUL` 与 `-1` 长度）、`AcpToUtf8` 与手工 ACP->Wide->UTF-8 一致性、`PathFromUtf8/Utf8FromPath/TCharToWide/PathFromTChar` 行为；`ctest -R utf8_utils` 本地已通过。
  - 文件：`src/common/utf8_utils.cpp` 对应测试
- 验收标准：关键路径自动化可回归，编码问题可被测试提前发现。


