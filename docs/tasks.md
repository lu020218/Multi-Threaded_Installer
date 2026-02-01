# 实现计划: Rust + Tauri 安装程序系统改造

## 概述

本实现计划将C++安装程序系统迁移到Rust + Tauri技术栈。采用增量开发方式,优先实现核心功能,然后逐步添加GUI和高级特性。每个任务都引用具体的需求编号以确保可追溯性。所有测试任务都是必需的,以确保从一开始就有全面的测试覆盖。

## 任务列表

- [x] 1. 设置项目结构和基础设施
  - 创建Cargo workspace,包含installer_shared、installer_core、packager_cli、installer_cli、installer_gui五个crate
  - 配置依赖项:serde、rmp-serde、zstd、lzma-rs、rayon、thiserror、anyhow、tracing、proptest
  - 设置CI/CD配置文件
  - _需求: 架构设计_

- [x] 2. 实现共享类型和错误处理
  - [x] 2.1 定义核心数据结构
    - 实现PackageHeader、TocHeader、FileEntry、BlockEntry结构体
    - 实现PackageMetadata、WindowsVersion、RegistryEntry结构体
    - 实现PackageFooter结构体
    - 添加#[repr(C)]和序列化支持
    - _需求: 1.1, 1.2, 1.3, 1.4, 1.6, 1.7_
  
  - [x] 2.2 定义错误类型
    - 使用thiserror定义InstallerError枚举
    - 实现各种错误变体:Io、InvalidFormat、ChecksumMismatch等
    - 定义Result类型别名
    - _需求: 8.8, 8.9_
  
  - [x] 2.3 定义配置模型
    - 实现PackagerConfig结构体及Default trait
    - 实现InstallOptions结构体
    - 实现FolderTarget、LocalizationConfig结构体
    - _需求: 9.3, 9.4, 9.5, 9.6, 9.7, 9.8, 9.9_
  
  - [x] 2.4 定义进度事件类型
    - 实现ProgressEvent结构体
    - 实现Phase枚举
    - 添加序列化支持用于跨进程传输
    - _需求: 7.2, 7.3, 7.4, 7.5_

- [x] 3. 实现安装包格式读写
  - [x] 3.1 实现Header读写
    - 实现write_header函数,写入魔数、版本号和偏移量
    - 实现read_header函数,验证魔数和版本
    - 确保使用小端字节序
    - _需求: 1.2, 1.8_
  
  - [x] 3.2 实现TOC读写
    - 实现write_toc函数,序列化文件条目和块条目
    - 实现read_toc函数,反序列化TOC
    - 计算并验证CRC32校验和
    - _需求: 1.3, 1.4, 2.6_
  
  - [x] 3.3 实现Metadata读写
    - 实现write_metadata函数,使用MessagePack序列化
    - 实现read_metadata函数,反序列化Metadata
    - _需求: 1.5, 1.6, 2.7_
  
  - [x] 3.4 实现Footer读写
    - 实现write_footer函数,写入魔数和偏移量
    - 实现read_footer函数,用于快速定位
    - _需求: 1.7_
  
  - [x] 3.5 编写包格式round-trip属性测试
    - **属性 1: 包格式Round-Trip一致性**
    - **验证需求: 1.1, 1.2, 3.1**

- [x] 4. 实现压缩和解压模块
  - [x] 4.1 实现Zstd压缩
    - 使用zstd crate实现块压缩
    - 支持配置压缩级别(默认3)
    - 计算压缩后的CRC32校验和
    - _需求: 2.4, 2.5_
  
  - [x] 4.2 实现LZMA压缩
    - 使用lzma-rs crate实现块压缩
    - 作为可选压缩算法
    - _需求: 2.5_
  
  - [x] 4.3 实现Zstd解压
    - 使用zstd crate实现块解压
    - 验证解压后的CRC32校验和
    - _需求: 3.5, 3.6_
  
  - [x] 4.4 实现LZMA解压
    - 使用lzma-rs crate实现块解压
    - 支持旧格式兼容(如需要)
    - _需求: 3.5_
  
  - [x] 4.5 编写校验和验证属性测试
    - **属性 4: 校验和完整性验证**
    - **验证需求: 3.6, 8.2**

- [x] 5. 实现文件系统操作
  - [x] 5.1 实现文件扫描
    - 使用walkdir递归扫描目录
    - 收集文件路径、大小、权限信息
    - 过滤隐藏文件和系统文件(可配置)
    - _需求: 2.1, 2.2_
  
  - [x] 5.2 实现块划分逻辑
    - 根据配置的块大小划分文件
    - 生成FileEntry和BlockEntry
    - _需求: 2.3_
  
  - [x] 5.3 实现文件写入
    - 创建目标目录
    - 写入文件内容
    - 设置文件权限
    - _需求: 3.7, 3.8_
  
  - [x] 5.4 实现磁盘空间检查
    - 使用fs2 crate查询可用空间
    - 计算所需空间(考虑压缩比)
    - 添加100MB缓冲
    - _需求: 16.1, 16.2, 16.3, 16.7, 16.8_
  
  - [x] 5.5 编写文件扫描完整性属性测试
    - **属性 11: 文件扫描完整性**
    - **验证需求: 2.1**
  
  - [x] 5.6 编写块划分一致性属性测试
    - **属性 3: 块划分一致性**
    - **验证需求: 2.3**

- [x] 6. 实现平台抽象层(Windows)
  - [x] 6.1 定义Platform trait
    - 定义所有平台操作的接口
    - 包括default_install_dir、ensure_admin、create_shortcut等
    - _需求: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6_
  
  - [x] 6.2 实现WindowsPlatform
    - 实现default_install_dir,返回Program Files路径
    - 实现is_elevated,使用is_elevated crate
    - 实现request_elevation,使用runas
    - _需求: 6.7, 6.8, 15.1, 15.2, 15.3, 15.9_
  
  - [x] 6.3 实现注册表操作
    - 使用winreg crate实现read_registry
    - 实现write_registry
    - 实现delete_registry
    - _需求: 6.5, 6.6, 10.1-10.8_
  
  - [x] 6.4 实现快捷方式创建
    - 使用mslnk crate实现create_shortcut
    - 设置目标路径、工作目录、图标
    - _需求: 6.3, 17.1-17.9_
  
  - [x] 6.5 实现进程操作
    - 使用sysinfo crate实现is_process_running
    - 实现terminate_process
    - _需求: 20.1, 20.2, 20.7, 20.9_
  
  - [x] 6.6 实现自动启动配置
    - 实现configure_auto_startup
    - 写入Run注册表键
    - _需求: 18.1-18.9_
  
  - [x] 6.7 实现版本检查
    - 使用WinAPI查询Windows版本
    - 比较主版本、次版本、构建号
    - _需求: 19.1, 19.2, 19.3, 19.5, 19.6_
  
  - [x] 6.8 编写注册表操作一致性属性测试
    - **属性 12: 注册表操作一致性**
    - **验证需求: 10.1, 10.2, 10.3, 18.2**

- [x] 7. 实现打包器核心逻辑
  - [x] 7.1 实现Packager结构体
    - 实现new方法,接受PackagerConfig
    - 初始化平台抽象层
    - _需求: 2.1_
  
  - [x] 7.2 实现scan_directory方法
    - 调用文件扫描模块
    - 应用folder_targets配置
    - _需求: 2.1, 2.2_
  
  - [x] 7.3 实现compress_blocks方法
    - 使用rayon并行压缩块
    - 发送进度事件
    - 保持块顺序
    - _需求: 2.10, 13.1, 13.2, 13.3_
  
  - [x] 7.4 实现generate_toc方法
    - 从压缩结果生成TOC
    - 计算所有校验和
    - _需求: 2.6_
  
  - [x] 7.5 实现generate_metadata方法
    - 从配置生成PackageMetadata
    - 序列化为MessagePack
    - _需求: 2.7_
  
  - [x] 7.6 实现build_package方法
    - 协调所有打包步骤
    - 写入完整的包格式
    - 返回PackageStats
    - _需求: 1.1, 2.8, 2.9_
  
  - [x] 7.7 编写并行压缩顺序保持属性测试
    - **属性 5: 并行压缩顺序保持**
    - **验证需求: 13.3**

- [x] 8. 实现UI资源嵌入
  - [x] 8.1 实现UIResources结构体
    - 实现from_directory方法,扫描UI资源目录
    - 压缩为tar.gz或zip归档
    - 计算CRC32校验和
    - _需求: 5.1, 5.2, 5.3_
  
  - [x] 8.2 实现UI资源验证
    - 验证必须包含index.html
    - 验证locales目录结构
    - _需求: 5.8_
  
  - [x] 8.3 实现UI资源嵌入到安装器
    - 将UIResources写入安装器可执行文件
    - 更新PackageHeader标志位
    - _需求: 5.3_
  
  - [x] 8.4 实现UI资源提取
    - 从安装器提取UI资源到临时目录
    - 验证校验和
    - _需求: 5.4, 5.9_
  
  - [x] 8.5 编写UI资源round-trip属性测试
    - **属性 7: UI资源Round-Trip**
    - **验证需求: 5.3, 5.9**

- [x] 9. 实现多语言支持
  - [x] 9.1 实现LocalizationManager
    - 实现load_from_resources方法
    - 解析JSON翻译文件
    - 实现get_text方法,支持变量插值
    - _需求: UI多语言支持_
  
  - [x] 9.2 实现语言检测
    - 实现detect_system_locale方法
    - 在Windows上使用GetUserDefaultLocaleName
    - _需求: UI多语言支持_
  
  - [x] 9.3 实现语言回退
    - 当翻译缺失时回退到默认语言
    - 记录缺失的翻译键
    - _需求: UI多语言支持_
  
  - [x] 9.4 编写多语言翻译完整性属性测试
    - **属性 21: 多语言翻译完整性**
    - **验证需求: UI多语言支持**

- [x] 10. 实现安装器核心逻辑
  - [x] 10.1 实现Installer结构体
    - 实现new方法,接受package_path
    - 初始化平台抽象层
    - _需求: 3.1_
  
  - [x] 10.2 实现parse_package方法
    - 读取并验证Header
    - 读取TOC和Metadata
    - _需求: 3.1, 3.2, 3.3, 3.4_
  
  - [x] 10.3 实现check_disk_space方法
    - 调用平台抽象层查询磁盘空间
    - 比较所需空间和可用空间
    - _需求: 16.1, 16.2, 16.3_
  
  - [x] 10.4 实现check_windows_version方法
    - 调用平台抽象层查询版本
    - 比较当前版本和最低要求
    - _需求: 19.1, 19.2, 19.3_
  
  - [x] 10.5 实现check_running_process方法
    - 调用平台抽象层检测进程
    - 返回进程是否运行
    - _需求: 20.1, 20.2, 20.3_
  
  - [x] 10.6 实现install方法
    - 使用rayon并行解压块
    - 写入文件到目标目录
    - 发送进度事件
    - _需求: 3.9, 3.10, 7.1_
  
  - [x] 10.7 实现rollback方法
    - 删除已安装的文件
    - 删除空目录
    - 删除注册表键
    - _需求: 8.5, 8.6, 8.7_
  
  - [x] 10.8 实现create_uninstaller方法
    - 复制安装器为uninstall.exe
    - 创建install.manifest.json
    - 写入卸载信息到注册表
    - _需求: 11.1, 11.2, 10.1-10.7_
  
  - [x] 10.9 编写并行解压顺序保持属性测试
    - **属性 6: 并行解压顺序保持**
    - **验证需求: 3.9, 13.6**
  
  - [x] 10.10 编写安装回滚完整性属性测试
    - **属性 8: 安装回滚完整性**
    - **验证需求: 8.5, 8.6**

- [x] 11. 实现卸载功能
  - [x] 11.1 实现manifest读取
    - 解析install.manifest.json
    - 加载已安装文件列表
    - _需求: 11.3_
  
  - [x] 11.2 实现文件删除
    - 删除manifest中的所有文件
    - 删除空目录
    - _需求: 11.4, 11.5_
  
  - [x] 11.3 实现注册表清理
    - 删除安装时创建的注册表键
    - _需求: 11.6_
  
  - [x] 11.4 实现快捷方式删除
    - 删除桌面快捷方式
    - 删除自动启动条目
    - _需求: 11.7, 11.8_
  
  - [x] 11.5 实现自清理
    - 删除manifest文件
    - 删除uninstall.exe自身
    - _需求: 11.9_
  
  - [x] 11.6 编写卸载round-trip属性测试
    - **属性 9: 卸载Round-Trip**
    - **验证需求: 11.4, 11.5, 11.6, 11.7, 11.8**

- [x] 12. 实现日志系统
  - [x] 12.1 配置tracing
    - 使用tracing和tracing-subscriber
    - 配置日志级别和过滤器
    - _需求: 14.1, 14.2, 14.10_
  
  - [x] 12.2 实现日志输出
    - GUI模式写入临时目录文件
    - CLI模式写入stdout
    - _需求: 14.7, 14.8_
  
  - [x] 12.3 实现结构化日志
    - 包含时间戳、级别、模块路径
    - 使用不同级别:DEBUG、INFO、WARN、ERROR
    - _需求: 14.3, 14.4, 14.5, 14.6, 14.9_
  
  - [x] 12.4 编写日志格式完整性属性测试
    - **属性 17: 日志格式完整性**
    - **验证需求: 14.9**

- [x] 13. 实现packager_cli
  - [x] 13.1 实现命令行参数解析
    - 使用clap解析参数
    - 支持input_dir、output_path、ui_resources_dir等
    - _需求: 2.1_
  
  - [x] 13.2 实现配置文件加载
    - 搜索packager.json
    - 解析JSON为PackagerConfig
    - 处理解析错误
    - _需求: 9.1, 9.2, 9.10_
  
  - [x] 13.3 实现进度显示
    - 订阅ProgressEvent
    - 在控制台显示进度条
    - _需求: 7.8_
  
  - [x] 13.4 实现主流程
    - 调用Packager::build_package
    - 处理错误并显示
    - 显示打包统计信息
    - _需求: 2.9_
  
  - [x] 13.5 编写配置序列化round-trip属性测试
    - **属性 2: 配置序列化Round-Trip**
    - **验证需求: 2.7, 9.2**

- [x] 14. 实现installer_cli
  - [x] 14.1 实现命令行参数解析
    - 使用clap解析参数
    - 支持--silent、--install-dir、--uninstall等
    - _需求: 12.1-12.6_
  
  - [x] 14.2 实现静默安装
    - 不提示用户输入
    - 使用默认或命令行指定的选项
    - _需求: 12.7, 12.8_
  
  - [x] 14.3 实现进度显示
    - 订阅ProgressEvent
    - 在控制台显示进度
    - _需求: 12.9, 7.8_
  
  - [x] 14.4 实现安装流程
    - 检查磁盘空间、版本、进程
    - 调用Installer::install
    - 处理错误和回滚
    - _需求: 3.10_
  
  - [x] 14.5 实现卸载流程
    - 检测--uninstall标志
    - 调用卸载逻辑
    - _需求: 12.5_
  
  - [x] 14.6 编写静默模式非交互性属性测试
    - **属性 18: 静默模式非交互性**
    - **验证需求: 12.7**

- [x] 15. 实现installer_gui (Tauri)
  - [x] 15.1 初始化Tauri项目
    - 创建tauri.conf.json
    - 配置窗口大小、标题、图标
    - _需求: 4.1_
  
  - [x] 15.2 实现WebView2检测
    - 检查WebView2运行时
    - 显示安装提示或回退到CLI
    - _需求: 17.1, 17.2, 17.3, 17.4, 17.5_
  
  - [x] 15.3 实现Tauri commands
    - start_install command
    - cancel_install command
    - query_metadata command
    - get_system_locale command
    - _需求: 4.9, 4.10_
  
  - [x] 15.4 实现Tauri events
    - install_progress event
    - install_error event
    - install_complete event
    - _需求: 4.9, 7.6, 7.7_
  
  - [x] 15.5 实现前端UI(Vue/React/Svelte)
    - 欢迎页面
    - 安装目录选择页面
    - 进度页面
    - 完成页面
    - _需求: 4.5, 4.6, 4.7, 4.8_
  
  - [x] 15.6 实现多语言UI
    - 加载翻译文件
    - 实现语言切换
    - 显示当前语言的文本
    - _需求: UI多语言支持_
  
  - [x] 15.7 实现UI资源加载
    - 从临时目录加载HTML/CSS/JS
    - 清理临时目录
    - _需求: 5.4, 5.5, 5.6_
  
  - [x] 15.8 编写进度事件完整性属性测试
    - **属性 10: 进度事件完整性**
    - **验证需求: 7.1, 4.9**

- [x] 16. 集成测试和端到端测试
  - [x] 16.1 编写完整安装流程测试
    - 打包 -> 安装 -> 验证文件
    - 验证注册表条目
    - 验证快捷方式
    - _需求: 所有核心需求_
  
  - [x] 16.2 编写卸载流程测试
    - 安装 -> 卸载 -> 验证清理
    - _需求: 11.1-11.10_
  
  - [x] 16.3 编写错误处理测试
    - 磁盘空间不足
    - 校验和错误
    - 权限不足
    - _需求: 8.1-8.10_
  
  - [x] 16.4 编写并发测试
    - 多线程压缩和解压
    - 验证无数据竞争
    - _需求: 13.1-13.10_

- [x] 17. 文档和示例
  - [x] 17.1 编写README
    - 项目介绍
    - 构建说明
    - 使用示例
  
  - [x] 17.2 编写API文档
    - 为所有公共API添加文档注释
    - 生成rustdoc
  
  - [x] 17.3 创建示例配置
    - 基础配置示例
    - 高级配置示例
    - 多语言配置示例
  
  - [x] 17.4 创建示例UI资源
    - 简单的HTML/CSS/JS模板
    - 多语言翻译文件示例

- [ ] 18. 最终检查点
  - 确保所有测试通过
  - 验证所有需求已实现
  - 检查代码质量和文档完整性
  - 如有问题,询问用户

## 注意事项

- 所有任务都是必需的,包括所有测试任务,以确保全面的测试覆盖
- 每个任务都引用了具体的需求编号,确保可追溯性
- 任务按依赖关系排序,后续任务依赖前面任务的完成
- 属性测试使用proptest crate,每个测试至少100次迭代
- 所有错误使用Result类型,通过?操作符传播
- 使用tracing记录所有关键操作
