# 需求文档

## 简介

本规范定义了为现有C++多线程安装程序添加图形用户界面（GUI）的需求。该安装程序当前是纯控制台应用程序，运行在Windows平台上，使用CMake构建，遵循C++17标准。核心功能包括多线程LZMA解压缩、文件夹映射、路径解析、进度回调机制、注册表操作和卸载支持。

新的GUI界面将提供简洁的三页式用户体验：欢迎页面（包含logo、版本信息和路径选择）、安装进度页面（显示实时进度）和完成页面（显示结果和后续操作选项），同时保持对静默安装模式的支持。

## 术语表

- **Installer**: 安装程序系统，负责将应用程序文件解压并安装到目标位置
- **GUI_Manager**: GUI管理器，负责创建和管理所有GUI窗口和页面
- **Page_Controller**: 页面控制器，管理安装向导的页面导航和状态转换
- **Progress_Callback**: 进度回调接口，用于从解压引擎向GUI传递进度信息
- **DecompressionEngine**: 解压引擎，现有的多线程LZMA解压缩组件
- **ConsoleInterface**: 控制台接口，现有的文本用户界面组件
- **Silent_Mode**: 静默模式，通过命令行参数启用的无UI安装模式
- **Win32_API**: Windows原生API，用于创建GUI组件
- **UI_Thread**: UI线程，运行GUI消息循环的独立线程
- **Worker_Thread**: 工作线程，执行实际安装操作的后台线程

## 需求

### 需求 1: GUI架构和线程模型

**用户故事:** 作为开发者，我希望GUI在独立线程中运行，这样安装过程不会阻塞用户界面，用户体验流畅。

#### 验收标准

1. THE GUI_Manager SHALL 在独立的UI_Thread中运行所有GUI操作
2. WHEN 安装操作执行时，THE Installer SHALL 在Worker_Thread中运行解压和文件操作
3. THE Progress_Callback SHALL 使用线程安全的消息传递机制从Worker_Thread向UI_Thread传递进度信息
4. WHEN 命令行参数包含 `-s` 标志时，THE Installer SHALL 跳过GUI初始化并使用ConsoleInterface
5. THE GUI_Manager SHALL 在应用程序启动时初始化COM库以支持文件对话框

### 需求 2: 欢迎页面

**用户故事:** 作为用户，我希望在一个页面中看到应用程序信息并配置安装选项，这样我可以快速开始安装过程。

#### 验收标准

1. WHEN 安装程序启动时，THE GUI_Manager SHALL 显示欢迎页面作为第一个页面
2. THE 欢迎页面 SHALL 在顶部显示应用程序logo图标
3. THE 欢迎页面 SHALL 显示应用程序名称和版本号
4. THE 欢迎页面 SHALL 提供"我同意安装协议"复选框
5. THE 欢迎页面 SHALL 在复选框文本中提供"安装协议"超链接
6. WHEN 用户点击"安装协议"超链接时，THE GUI_Manager SHALL 打开许可协议对话框
7. THE 欢迎页面 SHALL 提供安装路径选择区域，包含文本框显示默认路径
8. THE 欢迎页面 SHALL 提供"浏览"按钮打开文件夹选择对话框
9. WHEN 用户点击"浏览"按钮时，THE 欢迎页面 SHALL 显示Windows文件夹浏览对话框
10. WHEN 用户在对话框中选择文件夹时，THE 欢迎页面 SHALL 更新文本框中的路径
11. THE 欢迎页面 SHALL 显示所需的磁盘空间大小
12. THE 欢迎页面 SHALL 显示所选驱动器的可用磁盘空间
13. WHEN 可用空间小于所需空间时，THE 欢迎页面 SHALL 显示警告消息并禁用"安装"按钮
14. WHEN "我同意安装协议"复选框未选中时，THE 欢迎页面 SHALL 禁用"安装"按钮
15. WHEN "我同意安装协议"复选框被选中且磁盘空间充足时，THE 欢迎页面 SHALL 启用"安装"按钮
16. THE 欢迎页面 SHALL 提供"安装"按钮开始安装过程
17. THE 欢迎页面 SHALL 提供"取消"按钮退出安装程序
18. THE 欢迎页面 SHALL 使用现代扁平化设计风格

### 需求 3: 许可协议对话框

**用户故事:** 作为用户，我需要阅读许可协议并做出同意或不同意的决定，这样我可以了解使用条款。

#### 验收标准

1. WHEN 用户点击欢迎页面的"安装协议"超链接时，THE GUI_Manager SHALL 显示模态许可协议对话框
2. THE 许可协议对话框 SHALL 在可滚动的文本框中显示完整的许可协议文本
3. THE 许可协议对话框 SHALL 提供"同意"按钮
4. THE 许可协议对话框 SHALL 提供"不同意"按钮
5. WHEN 用户点击"同意"按钮时，THE 许可协议对话框 SHALL 关闭并返回欢迎页面
6. WHEN 用户点击"同意"按钮时，THE 欢迎页面 SHALL 自动选中"我同意安装协议"复选框
7. WHEN 用户点击"不同意"按钮时，THE 许可协议对话框 SHALL 关闭并返回欢迎页面
8. WHEN 用户点击"不同意"按钮时，THE 欢迎页面 SHALL 保持"我同意安装协议"复选框未选中状态
9. THE 许可协议对话框 SHALL 阻止用户与主窗口交互直到对话框关闭

### 需求 4: 安装进度页面

**用户故事:** 作为用户，我希望看到实时的安装进度，这样我可以了解安装状态和预计完成时间。

#### 验收标准

1. WHEN 用户从欢迎页面点击"安装"按钮时，THE Page_Controller SHALL 导航到进度页面并启动安装
2. THE 进度页面 SHALL 在顶部显示应用程序logo图标
3. THE 进度页面 SHALL 显示应用程序名称和版本号
4. THE 进度页面 SHALL 显示当前正在处理的文件夹名称
5. THE 进度页面 SHALL 显示进度条，反映0%到100%的完成百分比
6. THE 进度页面 SHALL 在进度条旁边显示数字百分比
7. WHEN DecompressionEngine调用Progress_Callback时，THE 进度页面 SHALL 更新进度条和文件夹名称
8. THE 进度页面 SHALL 显示预计剩余时间
9. THE 进度页面 SHALL 提供"取消"按钮以中止安装
10. WHEN 用户点击"取消"按钮时，THE 进度页面 SHALL 请求Worker_Thread停止安装并显示确认对话框

### 需求 5: 完成页面

**用户故事:** 作为用户，我希望看到安装结果的明确反馈并选择后续操作，这样我可以立即开始使用应用程序或访问相关资源。

#### 验收标准

1. WHEN 安装成功完成时，THE Page_Controller SHALL 导航到完成页面并显示成功消息
2. WHEN 安装失败时，THE Page_Controller SHALL 导航到完成页面并显示失败消息
3. THE 完成页面 SHALL 在顶部显示应用程序logo图标
4. THE 完成页面 SHALL 显示应用程序名称和版本号
5. WHEN 安装失败时，THE 完成页面 SHALL 显示详细的错误信息
6. THE 完成页面 SHALL 提供"立即运行应用程序"复选框（仅在成功时）
7. THE 完成页面 SHALL 提供"打开介绍网页"复选框（仅在成功时）
8. THE 完成页面 SHALL 提供"完成"按钮关闭安装程序
9. WHEN 用户选中"立即运行应用程序"并点击"完成"时，THE Installer SHALL 启动已安装的应用程序
10. WHEN 用户选中"打开介绍网页"并点击"完成"时，THE Installer SHALL 在默认浏览器中打开指定的介绍网页URL

### 需求 6: 高DPI支持

**用户故事:** 作为使用高分辨率显示器的用户，我希望GUI界面清晰锐利，这样我可以舒适地阅读所有文本和控件。

#### 验收标准

1. THE GUI_Manager SHALL 在应用程序清单中声明DPI感知
2. THE GUI_Manager SHALL 使用DPI缩放API调整所有控件大小
3. WHEN 系统DPI设置改变时，THE GUI_Manager SHALL 重新计算控件布局
4. THE GUI_Manager SHALL 使用矢量图标或高分辨率位图以保持清晰度

### 需求 7: 错误处理和用户反馈

**用户故事:** 作为用户，我希望在出现错误时收到清晰的反馈，这样我可以理解问题并采取适当的行动。

#### 验收标准

1. WHEN 发生错误时，THE GUI_Manager SHALL 显示模态对话框，包含错误描述
2. WHEN 路径无效时，THE 欢迎页面 SHALL 显示内联错误消息
3. WHEN 磁盘空间不足时，THE 欢迎页面 SHALL 显示警告图标和消息
4. WHEN 安装被用户取消时，THE Installer SHALL 清理部分安装的文件
5. THE 错误对话框 SHALL 提供"确定"按钮关闭对话框

### 需求 8: 页面导航和状态管理

**用户故事:** 作为用户，我希望在安装向导的页面之间流畅导航，这样我可以轻松地前进或取消安装过程。

#### 验收标准

1. THE Page_Controller SHALL 维护当前页面状态（欢迎、进度、完成）
2. WHEN 用户点击"安装"按钮时，THE Page_Controller SHALL 验证欢迎页面的输入并导航到进度页面
3. WHEN 用户点击"取消"按钮时，THE Page_Controller SHALL 显示确认对话框询问是否退出
4. WHEN 用户确认取消时，THE Installer SHALL 清理临时文件并退出应用程序
5. THE Page_Controller SHALL 根据当前页面状态启用或禁用导航按钮
6. WHEN 在进度页面时，THE Page_Controller SHALL 仅显示"取消"按钮
7. WHEN 在完成页面时，THE Page_Controller SHALL 仅显示"完成"按钮和复选框选项
8. THE Page_Controller SHALL 在页面切换时使用淡入淡出动画效果
9. THE GUI_Manager SHALL 在窗口标题栏显示当前页面名称
10. WHEN 许可协议对话框打开时，THE GUI_Manager SHALL 阻止用户与主窗口交互

### 需求 9: 键盘和辅助功能支持

**用户故事:** 作为用户，我希望使用键盘快捷键操作安装程序，这样我可以更高效地完成安装。

#### 验收标准

1. THE GUI_Manager SHALL 为所有按钮提供键盘快捷键（Alt+I安装，Alt+C取消，Alt+F完成）
2. WHEN 用户按下Enter键时，THE Page_Controller SHALL 触发当前页面的默认按钮
3. WHEN 用户按下Esc键时，THE Page_Controller SHALL 触发"取消"操作
4. THE GUI_Manager SHALL 支持Tab键在控件之间导航
5. THE GUI_Manager SHALL 为所有控件提供焦点指示器
6. THE 完成页面 SHALL 支持空格键切换复选框
7. THE 欢迎页面 SHALL 支持在文本框中直接输入路径

### 需求 10: 窗口行为和外观

**用户故事:** 作为用户，我希望安装程序窗口具有一致的外观和行为，这样我可以获得专业的用户体验。

#### 验收标准

1. THE GUI_Manager SHALL 创建固定大小的窗口（不可调整大小）
2. THE 窗口 SHALL 居中显示在屏幕上
3. THE 窗口 SHALL 显示应用程序图标在标题栏和任务栏
4. THE 窗口 SHALL 具有最小化按钮和关闭按钮
5. WHEN 用户点击关闭按钮时，THE GUI_Manager SHALL 触发"取消"操作
6. THE 窗口 SHALL 使用系统默认字体（Segoe UI）
7. THE 窗口 SHALL 使用现代扁平化配色方案（浅灰色背景，蓝色强调色）
8. THE 导航按钮 SHALL 固定在窗口底部，与页面内容分隔
9. THE 窗口 SHALL 在任务栏中显示为单独的应用程序

### 需求 11: 与现有架构集成

**用户故事:** 作为开发者，我希望GUI组件与现有代码库无缝集成，这样我可以最小化代码重构并保持系统稳定性。

#### 验收标准

1. THE GUI_Manager SHALL 使用现有的DecompressionEngine而不修改其接口
2. THE GUI_Manager SHALL 通过Progress_Callback接收进度更新
3. THE Installer SHALL 保持ConsoleInterface用于静默模式
4. THE GUI_Manager SHALL 使用现有的PathResolver进行路径验证
5. THE Installer SHALL 在main.cpp中根据命令行参数选择GUI或控制台模式
6. THE GUI_Manager SHALL 不修改现有的核心逻辑（解压、文件操作、路径解析）

### 需求 12: UI资源文件分离

**用户故事:** 作为开发者，我希望UI界面资源与C++代码分离，这样我可以修改UI外观而无需重新编译整个程序。

#### 验收标准

1. THE 项目 SHALL 使用Windows资源文件（.rc）定义所有UI元素
2. THE 资源文件 SHALL 包含对话框模板定义所有页面的布局和控件
3. THE 资源文件 SHALL 包含所有UI文本字符串资源
4. THE 资源文件 SHALL 包含应用程序图标、位图和其他图形资源
5. THE GUI_Manager SHALL 通过资源ID加载对话框模板而不是硬编码创建控件
6. THE GUI_Manager SHALL 通过LoadString API加载所有显示文本
7. WHEN 资源文件被修改时，THE 项目 SHALL 仅需重新编译资源文件并重新链接
8. THE 资源文件 SHALL 支持使用资源编辑器（如Visual Studio Resource Editor）进行可视化编辑
9. THE 资源文件 SHALL 包含版本信息资源（VERSIONINFO）
10. THE 资源文件 SHALL 使用清晰的资源ID命名约定（如IDD_WELCOME_PAGE, IDC_INSTALL_PATH）

### 需求 13: 构建和部署

**用户故事:** 作为开发者，我希望GUI组件易于构建和部署，这样我可以维护简单的构建流程。

#### 验收标准

1. THE 项目 SHALL 在CMakeLists.txt中添加GUI相关的源文件
2. THE 项目 SHALL 链接必要的Windows库（user32.lib, comctl32.lib, ole32.lib, shell32.lib）
3. THE 项目 SHALL 包含应用程序清单文件以声明DPI感知和通用控件支持
4. THE 项目 SHALL 包含Windows资源文件（.rc）定义UI布局和资源
5. THE 构建系统 SHALL 支持在有或没有GUI的情况下编译（通过CMake选项）
6. THE 安装程序可执行文件 SHALL 嵌入应用程序图标资源
