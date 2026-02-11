# 第三方依赖构建基线（Windows）

## 目标
固定 `lzma/duilib` 的依赖来源、运行时库策略和可校验指纹，避免因为本地替换库文件或误改构建参数导致不可预期的链接和运行问题。

## 固定策略
- GUI 安装器固定使用 `DuiLib` 静态库：`third_party/DuiLib_Ultimate/lib_static/DuiLib.lib`
- Windows 下固定使用 `liblzma` 静态库：`third_party/xz/lib_static/lzma.lib`
- GUI 打包场景固定使用 `/MT`（`STATIC_LINK_RUNTIME=ON`）
- `CMake` 配置阶段默认开启哈希校验：`VERIFY_THIRD_PARTY_HASHES=ON`

## 依赖版本与指纹
- `liblzma`
- 来源：`third_party/xz/lib_static/lzma.lib`
- 头文件版本：`5.8.2`（见 `third_party/xz/include/lzma/version.h`）
- SHA256：`229209de2a901b987543e1496a02d4874260c3b6f2f2f459bcd6fa2e7bc6f746`

- `DuiLib`
- 来源：`third_party/DuiLib_Ultimate/lib_static/DuiLib.lib`
- 构建约束（见 `third_party/DuiLib_Ultimate/CMakeLists.txt`）：`/MT`、`UNICODE`、`_UNICODE`、`UILIB_STATIC`
- SHA256：`6f7a28965d57daee3a213bd134ba2112f9f2cfdd842bef4b71ec028f8eb7bc54`

## CMake 已落地的约束
- `BUILD_GUI=ON` 且 `STATIC_LINK_RUNTIME=OFF` 时，直接 `FATAL_ERROR`
- `USE_DUILIB_STATIC` 通过 `CACHE ... FORCE` 固定为 `ON`
- 找不到 `third_party/xz/lib_static/lzma.lib` 时直接 `FATAL_ERROR`（不再自动回退 DLL）
- 配置阶段计算并校验两份静态库 SHA256；不匹配直接 `FATAL_ERROR`

## 升级依赖时需要同步的内容
1. 替换第三方静态库文件。
2. 更新 `CMakeLists.txt` 中：
- `EXPECTED_LIBLZMA_SHA256`
- `EXPECTED_DUILIB_SHA256`
3. 更新本文档中的版本与 SHA256。
4. 执行 `cmake --build build --config Release --clean-first` 验证。
