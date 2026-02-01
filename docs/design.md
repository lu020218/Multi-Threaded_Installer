# 设计文档

## 概述

本设计文档描述了将现有C++安装程序系统迁移到Rust + Tauri技术栈的技术方案。系统采用模块化架构,分为五个主要crate:

- **installer_shared**: 共享类型、错误定义和配置模型
- **installer_core**: 核心安装和打包逻辑,不依赖GUI
- **packager_cli**: 命令行打包工具
- **installer_cli**: 命令行安装工具
- **installer_gui**: 基于Tauri的图形界面安装器

设计遵循以下原则:
1. 核心逻辑与UI完全解耦
2. 通过trait实现平台抽象,便于未来跨平台扩展
3. 使用Rust所有权系统保证内存安全
4. 采用新的安装包格式,不兼容旧版本
5. 支持UI资源动态嵌入,无需重新编译安装器

## 架构

### 整体架构图

```
┌─────────────────────────────────────────────────────────────┐
│                     Workspace Root                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────┐      ┌──────────────────┐           │
│  │  packager_cli    │      │  installer_cli   │           │
│  │  (CLI Tool)      │      │  (CLI Tool)      │           │
│  └────────┬─────────┘      └────────┬─────────┘           │
│           │                         │                      │
│           │                         │                      │
│           │         ┌───────────────┴──────────┐          │
│           │         │   installer_gui          │          │
│           │         │   (Tauri GUI)            │          │
│           │         └───────────┬──────────────┘          │
│           │                     │                          │
│           └─────────┬───────────┘                          │
│                     │                                      │
│           ┌─────────▼──────────┐                          │
│           │  installer_core    │                          │
│           │  (Core Logic)      │                          │
│           └─────────┬──────────┘                          │
│                     │                                      │
│           ┌─────────▼──────────┐                          │
│           │  installer_shared  │                          │
│           │  (Shared Types)    │                          │
│           └────────────────────┘                          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 模块职责

**installer_shared**:
- 定义核心数据结构(PackageHeader, TOC, Metadata等)
- 定义错误类型和Result别名
- 定义配置模型(PackagerConfig, InstallOptions等)
- 定义进度事件类型

**installer_core**:
- 实现安装包格式的读写
- 实现压缩和解压逻辑
- 实现文件系统操作
- 实现平台抽象trait
- 提供packager和installer的核心API

**packager_cli**:
- 解析命令行参数
- 调用installer_core进行打包
- 显示打包进度
- 处理UI资源嵌入

**installer_cli**:
- 解析命令行参数
- 调用installer_core进行安装
- 显示安装进度
- 支持静默安装

**installer_gui**:
- 使用Tauri框架
- 实现Web UI界面
- 通过Tauri command调用installer_core
- 通过Tauri event接收进度更新
- 处理WebView2检测和回退
- 支持多语言界面,自动检测系统语言
- 提供语言切换功能

## 组件和接口

### 1. 安装包格式

#### 1.1 PackageHeader

```rust
#[repr(C)]
pub struct PackageHeader {
    pub magic: [u8; 4],           // b"MTI2"
    pub version: u32,              // 格式版本号
    pub header_size: u32,          // Header自身大小
    pub toc_offset: u64,           // TOC起始偏移
    pub toc_size: u64,             // TOC大小
    pub metadata_offset: u64,      // Metadata起始偏移
    pub metadata_size: u64,        // Metadata大小
    pub data_offset: u64,          // 数据起始偏移
    pub data_size: u64,            // 数据总大小
    pub flags: u32,                // 标志位
    pub reserved: [u8; 28],        // 预留扩展
}
```

标志位定义:
- bit 0: 是否包含UI资源
- bit 1: 是否存在签名块
- bit 2: 是否压缩metadata

#### 1.2 TOC (Table of Contents)

```rust
pub struct TocHeader {
    pub file_count: u32,
    pub block_count: u32,
    pub toc_version: u32,
    pub reserved: u32,
}

pub struct FileEntry {
    pub path: String,              // 相对路径
    pub original_size: u64,        // 原始大小
    pub mode: u32,                 // 文件权限
    pub first_block_index: u32,    // 第一个块索引
    pub block_count: u32,          // 块数量
    pub checksum: u32,             // CRC32校验和
}

pub struct BlockEntry {
    pub offset: u64,               // 在数据区的偏移
    pub compressed_size: u64,      // 压缩后大小
    pub original_size: u64,        // 原始大小
    pub checksum: u32,             // CRC32校验和
    pub algorithm: CompressionAlgorithm,  // 压缩算法
}

pub enum CompressionAlgorithm {
    Zstd = 0,
    Lzma = 1,
}
```

#### 1.3 Metadata

使用MessagePack编码的结构:

```rust
#[derive(Serialize, Deserialize)]
pub struct PackageMetadata {
    pub app_name: String,
    pub version: String,
    pub default_install_dir: String,
    pub vendor: Option<String>,
    pub license_text: Option<String>,
    pub require_admin: bool,
    pub icon_path: Option<String>,
    pub ui_theme: Option<String>,
    pub min_windows_version: Option<WindowsVersion>,
    pub registry_entries: Vec<RegistryEntry>,
    pub auto_startup: bool,
    pub desktop_icons: bool,
    pub process_name: Option<String>,
    pub ui_resources_checksum: Option<u32>,  // UI资源校验和
}

#[derive(Serialize, Deserialize)]
pub struct WindowsVersion {
    pub major: u16,
    pub minor: u16,
    pub build: u32,
}

#[derive(Serialize, Deserialize)]
pub struct RegistryEntry {
    pub path: String,
    pub key: String,
    pub value: String,
    pub value_type: RegistryValueType,
}

#[derive(Serialize, Deserialize)]
pub enum RegistryValueType {
    String,
    Dword,
    ExpandString,
}
```

#### 1.4 Footer

```rust
#[repr(C)]
pub struct PackageFooter {
    pub footer_magic: [u8; 4],     // b"MTIF"
    pub header_offset: u64,        // Header起始偏移
    pub toc_offset: u64,           // TOC起始偏移
    pub metadata_offset: u64,      // Metadata起始偏移
    pub data_offset: u64,          // 数据起始偏移
    pub crc32: u32,                // 整个包的CRC32
    pub reserved: [u8; 12],        // 预留
}
```

### 2. 平台抽象层

```rust
pub trait Platform: Send + Sync {
    /// 获取默认安装目录
    fn default_install_dir(&self, app_name: &str) -> Result<PathBuf>;
    
    /// 确保管理员权限
    fn ensure_admin(&self) -> Result<()>;
    
    /// 创建快捷方式
    fn create_shortcut(&self, name: &str, target: &Path, icon: Option<&Path>) -> Result<()>;
    
    /// 注册卸载信息
    fn register_uninstaller(&self, info: &UninstallInfo) -> Result<()>;
    
    /// 读取注册表
    fn read_registry(&self, path: &str, key: &str) -> Result<String>;
    
    /// 写入注册表
    fn write_registry(&self, entry: &RegistryEntry) -> Result<()>;
    
    /// 删除注册表键
    fn delete_registry(&self, path: &str, key: &str) -> Result<()>;
    
    /// 检查是否以管理员权限运行
    fn is_elevated(&self) -> bool;
    
    /// 请求权限提升
    fn request_elevation(&self) -> Result<()>;
    
    /// 检查进程是否运行
    fn is_process_running(&self, name: &str) -> Result<bool>;
    
    /// 终止进程
    fn terminate_process(&self, name: &str) -> Result<()>;
    
    /// 配置自动启动
    fn configure_auto_startup(&self, app_name: &str, exe_path: &Path, enable: bool) -> Result<()>;
}

pub struct UninstallInfo {
    pub app_name: String,
    pub version: String,
    pub install_location: PathBuf,
    pub uninstall_exe: PathBuf,
    pub publisher: Option<String>,
    pub estimated_size_kb: u64,
}
```

Windows实现:

```rust
pub struct WindowsPlatform;

impl Platform for WindowsPlatform {
    // 使用WinAPI实现各个方法
    // 注册表操作使用winreg crate
    // 快捷方式使用mslnk crate
    // 权限检测使用is_elevated crate
    // 进程操作使用sysinfo crate
}
```

### 3. 打包器API

```rust
pub struct Packager {
    config: PackagerConfig,
    platform: Box<dyn Platform>,
}

impl Packager {
    pub fn new(config: PackagerConfig) -> Result<Self>;
    
    /// 扫描输入目录
    pub fn scan_directory(&self, input_dir: &Path) -> Result<Vec<FileInfo>>;
    
    /// 压缩文件块
    pub fn compress_blocks(
        &self,
        files: &[FileInfo],
        progress: impl Fn(ProgressEvent) + Send + Sync,
    ) -> Result<Vec<CompressedBlock>>;
    
    /// 生成TOC
    pub fn generate_toc(&self, blocks: &[CompressedBlock]) -> Result<Toc>;
    
    /// 生成Metadata
    pub fn generate_metadata(&self) -> Result<PackageMetadata>;
    
    /// 嵌入UI资源
    pub fn embed_ui_resources(&self, ui_dir: &Path) -> Result<Vec<u8>>;
    
    /// 构建安装包
    pub fn build_package(
        &self,
        input_dir: &Path,
        output_path: &Path,
        ui_resources_dir: Option<&Path>,
        progress: impl Fn(ProgressEvent) + Send + Sync,
    ) -> Result<PackageStats>;
}

pub struct PackageStats {
    pub total_files: usize,
    pub total_size: u64,
    pub compressed_size: u64,
    pub compression_ratio: f64,
}
```

### 4. 安装器API

```rust
pub struct Installer {
    package_path: PathBuf,
    platform: Box<dyn Platform>,
}

impl Installer {
    pub fn new(package_path: PathBuf) -> Result<Self>;
    
    /// 解析安装包
    pub fn parse_package(&self) -> Result<ParsedPackage>;
    
    /// 提取UI资源
    pub fn extract_ui_resources(&self, temp_dir: &Path) -> Result<()>;
    
    /// 检查磁盘空间
    pub fn check_disk_space(&self, install_dir: &Path) -> Result<()>;
    
    /// 检查Windows版本
    pub fn check_windows_version(&self) -> Result<()>;
    
    /// 检查进程
    pub fn check_running_process(&self) -> Result<bool>;
    
    /// 执行安装
    pub fn install(
        &self,
        options: InstallOptions,
        progress: impl Fn(ProgressEvent) + Send + Sync,
    ) -> Result<InstallStats>;
    
    /// 回滚安装
    pub fn rollback(&self, installed_files: &[PathBuf]) -> Result<()>;
    
    /// 创建卸载器
    pub fn create_uninstaller(&self, install_dir: &Path) -> Result<()>;
}

pub struct InstallOptions {
    pub install_dir: PathBuf,
    pub create_shortcuts: bool,
    pub configure_registry: bool,
    pub auto_startup: bool,
    pub silent: bool,
    pub thread_count: Option<usize>,
}

pub struct InstallStats {
    pub installed_files: usize,
    pub total_size: u64,
    pub elapsed_time: Duration,
}
```

### 5. 进度事件

```rust
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ProgressEvent {
    pub phase: Phase,
    pub current: u64,
    pub total: u64,
    pub current_file: Option<String>,
    pub speed_bps: Option<u64>,
    pub message: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Phase {
    Scanning,
    Compressing,
    Decompressing,
    Writing,
    Completing,
}
```

### 6. 错误处理

```rust
use thiserror::Error;

#[derive(Error, Debug)]
pub enum InstallerError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    
    #[error("Invalid package format: {0}")]
    InvalidFormat(String),
    
    #[error("Checksum mismatch: expected {expected:x}, got {actual:x}")]
    ChecksumMismatch { expected: u32, actual: u32 },
    
    #[error("Insufficient disk space: required {required} bytes, available {available} bytes")]
    InsufficientDiskSpace { required: u64, available: u64 },
    
    #[error("Unsupported compression algorithm: {0}")]
    UnsupportedAlgorithm(String),
    
    #[error("Platform error: {0}")]
    Platform(String),
    
    #[error("Configuration error: {0}")]
    Config(String),
    
    #[error("Process is running: {0}")]
    ProcessRunning(String),
    
    #[error("Permission denied: {0}")]
    PermissionDenied(String),
    
    #[error("Version check failed: {0}")]
    VersionCheckFailed(String),
}

pub type Result<T> = std::result::Result<T, InstallerError>;
```

## 数据模型

### 配置模型

```rust
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PackagerConfig {
    pub application_name: String,
    pub version: String,
    pub default_install_dir: String,
    pub vendor: Option<String>,
    pub license_text: Option<String>,
    pub icon_path: Option<String>,
    pub compression_algorithm: CompressionAlgorithm,
    pub compression_level: u8,
    pub block_size: usize,
    pub folder_targets: Vec<FolderTarget>,
    pub registry_entries: Vec<RegistryEntry>,
    pub require_admin: bool,
    pub auto_startup: bool,
    pub desktop_icons: bool,
    pub min_windows_version: Option<WindowsVersion>,
    pub process_name: Option<String>,
    pub ui_resources_dir: Option<PathBuf>,
}

impl Default for PackagerConfig {
    fn default() -> Self {
        Self {
            application_name: String::from("MyApp"),
            version: String::from("1.0.0"),
            default_install_dir: String::from("%ProgramFiles%"),
            vendor: None,
            license_text: None,
            icon_path: None,
            compression_algorithm: CompressionAlgorithm::Zstd,
            compression_level: 3,
            block_size: 4 * 1024 * 1024,  // 4MB
            folder_targets: Vec::new(),
            registry_entries: Vec::new(),
            require_admin: false,
            auto_startup: false,
            desktop_icons: false,
            min_windows_version: None,
            process_name: None,
            ui_resources_dir: None,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FolderTarget {
    pub folder_name: String,
    pub target_directory: String,
}
```

### 文件信息模型

```rust
pub struct FileInfo {
    pub path: PathBuf,
    pub relative_path: String,
    pub size: u64,
    pub mode: u32,
    pub modified: SystemTime,
}

pub struct CompressedBlock {
    pub id: u32,
    pub data: Vec<u8>,
    pub original_size: u64,
    pub compressed_size: u64,
    pub checksum: u32,
    pub algorithm: CompressionAlgorithm,
    pub files: Vec<FileEntry>,
}
```

### UI资源模型

```rust
pub struct UIResources {
    pub archive: Vec<u8>,      // 压缩的UI资源归档
    pub checksum: u32,          // CRC32校验和
    pub original_size: u64,     // 原始大小
    pub locales: Vec<String>,   // 支持的语言列表
}

impl UIResources {
    /// 从目录创建UI资源归档
    pub fn from_directory(dir: &Path) -> Result<Self>;
    
    /// 解压到临时目录
    pub fn extract_to(&self, temp_dir: &Path) -> Result<()>;
    
    /// 验证完整性
    pub fn verify(&self) -> Result<()>;
    
    /// 获取支持的语言列表
    pub fn supported_locales(&self) -> &[String];
}
```

### 多语言支持模型

```rust
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LocalizationConfig {
    pub default_locale: String,        // 默认语言(如"zh-CN")
    pub fallback_locale: String,       // 回退语言(如"en-US")
    pub supported_locales: Vec<String>, // 支持的语言列表
}

pub struct LocalizationManager {
    config: LocalizationConfig,
    translations: HashMap<String, HashMap<String, String>>,
}

impl LocalizationManager {
    /// 从UI资源加载翻译
    pub fn load_from_resources(resources_dir: &Path) -> Result<Self>;
    
    /// 获取翻译文本
    pub fn get_text(&self, key: &str, locale: &str) -> String;
    
    /// 检测系统语言
    pub fn detect_system_locale() -> String;
    
    /// 设置当前语言
    pub fn set_locale(&mut self, locale: &str) -> Result<()>;
}
```

UI资源目录结构:
```
ui_resources/
├── index.html
├── styles/
│   └── main.css
├── scripts/
│   └── main.js
└── locales/
    ├── zh-CN.json
    ├── en-US.json
    ├── ja-JP.json
    └── ...
```

翻译文件格式(JSON):
```json
{
  "welcome.title": "欢迎安装",
  "welcome.description": "这将在您的计算机上安装 {appName}",
  "install.directory": "安装目录",
  "install.progress": "正在安装...",
  "install.complete": "安装完成",
  "error.disk_space": "磁盘空间不足",
  "button.next": "下一步",
  "button.cancel": "取消",
  "button.finish": "完成"
}
```

## 正确性属性

*属性是一个特征或行为,应该在系统的所有有效执行中保持为真——本质上是关于系统应该做什么的形式化陈述。属性作为人类可读规范和机器可验证正确性保证之间的桥梁。*

在编写正确性属性之前,我需要使用prework工具分析验收标准的可测试性。

### 属性反思

在生成属性之前,我需要识别并消除冗余:

**识别的冗余:**
1. 需求1.1(包格式顺序)和需求1.2(Header包含偏移量)可以合并为一个综合属性,验证包格式的完整结构
2. 需求2.7(Metadata序列化)和需求9.2(配置解析)都是关于序列化的round-trip,可以合并
3. 需求3.1(包解析)已被需求1.1的round-trip属性覆盖
4. 需求3.6(校验和验证)和需求8.2(校验和失败处理)可以合并为一个属性
5. 需求10.1(注册表写入)和需求18.2(自动启动注册表)可以合并为通用的注册表操作属性

**保留的独特属性:**
- 包格式round-trip(合并1.1, 1.2, 3.1)
- 配置序列化round-trip(合并2.7, 9.2)
- 校验和验证(合并3.6, 8.2)
- UI资源round-trip(5.3, 5.9)
- 安装回滚(8.5)
- 卸载round-trip(11.4)
- 并发正确性(13.3)
- 注册表操作(合并10.1, 18.2)

### 正确性属性列表

**属性 1: 包格式Round-Trip一致性**

*对于任意*有效的文件集合和配置,打包后再解析应该产生等价的文件条目、块条目和元数据结构

**验证需求: 1.1, 1.2, 3.1**

**属性 2: 配置序列化Round-Trip**

*对于任意*有效的PackagerConfig对象,序列化为MessagePack后再反序列化应该产生等价的配置对象

**验证需求: 2.7, 9.2**

**属性 3: 块划分一致性**

*对于任意*文件和块大小配置,生成的块数量应该等于ceil(文件大小 / 块大小),且最后一个块的大小应该小于等于块大小

**验证需求: 2.3**

**属性 4: 校验和完整性验证**

*对于任意*数据块,如果数据被修改,则校验和验证应该失败并中止操作

**验证需求: 3.6, 8.2**

**属性 5: 并行压缩顺序保持**

*对于任意*文件集合,并行压缩的结果应该与串行压缩产生相同顺序的块序列

**验证需求: 13.3**

**属性 6: 并行解压顺序保持**

*对于任意*压缩包,并行解压的文件内容和顺序应该与串行解压完全相同

**验证需求: 3.9, 13.6**

**属性 7: UI资源Round-Trip**

*对于任意*UI资源目录,嵌入到安装器后再提取应该产生相同的文件内容和结构

**验证需求: 5.3, 5.9**

**属性 8: 安装回滚完整性**

*对于任意*安装过程,如果在中途中止,则所有已写入的文件和创建的目录应该被完全清理

**验证需求: 8.5, 8.6**

**属性 9: 卸载Round-Trip**

*对于任意*成功的安装,执行卸载后应该删除所有安装的文件、目录、注册表键和快捷方式

**验证需求: 11.4, 11.5, 11.6, 11.7, 11.8**

**属性 10: 进度事件完整性**

*对于任意*安装或打包操作,应该为每个处理的文件发出至少一个进度事件

**验证需求: 7.1, 4.9**

**属性 11: 文件扫描完整性**

*对于任意*目录结构,递归扫描应该发现所有常规文件,且不遗漏任何文件

**验证需求: 2.1**

**属性 12: 注册表操作一致性**

*对于任意*注册表条目,写入后立即读取应该返回相同的值

**验证需求: 10.1, 10.2, 10.3, 18.2**

**属性 13: 磁盘空间检查准确性**

*对于任意*安装包,如果目标磁盘可用空间小于(所需空间 + 100MB),则安装应该在写入任何文件前中止

**验证需求: 16.3**

**属性 14: 进程检测准确性**

*对于任意*进程名称,如果该进程正在运行,则进程检测应该返回true

**验证需求: 20.1, 20.3**

**属性 15: 版本检查准确性**

*对于任意*Windows版本要求,如果当前系统版本低于要求,则安装应该中止

**验证需求: 19.3**

**属性 16: 快捷方式创建验证**

*对于任意*应用程序,如果元数据启用desktop_icons,则安装后桌面目录应该包含对应的快捷方式文件

**验证需求: 17.1**

**属性 17: 日志格式完整性**

*对于任意*日志消息,输出应该包含时间戳、日志级别、模块路径和消息内容四个字段

**验证需求: 14.9**

**属性 18: 静默模式非交互性**

*对于任意*安装操作,如果使用--silent标志,则不应该有任何需要用户输入的交互提示

**验证需求: 12.7**

**属性 19: Manifest完整性**

*对于任意*安装操作,生成的install.manifest.json应该包含所有已安装的文件路径

**验证需求: 11.2**

**属性 20: 字节序一致性**

*对于任意*数值字段,在不同字节序的系统上写入和读取应该产生相同的值(使用小端序)

**验证需求: 1.8**

**属性 21: 多语言翻译完整性**

*对于任意*支持的语言,所有UI文本键都应该有对应的翻译,如果缺失则回退到默认语言

**验证需求: UI多语言支持**

**属性 22: 语言切换一致性**

*对于任意*支持的语言,切换语言后UI显示的所有文本应该使用该语言的翻译

**验证需求: UI多语言支持**

## 错误处理

### 错误分类

系统定义以下错误类别:

1. **IO错误**: 文件读写失败、权限不足
2. **格式错误**: 包格式无效、魔数不匹配
3. **校验和错误**: 数据损坏、完整性验证失败
4. **资源错误**: 磁盘空间不足、内存不足
5. **平台错误**: 注册表访问失败、进程操作失败
6. **配置错误**: JSON解析失败、字段验证失败
7. **版本错误**: Windows版本不满足要求
8. **权限错误**: 需要管理员权限但未提升

### 错误传播策略

1. 使用`Result<T, InstallerError>`作为所有可能失败操作的返回类型
2. 使用`thiserror`为库代码定义结构化错误
3. 使用`anyhow`为应用代码添加错误上下文
4. 错误应该包含足够的上下文信息用于调试
5. 关键错误应该触发回滚操作

### 回滚机制

安装器维护一个已安装文件列表,在以下情况触发回滚:

1. 校验和验证失败
2. 磁盘空间不足
3. 文件写入失败
4. 用户取消安装
5. 任何致命错误

回滚操作:
1. 删除所有已写入的文件
2. 删除已创建的空目录
3. 删除已写入的注册表键
4. 删除已创建的快捷方式
5. 记录回滚结果到日志

## 测试策略

### 双重测试方法

系统采用单元测试和属性测试相结合的策略:

**单元测试**:
- 验证特定示例和边界情况
- 测试错误处理路径
- 测试组件集成点
- 快速反馈,易于调试

**属性测试**:
- 验证通用属性在所有输入下成立
- 使用随机生成的测试数据
- 每个属性至少运行100次迭代
- 发现边界情况和意外行为

### 属性测试配置

使用`proptest` crate进行属性测试:

```rust
use proptest::prelude::*;

proptest! {
    #![proptest_config(ProptestConfig::with_cases(100))]
    
    #[test]
    fn test_package_roundtrip(
        files in prop::collection::vec(arbitrary_file_info(), 1..100)
    ) {
        // Feature: rust-tauri-migration, Property 1: 包格式Round-Trip一致性
        let packager = Packager::new(default_config())?;
        let package = packager.build_package(&files)?;
        let installer = Installer::new(package)?;
        let parsed = installer.parse_package()?;
        
        prop_assert_eq!(parsed.files.len(), files.len());
        for (original, parsed) in files.iter().zip(parsed.files.iter()) {
            prop_assert_eq!(original.path, parsed.path);
            prop_assert_eq!(original.size, parsed.size);
        }
    }
}
```

### 测试覆盖目标

- 核心逻辑代码覆盖率 > 80%
- 所有公共API都有单元测试
- 所有正确性属性都有属性测试
- 关键路径有集成测试

### 测试组织

```
tests/
├── unit/
│   ├── package_format_tests.rs
│   ├── compression_tests.rs
│   ├── platform_tests.rs
│   └── ...
├── property/
│   ├── roundtrip_tests.rs
│   ├── concurrency_tests.rs
│   ├── integrity_tests.rs
│   └── ...
└── integration/
    ├── install_flow_tests.rs
    ├── uninstall_flow_tests.rs
    └── ...
```

### 模拟和测试工具

1. **文件系统模拟**: 使用`tempfile`创建临时测试目录
2. **注册表模拟**: 使用测试专用注册表路径
3. **进程模拟**: 使用测试辅助进程
4. **时间模拟**: 使用可控的时间源
5. **随机数据生成**: 使用`proptest`的策略生成器

## 性能考虑

### 压缩性能

- 默认使用Zstd level 3,平衡压缩比和速度
- 支持配置压缩级别(1-22)
- 使用rayon并行压缩多个块
- 块大小默认4MB,可配置

### 解压性能

- 使用rayon并行解压多个块
- 预分配文件空间减少碎片
- 使用缓冲IO减少系统调用
- 支持配置线程数

### 内存使用

- 流式处理大文件,避免全部加载到内存
- 块大小限制单次内存使用
- 及时释放已处理的块
- 使用内存映射文件处理大包

### IO优化

- 批量写入减少系统调用
- 使用异步IO(可选)
- 预读取下一个块
- 缓存TOC和Metadata

## 安全考虑

### 输入验证

- 验证所有用户输入和配置
- 检查文件路径防止目录遍历
- 验证包格式防止恶意包
- 限制资源使用防止DoS

### 权限管理

- 最小权限原则
- 仅在必要时请求管理员权限
- 记录所有权限提升操作
- 验证目标路径权限

### 数据完整性

- 所有数据块使用CRC32校验
- 包级别使用整体校验和
- 验证UI资源完整性
- 检测数据损坏并中止

### 代码签名

- 支持对安装器进行代码签名
- 验证签名(可选)
- 记录签名信息到日志

## 部署和分发

### 构建流程

1. 编译installer_core、installer_shared
2. 编译packager_cli
3. 使用packager_cli打包应用
4. 可选:嵌入UI资源
5. 可选:代码签名
6. 生成最终安装器

### 依赖管理

- 静态链接Rust标准库
- 最小化外部依赖
- Windows上依赖WebView2运行时
- 提供WebView2离线安装包(可选)

### 版本兼容性

- 包格式版本号用于兼容性检查
- 不支持跨主版本升级
- 支持同主版本内的次版本升级
- 记录版本信息到日志和注册表
