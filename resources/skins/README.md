# DuiLib XML布局文件说明

本目录包含安装程序GUI的所有XML布局文件。

## 文件列表

### 主窗口
- **main.xml** - 安装主窗口框架
  - 定义窗口大小（600x450）
  - 配置字体（微软雅黑，3种大小）
  - 定义标题栏（带最小化和关闭按钮）
  - 包含TabLayout容器用于页面切换
  - 通过Include标签引入安装与卸载页面

- **uninstall_main.xml** - 卸载主窗口框架
  - 独立卸载界面，包含卸载确认/进度/完成页面

### 页面布局
- **welcome_page.xml** - 欢迎页面
  - Logo显示
  - 应用名称和版本
  - 安装路径选择（Edit + 浏览按钮）
  - 磁盘空间信息显示
  - 许可协议复选框和超链接
  - 安装和取消按钮

- **progress_page.xml** - 进度页面
  - Logo显示
  - 应用名称和版本
  - 当前文件夹名称标签
  - 进度条控件
  - 百分比和预计时间标签
  - 取消按钮

- **completion_page.xml** - 完成页面
  - Logo显示
  - 应用名称和版本
  - 结果消息标签（成功/失败）
  - "立即运行应用程序"复选框
  - "打开介绍网页"复选框
  - 完成按钮

- **uninstall_confirm_page.xml** - 卸载确认页面
  - 卸载提示文案
  - 取消与开始卸载按钮

- **uninstall_progress_page.xml** - 卸载中页面
  - 卸载进度提示
  - 简单进度条

- **uninstall_completion_page.xml** - 卸载完成页面
  - 卸载结果提示
  - 完成按钮

### 对话框
- **license.xml** - 许可协议对话框
  - 对话框窗口（500x400）
  - 标题栏
  - 可滚动的协议文本框（RichEdit）
  - 同意和不同意按钮

## 控件命名约定

为便于C++代码中查找和操作控件，使用以下命名规则：

- 页面容器：`{page_name}_page`
- 按钮：`{action}_button`
- 标签：描述性名称（如 `app_name`, `current_folder`）
- 输入框：`{purpose}_path` 或 `{purpose}_edit`
- 复选框：`{purpose}_checkbox`
- 进度条：`progress_bar`
- 富文本：`{purpose}_text` 或 `{purpose}_link`

## 修改指南

### 修改布局
1. 直接编辑对应的XML文件
2. 无需重新编译C++代码
3. 重新运行程序即可看到效果

### 修改样式
- 颜色：修改 `textcolor`, `bkcolor` 属性
- 字体：修改 `font` 属性（引用Font id）
- 尺寸：修改 `width`, `height` 属性
- 间距：修改 `padding` 属性

### 添加控件
1. 在适当的Layout中添加控件标签
2. 设置 `name` 属性以便C++代码访问
3. 配置控件的外观属性
4. 在C++代码中通过 `FindControl()` 获取控件指针

## DuiLib控件类型

常用控件：
- `Label` - 文本标签
- `Button` - 按钮
- `Edit` - 单行文本框
- `RichEdit` - 多行文本框
- `CheckBox` - 复选框
- `Progress` - 进度条
- `Control` - 通用控件（用作占位符或容器）

布局容器：
- `VerticalLayout` - 垂直布局
- `HorizontalLayout` - 水平布局
- `TabLayout` - 标签页布局

## 颜色格式

DuiLib使用ARGB格式：`#AARRGGBB`
- AA：透明度（FF=不透明，00=完全透明）
- RR：红色分量
- GG：绿色分量
- BB：蓝色分量

示例：
- `#FFFFFFFF` - 白色
- `#FF000000` - 黑色
- `#FF4A90E2` - 蓝色
- `#FF4CAF50` - 绿色

## 图片资源引用

XML中通过文件名引用图片：
```xml
<Control bkimage="logo.png"/>
<Button normalimage="button_normal.png" hotimage="button_hover.png"/>
```

图片文件应放在 `resources/images/` 目录。

## 调试技巧

1. **验证XML语法**：使用XML编辑器检查语法错误
2. **检查控件名称**：确保name属性唯一且与C++代码一致
3. **测试布局**：调整窗口大小查看布局响应
4. **颜色对比**：确保文本颜色与背景有足够对比度

## 参考资源

- DuiLib官方文档
- 示例项目：third_party/DuiLib_Ultimate/Demos/
- 属性列表：third_party/DuiLib_Ultimate/Help/Duilib属性列表.html
