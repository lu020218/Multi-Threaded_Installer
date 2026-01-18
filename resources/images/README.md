# 图片资源说明

本目录包含GUI界面所需的所有图片资源。以下是需要准备的图片列表：

## 应用程序Logo
- **logo.png** (64x64 PNG)
  - 应用程序的主图标
  - 显示在所有页面的顶部
  - 建议使用透明背景

## 按钮状态图片
- **button_normal.png** - 按钮正常状态
- **button_hover.png** - 按钮悬停状态
- **button_pushed.png** - 按钮按下状态
- **button_primary_normal.png** - 主按钮正常状态
- **button_primary_hover.png** - 主按钮悬停状态
- **button_primary_pushed.png** - 主按钮按下状态
- **button_secondary_normal.png** - 次按钮正常状态
- **button_secondary_hover.png** - 次按钮悬停状态
- **button_secondary_pushed.png** - 次按钮按下状态

## 进度条图片
- **progress_fore.png** - 进度条前景（已完成部分）
- **progress_bk.png** - 进度条背景

## 标题栏按钮图标
- **min.png** - 最小化按钮图标 (28x22)
- **close.png** - 关闭按钮图标 (28x22)

## 图片规格要求
- 所有图片使用PNG格式
- 支持透明通道（Alpha通道）
- 按钮图片建议尺寸：根据实际按钮大小设计
- 图标使用高分辨率以支持高DPI显示

## 临时占位符
在开发阶段，可以使用纯色矩形作为占位符：
- 按钮：蓝色矩形 (#4A90E2)
- 进度条前景：绿色矩形 (#4CAF50)
- 进度条背景：浅灰色矩形 (#E0E0E0)
- 图标：简单的几何形状

## 替换说明
1. 将设计师提供的PNG文件放入此目录
2. 确保文件名与上述列表完全匹配
3. 无需修改XML布局文件，DuiLib会自动加载
