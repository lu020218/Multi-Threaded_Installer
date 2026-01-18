# DuiLib Static Linking Implementation

## 概述

成功实现了DuiLib静态链接，消除了对DuiLib.dll的运行时依赖。现在安装程序只需要liblzma.dll和resources目录即可运行。

## 实现细节

### 1. CMakeLists.txt 修改

#### 添加静态库选项
```cmake
# Use static library for single-file installer
set(USE_DUILIB_STATIC ON CACHE BOOL "Use DuiLib static library")

if(USE_DUILIB_STATIC)
    # Use static library
    set(DUILIB_LIB ${THIRD_PARTY_DIR}/DuiLib_Ultimate/lib_static/DuiLib.lib)
    if(EXISTS ${DUILIB_LIB})
        message(STATUS "Using DuiLib static library: ${DUILIB_LIB}")
        add_definitions(-DUILIB_STATIC)
    else()
        message(FATAL_ERROR "DuiLib static library not found at ${DUILIB_LIB}")
    endif()
else()
    # Use dynamic library (old behavior)
    # ...
endif()
```

#### 强制使用静态运行时
由于DuiLib静态库是用`/MT`（静态运行时）编译的，我们的项目也必须使用静态运行时：

```cmake
# When using DuiLib static library, we must use static runtime
option(STATIC_LINK_RUNTIME "Statically link C/C++ runtime libraries" ON)

if(STATIC_LINK_RUNTIME)
    # Use static runtime library
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    message(STATUS "Using static C/C++ runtime library")
endif()
```

#### 修改DLL复制逻辑
```cmake
# Copy DuiLib DLL only if using dynamic library
if(BUILD_GUI AND NOT USE_DUILIB_STATIC)
    # Copy DuiLib.dll
else()
    message(STATUS "Using DuiLib static library - no DLL copy needed")
endif()
```

### 2. Packager 修改

修改了`src/packager/installer_generator.cpp`中的`copyRuntimeDependencies()`函数：

```cpp
// 复制 DuiLib.dll（仅在使用动态库时）
// 注意：如果使用静态库编译，则不需要复制DuiLib.dll
std::filesystem::path duilib = templateDir / "DuiLib.dll";
if (std::filesystem::exists(duilib)) {
    // 复制DLL
} else {
    std::cout << "  DuiLib.dll not found - assuming static linking" << std::endl;
}
```

## 构建步骤

### 1. 配置项目（使用静态库）
```cmd
cmake -B build -DUSE_DUILIB_STATIC=ON -DSTATIC_LINK_RUNTIME=ON
```

### 2. 构建安装程序
```cmd
cmake --build build --config Release --target installer
```

### 3. 构建打包器
```cmd
cmake --build build --config Release --target packager
```

### 4. 生成安装包
```cmd
build\Release\packager.exe test_input build\Release\output\MyApp_Setup.exe
```

## 验证结果

### 构建输出
```
Using DuiLib static library: .../lib_static/DuiLib.lib
Using static C/C++ runtime library
Using DuiLib static library - no DLL copy needed
```

### 打包器输出
```
Copying runtime dependencies from: "build/Release"
  DuiLib.dll not found - assuming static linking
  Copied: liblzma.dll
  Copied: resources/ directory
```

### 输出目录内容
```
build\Release\output\
├── MyApp_Setup.exe  (1.38 MB - 包含静态链接的DuiLib)
├── liblzma.dll      (185 KB)
└── resources\       (XML和图片资源)
```

### 安装程序运行
```
No embedded resources found, will use external resources
Instance path: ...\build\Release\output\
Resource path: ...\build\Release\output\resources
Path exists: YES
```

✅ **安装程序成功运行，无需DuiLib.dll**

## 优势

1. **减少依赖**: 从3个文件（exe + 2个DLL）减少到2个文件（exe + 1个DLL）
2. **更大的可执行文件**: installer.exe从~470KB增加到~1.38MB（包含DuiLib代码）
3. **更简单的分发**: 少一个DLL文件需要管理
4. **更好的兼容性**: 静态链接避免了DLL版本冲突

## 下一步

要实现完全的单文件安装程序，还需要：

1. **静态链接liblzma**: 或者将liblzma.dll嵌入到可执行文件中
2. **嵌入resources**: 将XML和图片资源嵌入到可执行文件中

参见：
- `docs/SINGLE_FILE_IMPLEMENTATION.md` - 完整的单文件实现指南
- `scripts/embed_resources.ps1` - 资源嵌入脚本

## 技术说明

### 运行时库匹配
- DuiLib静态库使用 `/MT` (MultiThreaded)
- 项目必须使用相同的运行时库设置
- 不匹配会导致链接错误：`LNK2038: 检测到"RuntimeLibrary"的不匹配项`

### 静态库位置
- 动态库: `third_party/DuiLib_Ultimate/lib/DuiLib.lib` (导入库)
- 静态库: `third_party/DuiLib_Ultimate/lib_static/DuiLib.lib` (完整代码)

### 编译器定义
- 使用静态库时添加: `-DUILIB_STATIC`
- 这告诉DuiLib头文件不要使用`__declspec(dllimport)`

## 相关文档

- `docs/PACKAGER_RESOURCE_PATH_FIX.md` - 资源路径修复
- `docs/DUILIB_DLL_FIX.md` - 原始DLL依赖问题
- `docs/BUILD_AND_DEPLOYMENT.md` - 构建和部署指南
