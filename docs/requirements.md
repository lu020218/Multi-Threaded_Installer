# 需求文档

## 简介

本文档定义了将现有C++安装程序系统迁移到Rust + Tauri技术栈的需求。该系统包含打包器(packager)和安装器(installer)两个核心组件,当前使用DuiLib作为GUI框架,支持LZMA压缩。改造目标是使用Rust重写核心逻辑以提升内存安全性和并发性能,使用Tauri替代DuiLib实现现代化的跨平台GUI(优先支持Windows),并采用新的安装包格式。

## 术语表

- **打包器(Packager)**: 负责扫描输入目录、压缩文件并生成安装包的工具
- **安装器(Installer)**: 负责解析安装包、解压文件并执行安装流程的程序
- **安装包格式(Package_Format)**: 包含Header、TOC、Metadata、Data Blocks和Footer的二进制结构
- **目录表(TOC)**: Table of Contents,文件和块的索引表
- **元数据(Metadata)**: 包含应用名称、版本、安装配置等信息
- **数据块(Data_Block)**: 压缩后的文件数据单元
- **Tauri**: 基于Web技术的跨平台GUI框架
- **WebView2**: Windows平台的Web渲染引擎,Tauri在Windows上的依赖
- **平台抽象接口(Platform_Trait)**: 用于隔离Windows特定代码的trait接口
- **Zstd**: 默认压缩算法,提供高压缩比和快速解压
- **LZMA**: 备选压缩算法,提供更高压缩比
- **进度事件(Progress_Event)**: 用于向UI报告安装/打包进度的事件
- **安装器核心(Installer_Core)**: 核心安装逻辑模块,不依赖GUI框架
- **共享模块(Installer_Shared)**: 共享类型和配置模块
- **注册表(Registry)**: Windows注册表,用于存储安装信息和卸载配置
- **UI资源(UI_Resources)**: 包含HTML、CSS、JavaScript等前端文件
- **资源嵌入(Resource_Embedding)**: 将UI资源打包到安装器可执行文件中的过程
- **系统(System)**: 指整个安装程序系统,包括打包器和安装器

## 需求

### 需求 1: 新安装包格式

**用户故事:** 作为系统架构师,我希望定义一个新的安装包格式,以便支持更灵活的元数据和更高效的数据组织。

#### 验收标准

1. 安装包格式 SHALL 按顺序包含 Header、TOC、Metadata、Data_Blocks 和 Footer 五个部分
2. Header SHALL 包含魔数 b"MTI2"、版本号以及 TOC、Metadata 和 Data 部分的偏移量
3. TOC SHALL 包含文件条目,每个条目包含路径、大小、权限和块映射信息
4. TOC SHALL 包含块条目,每个条目包含偏移量、压缩大小、原始大小、校验和及算法标识符
5. Metadata SHALL 使用 MessagePack 或 CBOR 格式编码
6. Metadata SHALL 包含应用名称、版本、默认安装目录、供应商、许可文本、管理员权限要求标志、图标、UI主题和卸载信息
7. Footer SHALL 包含魔数 b"MTIF" 以及所有主要部分的偏移量,用于快速定位
8. 安装包格式 SHALL 对所有数值字段使用小端字节序
9. 安装包格式 SHALL NOT 与旧版 C++ 安装器格式兼容


### 需求 2: 打包器核心功能

**用户故事:** 作为开发者,我希望使用打包器扫描目录并生成安装包,以便分发我的应用程序。

#### 验收标准

1. WHEN 用户提供输入目录和配置时, 打包器 SHALL 递归扫描所有文件
2. WHEN 扫描文件时, 打包器 SHALL 收集文件路径、大小和权限信息
3. WHEN 压缩文件时, 打包器 SHALL 将数据划分为可配置大小的块(默认 4MB)
4. WHEN 压缩块时, 打包器 SHALL 默认使用 Zstd 算法,压缩级别为 3
5. WHERE 配置了 LZMA 压缩时, 打包器 SHALL 使用 LZMA 算法代替 Zstd
6. WHEN 生成 TOC 时, 打包器 SHALL 创建带校验和的文件条目和块条目
7. WHEN 生成 Metadata 时, 打包器 SHALL 将配置序列化为 MessagePack 格式
8. WHEN 写入安装包时, 打包器 SHALL 按顺序写入 Header、TOC、Metadata、Data_Blocks 和 Footer
9. WHEN 打包完成时, 打包器 SHALL 报告总压缩大小和压缩比
10. 打包器 SHALL 支持使用线程池并行压缩多个块

### 需求 3: 安装器核心功能

**用户故事:** 作为最终用户,我希望运行安装器来安装应用程序,以便在我的系统上使用该软件。

#### 验收标准

1. WHEN 用户运行安装器时, 安装器 SHALL 解析嵌入的安装包格式
2. WHEN 解析安装包时, 安装器 SHALL 验证 Header 魔数和版本号
3. WHEN 读取 TOC 时, 安装器 SHALL 将文件条目和块条目加载到内存
4. WHEN 读取 Metadata 时, 安装器 SHALL 将 MessagePack 数据反序列化为配置结构
5. WHEN 解压块时, 安装器 SHALL 使用块条目中指定的算法
6. WHEN 解压块时, 安装器 SHALL 验证校验和与预期值匹配
7. WHEN 写入文件时, 安装器 SHALL 在目标目录不存在时创建它们
8. WHEN 写入文件时, 安装器 SHALL 根据 TOC 条目设置文件权限
9. 安装器 SHALL 支持使用线程池并行解压多个块
10. WHEN 安装完成时, 安装器 SHALL 报告成功或失败及错误详情

### 需求 4: Tauri GUI实现

**用户故事:** 作为最终用户,我希望看到图形化的安装界面,以便直观地了解安装进度和选项。

#### 验收标准

1. 安装器GUI SHALL 使用 Tauri 框架实现跨平台图形界面
2. WHEN 安装器在 Windows 上启动时, 安装器GUI SHALL 检查 WebView2 运行时
3. IF WebView2 未安装, THEN 安装器GUI SHALL 显示安装提示及下载链接
4. WHEN WebView2 可用时, 安装器GUI SHALL 启动主安装窗口
5. 安装器GUI SHALL 显示欢迎页面,包含应用名称、版本和许可协议
6. 安装器GUI SHALL 显示安装目录选择页面,并提供默认路径建议
7. 安装器GUI SHALL 显示进度页面,包含当前文件、总体进度百分比和速度
8. 安装器GUI SHALL 显示完成页面,包含成功消息和启动应用程序选项
9. WHEN 安装进行中时, 安装器GUI SHALL 从安装器核心接收进度事件
10. WHEN 用户取消安装时, 安装器GUI SHALL 请求安装器核心中止并回滚

### 需求 5: UI资源动态加载

**用户故事:** 作为开发者,我希望能够修改UI界面文件后通过打包器重新生成安装器,而无需重新编译安装器代码,以便快速迭代UI设计。

#### 验收标准

1. 打包器 SHALL 在配置中接受 ui_resources 目录路径参数
2. WHEN 提供 ui_resources 目录时, 打包器 SHALL 扫描 HTML、CSS、JavaScript 和图像文件
3. 打包器 SHALL 将 UI资源 作为压缩归档嵌入到安装器可执行文件中
4. 安装器GUI SHALL 在运行时将嵌入的 UI资源 解压到临时目录
5. 安装器GUI SHALL 从解压的临时目录加载 HTML 页面
6. 安装器GUI SHALL 在安装器退出时清理临时 UI资源 目录
7. WHEN UI资源 未嵌入时, 安装器GUI SHALL 回退到默认内置 UI
8. 打包器 SHALL 在嵌入前验证 UI资源 结构(必须包含 index.html)
9. 安装器GUI SHALL 在解压前使用校验和验证 UI资源 完整性
10. 系统 SHALL 在开发模式下支持热重载 UI资源 而无需重新嵌入

### 需求 6: 平台抽象层

**用户故事:** 作为系统架构师,我希望通过trait隔离平台特定代码,以便未来扩展到macOS和Linux平台。

#### 验收标准

1. 平台抽象接口 SHALL 定义 default_install_dir 操作的接口
2. 平台抽象接口 SHALL 定义 ensure_admin 操作的接口
3. 平台抽象接口 SHALL 定义 create_shortcut 操作的接口
4. 平台抽象接口 SHALL 定义 register_uninstaller 操作的接口
5. 平台抽象接口 SHALL 定义 read_registry 操作的接口
6. 平台抽象接口 SHALL 定义 write_registry 操作的接口
7. WHEN 在 Windows 上运行时, 安装器核心 SHALL 使用 Windows 实现的平台抽象接口
8. Windows 平台实现 SHALL 使用 WinAPI 进行注册表操作
9. Windows 平台实现 SHALL 使用 WinAPI 创建快捷方式
10. 平台抽象接口实现 SHALL 在编译时使用 cfg 属性选择

### 需求 7: 进度报告机制

**用户故事:** 作为最终用户,我希望实时看到安装进度,以便了解安装过程的状态。

#### 验收标准

1. WHEN 安装器核心 处理文件时, 安装器核心 SHALL 发出进度事件消息
2. 进度事件 SHALL 包含阶段标识符(扫描、解压、写入、完成)
3. 进度事件 SHALL 包含当前项目计数和总项目计数
4. 进度事件 SHALL 包含正在处理的当前文件路径
5. 进度事件 SHALL 包含处理速度(字节/秒)
6. WHEN 安装器GUI 接收到进度事件时, 安装器GUI SHALL 更新进度条
7. WHEN 安装器GUI 接收到进度事件时, 安装器GUI SHALL 更新状态文本
8. WHEN 安装器CLI 接收到进度事件时, 安装器CLI SHALL 将进度打印到控制台
9. 进度事件 SHALL 通过通道从安装器核心传输到 UI 层
10. 进度事件传输 SHALL NOT 阻塞安装器核心操作

### 需求 8: 错误处理和回滚

**用户故事:** 作为最终用户,我希望安装失败时能够自动回滚,以便系统保持干净状态。

#### 验收标准

1. WHEN 解压失败时, 安装器 SHALL 记录错误及文件路径和错误代码
2. WHEN 校验和验证失败时, 安装器 SHALL 中止安装并报告损坏
3. WHEN 磁盘空间不足时, 安装器 SHALL 在写入文件前中止安装
4. WHEN 文件写入失败时, 安装器 SHALL 记录失败的文件路径
5. IF 安装被中止, THEN 安装器 SHALL 删除所有部分写入的文件
6. IF 安装被中止, THEN 安装器 SHALL 删除已创建的空目录
7. WHEN 回滚完成时, 安装器 SHALL 报告回滚成功或失败
8. 安装器 SHALL 对所有可能失败的操作使用 Result 类型
9. 安装器 SHALL 使用 anyhow 或 thiserror 传播带上下文的错误
10. 安装器 SHALL 将所有错误记录到日志文件以便调试

### 需求 9: 配置文件解析

**用户故事:** 作为开发者,我希望通过JSON配置文件定义打包选项,以便灵活控制打包行为。

#### 验收标准

1. WHEN 打包器启动时, 打包器 SHALL 在输入目录中搜索 packager.json
2. WHEN 找到 packager.json 时, 打包器 SHALL 将 JSON 解析为 PackagerConfig 结构
3. PackagerConfig SHALL 包含 application_name 字段
4. PackagerConfig SHALL 包含 version 字段
5. PackagerConfig SHALL 包含 default_install_dir 字段
6. PackagerConfig SHALL 包含 compression_algorithm 字段(Zstd 或 LZMA)
7. PackagerConfig SHALL 包含 compression_level 字段
8. PackagerConfig SHALL 包含 folder_targets 数组用于自定义安装路径
9. PackagerConfig SHALL 包含 registry_entries 数组用于 Windows 注册表操作
10. WHEN JSON 解析失败时, 打包器 SHALL 报告带字段名的验证错误

### 需求 10: 注册表操作

**用户故事:** 作为Windows用户,我希望安装器能够写入注册表,以便系统能够识别已安装的应用程序。

#### 验收标准

1. WHEN 在 Windows 上安装完成时, 安装器 SHALL 将卸载信息写入注册表
2. 安装器 SHALL 写入 InstallLocation 键及安装目录路径
3. 安装器 SHALL 写入 DisplayName 键及应用程序名称
4. 安装器 SHALL 写入 DisplayVersion 键及应用程序版本
5. 安装器 SHALL 写入 Publisher 键及供应商名称
6. 安装器 SHALL 写入 UninstallString 键及卸载程序可执行文件路径
7. 安装器 SHALL 写入 EstimatedSize 键及已安装大小(千字节)
8. WHEN 元数据包含自定义注册表条目时, 安装器 SHALL 写入这些条目
9. WHEN 卸载程序运行时, 安装器 SHALL 删除安装期间创建的所有注册表键
10. 安装器 SHALL 优雅地处理注册表访问错误并向用户报告

### 需求 11: 卸载功能

**用户故事:** 作为最终用户,我希望能够卸载应用程序,以便从系统中完全移除软件。

#### 验收标准

1. WHEN 安装完成时, 安装器 SHALL 在安装目录中创建卸载程序可执行文件
2. 安装器 SHALL 创建 install.manifest.json 文件,包含已安装文件和目录列表
3. WHEN 卸载程序运行时, 安装器 SHALL 读取 install.manifest.json
4. WHEN 卸载时, 安装器 SHALL 删除清单中列出的所有文件
5. WHEN 卸载时, 安装器 SHALL 删除空目录
6. WHEN 卸载时, 安装器 SHALL 删除安装期间创建的注册表键
7. WHEN 卸载时, 安装器 SHALL 删除已创建的桌面快捷方式
8. WHEN 卸载时, 安装器 SHALL 删除已创建的自动启动条目
9. WHEN 卸载完成时, 安装器 SHALL 删除 install.manifest.json 和卸载程序可执行文件
10. IF 卸载失败, THEN 安装器 SHALL 报告无法删除的文件或注册表键

### 需求 12: 命令行接口

**用户故事:** 作为系统管理员,我希望使用命令行参数控制安装器,以便实现自动化部署。

#### 验收标准

1. 安装器CLI SHALL 接受 --silent 标志用于非交互式安装
2. 安装器CLI SHALL 接受 --install-dir 参数指定安装目录
3. 安装器CLI SHALL 接受 --no-shortcuts 标志跳过快捷方式创建
4. 安装器CLI SHALL 接受 --no-registry 标志跳过注册表操作
5. 安装器CLI SHALL 接受 --uninstall 标志触发卸载
6. 安装器CLI SHALL 接受 --help 标志显示使用信息
7. WHEN 提供 --silent 标志时, 安装器CLI SHALL NOT 提示用户输入
8. WHEN 提供 --silent 标志且安装失败时, 安装器CLI SHALL 以非零代码退出
9. 安装器CLI SHALL 将进度消息打印到 stdout
10. 安装器CLI SHALL 将错误消息打印到 stderr

### 需求 13: 多线程压缩和解压

**用户故事:** 作为开发者,我希望打包和安装过程能够利用多核CPU,以便提高处理速度。

#### 验收标准

1. 打包器 SHALL 使用线程池并行压缩块
2. 打包器 SHALL 允许配置线程数(默认: CPU 核心数)
3. WHEN 并行压缩块时, 打包器 SHALL 在输出中保持块顺序
4. 安装器 SHALL 使用线程池并行解压块
5. 安装器 SHALL 允许配置线程数(默认: CPU 核心数)
6. WHEN 并行解压块时, 安装器 SHALL 按正确顺序写入文件
7. 打包器 SHALL 使用 rayon 或 crossbeam 进行线程池管理
8. 安装器 SHALL 使用 rayon 或 crossbeam 进行线程池管理
9. WHEN 线程池操作失败时, 系统 SHALL 报告带线程上下文的错误
10. 系统 SHALL 通过 Rust 所有权系统避免数据竞争

### 需求 14: 日志系统

**用户故事:** 作为开发者,我希望系统能够记录详细日志,以便调试问题和分析性能。

#### 验收标准

1. 系统 SHALL 使用 tracing crate 进行结构化日志记录
2. 系统 SHALL 使用 tracing-subscriber 配置日志输出
3. 系统 SHALL 在 DEBUG 级别记录详细操作跟踪
4. 系统 SHALL 在 INFO 级别记录正常操作消息
5. 系统 SHALL 在 WARN 级别记录可恢复错误
6. 系统 SHALL 在 ERROR 级别记录致命错误
7. WHEN 在 GUI 模式下运行时, 系统 SHALL 将日志写入临时目录中的文件
8. WHEN 在 CLI 模式下运行时, 系统 SHALL 将日志写入 stdout
9. 日志消息 SHALL 包含时间戳、级别、模块路径和消息内容
10. 系统 SHALL 支持按模块和级别过滤日志

### 需求 15: 权限提升

**用户故事:** 作为最终用户,我希望安装器能够在需要时请求管理员权限,以便安装到系统目录。

#### 验收标准

1. WHEN 元数据要求管理员权限时, 安装器 SHALL 检查当前权限级别
2. IF 当前用户不是管理员, THEN 安装器 SHALL 在 Windows 上显示 UAC 提示
3. WHEN UAC 提示被接受时, 安装器 SHALL 以提升的权限重新启动自身
4. WHEN UAC 提示被拒绝时, 安装器 SHALL 中止安装并显示错误消息
5. WHEN 安装目录需要管理员访问权限时, 安装器 SHALL 请求提升权限
6. 安装器 SHALL 检测 Program Files 目录并自动请求提升权限
7. 安装器 SHALL 检测系统目录并自动请求提升权限
8. WHEN 以提升的权限运行时, 安装器 SHALL 记录权限级别
9. 安装器 SHALL 使用 is_elevated crate 或 WinAPI 进行权限检测
10. 安装器 SHALL 优雅地处理提升错误并通知用户

### 需求 16: 磁盘空间检查

**用户故事:** 作为最终用户,我希望安装器在开始安装前检查磁盘空间,以便避免安装失败。

#### 验收标准

1. WHEN 安装开始时, 安装器 SHALL 从元数据计算所需的总磁盘空间
2. 安装器 SHALL 查询目标驱动器上的可用磁盘空间
3. IF 可用空间小于所需空间加 100MB 缓冲, THEN 安装器 SHALL 中止安装
4. 安装器 SHALL 向用户显示所需空间和可用空间
5. WHEN 磁盘空间检查失败时, 安装器 SHALL 建议备选安装目录
6. 安装器 SHALL 使用 fs2 crate 或平台 API 查询磁盘空间
7. 安装器 SHALL 在计算所需空间时考虑压缩比
8. 安装器 SHALL 在写入任何文件前检查磁盘空间
9. WHEN 安装期间磁盘空间变得不足时, 安装器 SHALL 中止并回滚
10. 安装器 SHALL 记录磁盘空间信息以便调试

### 需求 17: WebView2运行时检测

**用户故事:** 作为Windows用户,我希望安装器能够检测并引导安装WebView2,以便GUI能够正常运行。

#### 验收标准

1. WHEN 安装器GUI 在 Windows 上启动时, 安装器GUI SHALL 检查 WebView2 运行时
2. 安装器GUI SHALL 使用 WebView2 loader API 进行运行时检测
3. IF 未找到 WebView2 运行时, THEN 安装器GUI SHALL 显示安装提示
4. 安装提示 SHALL 包含 WebView2 运行时安装程序的下载链接
5. 安装提示 SHALL 包含不使用 GUI 继续的选项(CLI 模式)
6. WHEN 用户选择安装 WebView2 时, 安装器GUI SHALL 在浏览器中打开下载 URL
7. WHEN 用户选择 CLI 模式时, 安装器GUI SHALL 切换到控制台界面
8. WHEN WebView2 已安装时, 安装器GUI SHALL 允许用户重启安装器
9. 安装器GUI SHALL 记录 WebView2 检测结果
10. 安装器GUI SHALL 优雅地处理 WebView2 检测错误

### 需求 18: 快捷方式创建

**用户故事:** 作为最终用户,我希望安装器能够创建桌面快捷方式,以便快速启动应用程序。

#### 验收标准

1. WHEN 元数据启用 desktop_icons 标志时, 安装器 SHALL 创建桌面快捷方式
2. 安装器 SHALL 在安装目录中定位主可执行文件
3. 安装器 SHALL 使用应用程序名称作为快捷方式名称
4. 安装器 SHALL 将快捷方式目标设置为主可执行文件路径
5. 安装器 SHALL 将快捷方式工作目录设置为安装目录
6. WHEN 元数据中提供图标文件时, 安装器 SHALL 设置快捷方式图标
7. 安装器 SHALL 在用户桌面目录创建快捷方式
8. WHEN 快捷方式创建失败时, 安装器 SHALL 记录警告但继续安装
9. WHEN 卸载时, 安装器 SHALL 删除桌面快捷方式
10. 安装器 SHALL 在 Windows 上使用 mslnk crate 或 WinAPI 创建快捷方式

### 需求 19: 自动启动配置

**用户故事:** 作为最终用户,我希望应用程序能够在系统启动时自动运行,以便无需手动启动。

#### 验收标准

1. WHEN 元数据启用 auto_startup 标志时, 安装器 SHALL 配置自动启动
2. 安装器 SHALL 将注册表键写入 HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
3. 安装器 SHALL 使用应用程序名称作为注册表键名
4. 安装器 SHALL 将注册表值设置为主可执行文件路径
5. WHEN 自动启动配置失败时, 安装器 SHALL 记录警告但继续安装
6. WHEN 卸载时, 安装器 SHALL 删除自动启动注册表键
7. 安装器 SHALL 在配置自动启动前验证可执行文件路径存在
8. 安装器 SHALL 优雅地处理注册表访问错误
9. 安装器 SHALL 记录自动启动配置结果
10. 安装器 SHALL 支持通过配置禁用自动启动

### 需求 20: 版本检查

**用户故事:** 作为最终用户,我希望安装器能够检查Windows版本,以便确保系统满足最低要求。

#### 验收标准

1. WHEN 安装开始时, 安装器 SHALL 查询 Windows 版本
2. 安装器 SHALL 将当前版本与元数据中的最低要求版本进行比较
3. IF 当前版本低于最低版本, THEN 安装器 SHALL 中止安装
4. 安装器 SHALL 向用户显示当前版本和要求版本
5. 安装器 SHALL 检查主版本号、次版本号和构建号
6. 安装器 SHALL 使用 WinAPI GetVersionEx 或 RtlGetVersion 查询版本
7. WHEN 版本检查失败时, 安装器 SHALL 提供清晰的错误消息
8. 安装器 SHALL 记录 Windows 版本信息
9. 安装器 SHALL 优雅地处理版本查询错误
10. 安装器 SHALL 支持为测试目的绕过版本检查

### 需求 21: 进程检测和终止

**用户故事:** 作为最终用户,我希望安装器能够检测正在运行的应用程序实例,以便避免文件占用冲突。

#### 验收标准

1. WHEN 安装开始时, 安装器 SHALL 检查应用程序进程是否正在运行
2. 安装器 SHALL 使用元数据中的进程名称进行检测
3. IF 应用程序正在运行, THEN 安装器 SHALL 提示用户关闭应用程序
4. 安装器 SHALL 提供自动终止进程的选项
5. 安装器 SHALL 提供手动关闭后重试检测的选项
6. 安装器 SHALL 提供取消安装的选项
7. WHEN 用户选择终止进程时, 安装器 SHALL 按名称终止进程
8. WHEN 进程终止失败时, 安装器 SHALL 向用户报告错误
9. 安装器 SHALL 使用 sysinfo crate 或 WinAPI 进行进程检测
10. 安装器 SHALL 优雅地处理进程检测错误
