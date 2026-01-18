# GUI辅助功能实现文档

## 概述

本文档描述了任务8"实现辅助功能"的实现细节。该任务为GUI安装程序提供了一系列辅助函数，包括文件浏览对话框、磁盘空间查询、应用程序启动和错误处理对话框。

## 实现文件

### 头文件
- `include/gui/gui_helpers.h` - GUIHelpers类的声明

### 源文件
- `src/gui/gui_helpers.cpp` - GUIHelpers类的实现

### 测试文件
- `tests/test_gui_helpers.cpp` - 单元测试

## 功能实现

### 8.1 文件浏览对话框

**函数**: `GUIHelpers::ShowFolderBrowserDialog()`

**功能**: 显示Windows文件夹选择对话框，允许用户选择安装目录。

**实现细节**:
- 使用Shell API的`SHBrowseForFolder()`函数
- 自动初始化COM库（如果尚未初始化）
- 支持设置初始路径
- 使用`BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI`标志以获得现代化外观
- 通过回调函数设置初始选择的文件夹

**使用示例**:
```cpp
std::wstring selectedPath;
if (GUIHelpers::ShowFolderBrowserDialog(
    hWnd,
    L"请选择安装目录",
    L"C:\\Program Files",
    selectedPath)) {
    // 用户选择了路径
    std::wcout << L"Selected: " << selectedPath << std::endl;
}
```

**满足需求**: 1.5, 2.8, 2.9, 2.10

### 8.2 磁盘空间查询

**函数**:
- `GUIHelpers::GetAvailableDiskSpace()` - 获取可用磁盘空间
- `GUIHelpers::FormatBytes()` - 格式化字节数为可读字符串
- `GUIHelpers::CheckDiskSpace()` - 检查磁盘空间是否充足

**功能**: 查询指定路径的可用磁盘空间，并提供格式化显示。

**实现细节**:
- 使用`GetDiskFreeSpaceExW()` API查询磁盘空间
- 自动提取驱动器根路径（支持绝对路径和UNC路径）
- 格式化字节数为B、KB、MB、GB、TB单位
- 提供便捷的空间检查函数

**使用示例**:
```cpp
// 获取可用空间
uint64_t space = GUIHelpers::GetAvailableDiskSpace(L"C:\\Program Files");
std::wcout << L"Available: " << GUIHelpers::FormatBytes(space) << std::endl;

// 检查空间是否充足
uint64_t availableBytes;
bool hasSpace = GUIHelpers::CheckDiskSpace(
    L"C:\\Program Files",
    100 * 1024 * 1024,  // 需要100MB
    availableBytes);
if (!hasSpace) {
    std::wcout << L"Insufficient disk space!" << std::endl;
}
```

**满足需求**: 2.11, 2.12, 2.13, 7.2, 7.3

### 8.3 应用程序启动和网页打开

**函数**:
- `GUIHelpers::LaunchApplication()` - 启动已安装的应用程序
- `GUIHelpers::OpenWebPage()` - 在默认浏览器中打开网页

**功能**: 在安装完成后启动应用程序或打开介绍网页。

**实现细节**:
- 使用`ShellExecuteW()` API启动应用程序和打开URL
- 验证可执行文件是否存在
- 自动确定工作目录（默认为可执行文件所在目录）
- 验证URL格式（必须以http://或https://开头）
- 返回值检查（ShellExecute返回值>32表示成功）

**使用示例**:
```cpp
// 启动应用程序
if (!GUIHelpers::LaunchApplication(
    L"C:\\Program Files\\MyApp\\app.exe",
    L"C:\\Program Files\\MyApp")) {
    std::wcout << L"Failed to launch application" << std::endl;
}

// 打开网页
if (!GUIHelpers::OpenWebPage(L"https://example.com")) {
    std::wcout << L"Failed to open web page" << std::endl;
}
```

**满足需求**: 5.9, 5.10

### 8.4 错误处理和对话框

**函数**:
- `GUIHelpers::ShowErrorDialog()` - 显示错误对话框
- `GUIHelpers::ShowWarningDialog()` - 显示警告对话框
- `GUIHelpers::ShowConfirmDialog()` - 显示确认对话框
- `GUIHelpers::ShowInfoDialog()` - 显示信息对话框

**功能**: 提供统一的错误处理和用户反馈机制。

**实现细节**:
- 使用`MessageBoxW()` API显示对话框
- 使用适当的图标（错误、警告、问号、信息）
- 确认对话框返回布尔值（是/否）
- 其他对话框仅显示"确定"按钮

**使用示例**:
```cpp
// 显示错误
GUIHelpers::ShowErrorDialog(
    hWnd,
    L"错误",
    L"安装失败：磁盘空间不足");

// 显示警告
GUIHelpers::ShowWarningDialog(
    hWnd,
    L"警告",
    L"无法启动应用程序");

// 显示确认对话框
if (GUIHelpers::ShowConfirmDialog(
    hWnd,
    L"确认",
    L"确定要退出安装程序吗？")) {
    // 用户点击了"是"
    Close();
}

// 显示信息
GUIHelpers::ShowInfoDialog(
    hWnd,
    L"提示",
    L"安装成功完成！");
```

**满足需求**: 7.1, 7.2, 7.3, 7.5, 8.3, 8.4

## 辅助函数

### COM库管理

**函数**:
- `GUIHelpers::InitializeCOM()` - 初始化COM库
- `GUIHelpers::UninitializeCOM()` - 反初始化COM库

**功能**: 管理COM库的初始化和清理，用于文件对话框。

**实现细节**:
- 使用`CoInitializeEx()`初始化COM（单线程模式）
- 跟踪初始化状态，避免重复初始化
- 处理各种返回值（S_OK、S_FALSE、RPC_E_CHANGED_MODE）
- 在文件浏览对话框中自动管理COM生命周期

### 路径验证

**函数**:
- `GUIHelpers::ValidatePath()` - 验证路径格式
- `GUIHelpers::ExtractRootPath()` - 提取驱动器根路径

**功能**: 验证和处理文件路径。

**实现细节**:
- 支持绝对路径（C:\...）
- 支持UNC路径（\\server\share\...）
- 检查非法字符
- 提取驱动器根路径用于磁盘空间查询

## 集成到现有代码

### GUIManager集成

`gui_manager.cpp`已更新以使用新的辅助函数：

1. **文件浏览**: `OnBrowseButtonClick()`使用`ShowFolderBrowserDialog()`
2. **磁盘空间**: `UpdateDiskSpaceInfo()`和`UpdateInstallButtonState()`使用磁盘空间查询函数
3. **应用启动**: `OnFinishButtonClick()`使用`LaunchApplication()`和`OpenWebPage()`
4. **错误对话框**: `OnCancelButtonClick()`和`OnCancelProgressButtonClick()`使用`ShowConfirmDialog()`

### WelcomePageController集成

`welcome_page_controller.cpp`中的磁盘空间查询功能已经实现，可以选择性地迁移到使用`GUIHelpers`以保持一致性。

## 构建配置

`CMakeLists.txt`已更新以包含新的源文件：

```cmake
set(GUI_SOURCES
    src/gui/gui_manager.cpp
    src/gui/page_controller.cpp
    src/gui/installation_worker.cpp
    src/gui/welcome_page_controller.cpp
    src/gui/progress_page_controller.cpp
    src/gui/completion_page_controller.cpp
    src/gui/license_dialog.cpp
    src/gui/gui_helpers.cpp  # 新增
)
```

## 测试

### 单元测试

`tests/test_gui_helpers.cpp`提供了以下测试：

1. **testFormatBytes()** - 测试字节格式化
2. **testValidatePath()** - 测试路径验证
3. **testExtractRootPath()** - 测试根路径提取
4. **testGetAvailableDiskSpace()** - 测试磁盘空间查询
5. **testCheckDiskSpace()** - 测试空间检查
6. **testCOMInitialization()** - 测试COM初始化

### 运行测试

```bash
# 构建测试（需要启用GUI支持）
cmake -DBUILD_GUI=ON ..
cmake --build .

# 运行测试
./test_gui_helpers
```

## 设计决策

### 静态类设计

`GUIHelpers`被设计为静态类（所有方法都是静态的），原因：
1. 不需要维护状态（除了COM初始化标志）
2. 便于在任何地方调用，无需创建实例
3. 符合工具类的常见模式

### COM生命周期管理

COM初始化采用引用计数方式：
- 第一次调用`InitializeCOM()`时初始化
- 后续调用返回成功但不重复初始化
- `UninitializeCOM()`清理COM
- 文件浏览对话框自动管理临时COM初始化

### 错误处理

所有函数都返回明确的成功/失败状态：
- 布尔返回值表示操作是否成功
- 失败时不抛出异常，由调用者决定如何处理
- 提供详细的错误对话框函数供调用者使用

## 未来改进

1. **日志记录**: 添加详细的日志记录以便调试
2. **本地化**: 支持多语言错误消息
3. **自定义对话框**: 使用DuiLib创建自定义对话框以保持UI一致性
4. **异步操作**: 为耗时操作（如磁盘空间查询）提供异步版本
5. **更多验证**: 增强路径验证，检查写入权限等

## 总结

任务8"实现辅助功能"已完全实现，提供了：
- ✅ 文件浏览对话框（任务8.1）
- ✅ 磁盘空间查询（任务8.2）
- ✅ 应用程序启动和网页打开（任务8.3）
- ✅ 错误处理和对话框（任务8.4）

所有功能都已集成到`GUIManager`和其他GUI组件中，并提供了单元测试验证正确性。
