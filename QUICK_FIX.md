# 快速修复：打包器生成的安装程序资源加载问题

## ✅ 问题已解决

打包器生成的安装程序之前会报错"加载资源文件失败：main.xml"。此问题已**修复**。

## ✅ DuiLib静态链接已实现

现在使用DuiLib静态库，**不再需要DuiLib.dll**！安装程序只需要：
- MyApp_Setup.exe (包含静态链接的DuiLib)
- liblzma.dll
- resources/ 目录

## 当前状态

✅ **DuiLib静态链接** - 无需DuiLib.dll  
✅ **打包器正确复制依赖文件** (liblzma.dll, resources/)  
✅ **安装程序正确回退到外部资源**  
✅ **打包器生成的安装程序成功启动 GUI**

## 使用方法

1. **配置项目（使用静态库）**:
   ```cmd
   cmake -B build -DUSE_DUILIB_STATIC=ON -DSTATIC_LINK_RUNTIME=ON
   ```

2. **构建安装程序**:
   ```cmd
   cmake --build build --config Release --target installer
   ```

3. **构建打包器**:
   ```cmd
   cmake --build build --config Release --target packager
   ```

4. **运行打包器**:
   ```cmd
   build\Release\packager.exe <输入目录> <输出文件>
   ```
   
   示例:
   ```cmd
   build\Release\packager.exe test_input build\Release\output\MyApp_Setup.exe
   ```

5. **打包器会自动**:
   - 复制 liblzma.dll 到输出目录
   - 复制 resources/ 目录到输出目录
   - **不复制 DuiLib.dll**（已静态链接）

6. **运行生成的安装程序**:
   ```cmd
   build\Release\output\MyApp_Setup.exe
   ```

## 配置文件格式

在输入目录中创建 `packager.json`:

```json
{
  "Version": "1.0.0",
  "AppName": "YourApp",
  "InstallDir": "%ProgramFiles%\\YourApp",
  "Folder": {
    "InstallDir": "your_folder_name"
  }
}
```

## 验证

打包器输出会显示:
```
DuiLib.dll not found - assuming static linking
Copied: liblzma.dll
Copied: resources/ directory
```

安装程序会显示调试输出:
```
No embedded resources found, will use external resources
Instance path: <exe所在目录>\
Resource path: <exe所在目录>\resources
Path exists: YES
```

## 下一步（可选）

要实现完全的单文件安装程序（无需任何外部文件），还需要：
1. 静态链接或嵌入 liblzma.dll
2. 嵌入 resources/ 目录

参见：
- `docs/SINGLE_FILE_IMPLEMENTATION.md` - 实现指南
- `docs/SINGLE_FILE_QUICK_START.md` - 快速入门
- `scripts/embed_resources.ps1` - 资源嵌入脚本

## 相关文档

- `docs/DUILIB_STATIC_LINKING.md` - DuiLib静态链接实现详情
- `docs/PACKAGER_RESOURCE_PATH_FIX.md` - 资源路径修复详情
- `docs/PACKAGING_WITH_DEPENDENCIES.md` - 打包指南
- `docs/MANUAL_DEPENDENCY_COPY.md` - 手动复制说明

