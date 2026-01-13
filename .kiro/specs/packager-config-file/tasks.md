# Implementation Plan: Packager Configuration File Support

## Overview

本实现计划将为打包器添加配置文件支持功能。实现将分为几个主要阶段：配置文件解析、配置验证、元数据扩展、打包器集成和安装程序扩展。每个阶段都包含相应的测试任务。

## Tasks

- [x] 1. 集成nlohmann/json库
  - 下载nlohmann/json单头文件库
  - 将json.hpp放入third_party/nlohmann/目录
  - 更新CMakeLists.txt以包含新的头文件路径
  - _Requirements: 1.4_

- [-] 2. 实现配置数据结构
  - [x] 2.1 定义SpecialDirectoryType枚举
    - 在types.h中添加SpecialDirectoryType枚举
    - 包含INSTALL_DIRECTORY、PROGRAM_FILES、APPDATA_ROAMING、APPDATA_LOCAL、PROGRAM_DATA
    - _Requirements: 4.3, 5.2_
  
  - [x] 2.2 定义FolderTargetConfig结构
    - 在types.h中添加FolderTargetConfig结构
    - 包含folderName、targetDirectory、dirType字段
    - _Requirements: 4.1_
  
  - [x] 2.3 定义PackagerConfiguration结构
    - 在types.h中添加PackagerConfiguration结构
    - 包含applicationName、defaultInstallDir、compressionAlgorithm、folderTargets字段
    - 提供默认构造函数
    - _Requirements: 1.6, 2.1, 3.1, 4.1_
  
  - [x] 2.4 编写配置数据结构的单元测试
    - 测试默认值
    - 测试结构初始化
    - _Requirements: 1.6, 2.1, 3.1, 4.1_

- [x] 3. 实现ConfigurationLoader类
  - [x] 3.1 创建ConfigurationLoader头文件和实现文件
    - 创建include/packager/configuration_loader.h
    - 创建src/packager/configuration_loader.cpp
    - 定义类接口
    - _Requirements: 1.1, 8.1_
  
  - [x] 3.2 实现findConfigFile方法
    - 按优先级查找配置文件（packager.json -> .packager.json）
    - 支持环境变量PACKAGER_CONFIG
    - _Requirements: 8.1, 8.2, 8.4_
  
  - [x] 3.3 实现parseJsonConfig方法
    - 使用nlohmann/json解析JSON文件
    - 解析所有配置字段
    - 处理JSON解析错误
    - _Requirements: 1.1, 1.2_
  
  - [x] 3.4 实现parseDirectoryType方法
    - 将字符串转换为SpecialDirectoryType枚举
    - 支持"installDirectory"、"%ProgramFiles%"、"%AppData%\\Roaming"等
    - _Requirements: 4.3, 5.1_
  
  - [x] 3.5 编写ConfigurationLoader的单元测试
    - 测试成功加载有效配置文件
    - 测试配置文件不存在的情况
    - 测试无效JSON格式
    - 测试配置文件优先级
    - _Requirements: 1.1, 1.2, 1.3, 8.2, 8.3_
  
  - [x] 3.6 编写ConfigurationLoader的属性测试
    - **Property 1: Configuration File Discovery and Parsing**
    - **Validates: Requirements 1.1, 7.5, 8.1**
    - 生成随机的有效配置文件，验证能够被正确发现和解析

- [x] 4. 实现ConfigurationValidator类
  - [x] 4.1 创建ConfigurationValidator头文件和实现文件
    - 创建include/packager/configuration_validator.h
    - 创建src/packager/configuration_validator.cpp
    - 定义ValidationResult结构
    - _Requirements: 9.1, 9.2, 9.3_
  
  - [x] 4.2 实现validateApplicationName方法
    - 验证应用程序名称不为空
    - 验证应用程序名称不包含非法字符
    - _Requirements: 1.6, 9.1_
  
  - [x] 4.3 实现validateFolderExists方法
    - 验证配置的文件夹在输入目录中存在
    - _Requirements: 9.4_
  
  - [x] 4.4 实现validateTargetDirectory方法
    - 验证目标目录配置的有效性
    - 验证环境变量格式
    - _Requirements: 2.4, 9.5_
  
  - [x] 4.5 实现validate方法
    - 调用所有验证方法
    - 收集错误和警告信息
    - _Requirements: 9.1, 9.2, 9.3_
  
  - [x] 4.6 编写ConfigurationValidator的单元测试
    - 测试缺少应用程序名的配置
    - 测试文件夹不存在的情况
    - 测试无效的目标目录配置
    - 测试有效配置通过验证
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5_
  
  - [x] 4.7 编写ConfigurationValidator的属性测试
    - **Property 3: Application Name Requirement**
    - **Validates: Requirements 1.6, 9.1**
    - 生成缺少applicationName的配置，验证返回错误
    - **Property 6: Folder Existence Validation**
    - **Validates: Requirements 9.4**
    - 生成随机的文件夹配置，验证文件夹存在性检查
    - **Property 12: Configuration Validation Completeness**
    - **Validates: Requirements 9.1, 9.2, 9.5**
    - 生成各种无效配置，验证验证逻辑的完整性

- [x] 5. Checkpoint - 配置加载和验证完成
  - 确保所有测试通过，询问用户是否有问题

- [x] 6. 实现ConfigurationManager类
  - [x] 6.1 创建ConfigurationManager头文件和实现文件
    - 创建include/packager/configuration_manager.h
    - 创建src/packager/configuration_manager.cpp
    - 定义类接口
    - _Requirements: 1.1_
  
  - [x] 6.2 实现initialize方法
    - 调用ConfigurationLoader加载配置
    - 调用ConfigurationValidator验证配置
    - 处理配置不存在的情况（使用默认配置）
    - _Requirements: 1.1, 1.3_
  
  - [x] 6.3 实现applyFolderTargets方法
    - 将配置的文件夹目标应用到FolderInfo列表
    - 设置每个文件夹的targetPath
    - _Requirements: 4.2_
  
  - [x] 6.4 编写ConfigurationManager的单元测试
    - 测试初始化成功
    - 测试应用文件夹目标配置
    - 测试配置不存在时使用默认值
    - _Requirements: 1.1, 1.3, 4.2_

- [x] 7. 扩展元数据结构
  - [x] 7.1 定义ExtendedFolderMapping结构
    - 在types.h中添加ExtendedFolderMapping结构
    - 继承自FolderMapping
    - 添加targetDirType和customTargetPath字段
    - _Requirements: 4.2, 10.2_
  
  - [x] 7.2 定义ExtendedInstallationMetadata结构
    - 在types.h中添加ExtendedInstallationMetadata结构
    - 继承自InstallationMetadata
    - 添加applicationName、defaultInstallDir和extendedMappings字段
    - _Requirements: 10.1, 10.2_
  
  - [x] 7.3 更新MetadataGenerator以支持扩展元数据
    - 修改generateMetadata方法以接受PackagerConfiguration
    - 将配置信息写入扩展元数据
    - _Requirements: 10.1, 10.2, 10.3_
  
  - [x] 7.4 更新serializeMetadata以支持扩展元数据
    - 序列化新增的字段
    - 保持向后兼容性
    - _Requirements: 10.4_
  
  - [x] 7.5 编写元数据扩展的单元测试
    - 测试扩展元数据的生成
    - 测试扩展元数据的序列化
    - 测试向后兼容性
    - _Requirements: 10.1, 10.2, 10.3, 10.4_
  
  - [x] 7.6 编写元数据扩展的属性测试
    - **Property 8: Folder Target Metadata Persistence**
    - **Validates: Requirements 4.2, 10.2**
    - 生成随机的文件夹目标配置，验证正确写入元数据
    - **Property 13: Metadata Configuration Round-Trip**
    - **Validates: Requirements 10.1, 10.2, 10.3, 10.5**
    - 生成随机配置，验证写入元数据后能正确读取
    - **Property 14: Backward Compatibility**
    - **Validates: Requirements 10.4**
    - 验证旧版本元数据能被正确读取

- [x] 8. 集成配置管理器到打包器主程序
  - [x] 8.1 修改main.cpp以简化命令行参数
    - 只接受两个参数：input_directory和output_file
    - 移除其他命令行选项
    - _Requirements: 7.1, 7.2_
  
  - [x] 8.2 在main.cpp中初始化ConfigurationManager
    - 创建ConfigurationManager实例
    - 调用initialize方法
    - 处理初始化失败的情况
    - _Requirements: 1.1, 1.2_
  
  - [x] 8.3 使用配置管理器的配置
    - 从配置管理器获取压缩算法
    - 应用文件夹目标配置
    - 传递配置到MetadataGenerator
    - _Requirements: 3.4, 4.2, 10.1_
  
  - [x] 8.4 添加配置日志记录
    - 记录使用的配置文件路径
    - 记录所有应用的配置选项
    - 记录使用默认值的情况
    - _Requirements: 12.2, 12.3, 12.4_
  
  - [x] 8.5 编写打包器集成的单元测试
    - 测试命令行参数验证
    - 测试配置加载和应用
    - 测试日志记录
    - _Requirements: 7.1, 7.2, 12.2, 12.3, 12.4_
  
  - [x] 8.6 编写打包器集成的属性测试
    - **Property 11: Command Line Argument Validation**
    - **Validates: Requirements 7.1, 7.2**
    - 生成不同数量的命令行参数，验证只接受两个参数

- [-] 9. Checkpoint - 打包器集成完成
  - 确保所有测试通过，询问用户是否有问题

- [ ] 10. 实现InstallerPathResolver类
  - [ ] 10.1 创建InstallerPathResolver头文件和实现文件
    - 创建include/installer/path_resolver.h
    - 创建src/installer/path_resolver.cpp
    - 定义类接口
    - _Requirements: 4.6, 5.3_
  
  - [ ] 10.2 实现expandEnvironmentVariables方法
    - 展开Windows环境变量
    - 支持%ProgramFiles%、%AppData%等
    - _Requirements: 5.3, 5.4_
  
  - [ ] 10.3 实现pathContainsAppName方法
    - 检查路径的最后一个目录是否为应用程序名
    - _Requirements: 4.6_
  
  - [ ] 10.4 实现appendAppNameIfNeeded方法
    - 如果路径不包含应用程序名，则追加
    - 如果已包含，则不追加
    - _Requirements: 4.6_
  
  - [ ] 10.5 实现resolveFinalPath方法
    - 根据目标目录类型解析最终路径
    - 调用expandEnvironmentVariables和appendAppNameIfNeeded
    - _Requirements: 4.6, 5.3_
  
  - [ ] 10.6 编写InstallerPathResolver的单元测试
    - 测试环境变量展开
    - 测试应用程序名检测
    - 测试路径补齐逻辑
    - 测试不重复添加应用程序名
    - _Requirements: 4.6, 5.3, 5.4_
  
  - [ ] 10.7 编写InstallerPathResolver的属性测试
    - **Property 9: Install Directory Path Resolution**
    - **Validates: Requirements 4.6, 2.5**
    - 生成随机路径和应用程序名，验证路径解析逻辑
    - **Property 10: Environment Variable Path Resolution**
    - **Validates: Requirements 5.3, 5.4**
    - 生成包含环境变量的路径，验证展开和补齐逻辑

- [ ] 11. 扩展安装程序以支持配置
  - [ ] 11.1 更新MetadataParser以解析扩展元数据
    - 修改parseMetadata方法以读取扩展字段
    - 处理向后兼容性
    - _Requirements: 10.4, 10.5_
  
  - [ ] 11.2 修改安装程序主程序以使用路径解析器
    - 创建InstallerPathResolver实例
    - 对每个文件夹调用resolveFinalPath
    - 使用解析后的路径进行安装
    - _Requirements: 4.5, 4.6_
  
  - [ ] 11.3 实现用户安装目录输入
    - 显示默认安装目录建议
    - 允许用户修改安装目录
    - _Requirements: 2.5_
  
  - [ ] 11.4 编写安装程序扩展的单元测试
    - 测试扩展元数据解析
    - 测试路径解析和文件安装
    - 测试用户输入处理
    - _Requirements: 2.5, 4.5, 4.6, 10.4, 10.5_

- [ ] 12. 端到端集成测试
  - [ ] 12.1 编写完整的打包和安装流程测试
    - 创建测试输入目录和配置文件
    - 运行打包器生成安装程序
    - 运行安装程序并验证文件安装位置
    - 验证应用程序名补齐逻辑
    - _Requirements: 4.5, 4.6_
  
  - [ ] 12.2 编写多场景测试
    - 测试用户未修改安装目录的场景
    - 测试用户修改为不含应用程序名的路径
    - 测试用户修改为已含应用程序名的路径
    - _Requirements: 4.6_

- [ ] 13. 创建配置文件文档
  - [ ] 13.1 创建配置文件参考文档
    - 创建docs/configuration_reference.md
    - 说明所有配置选项
    - 包含完整示例
    - _Requirements: 11.1, 11.2, 11.3, 11.4_
  
  - [ ] 13.2 创建迁移指南
    - 创建docs/migration_guide.md
    - 说明如何从旧的命令行参数迁移
    - 说明向后兼容性
    - _Requirements: 11.1, 11.3_
  
  - [ ] 13.3 创建配置示例
    - 创建examples/configurations/目录
    - 添加基本配置示例
    - 添加高级配置示例
    - _Requirements: 11.4_

- [ ] 14. Final Checkpoint - 确保所有测试通过
  - 运行所有单元测试
  - 运行所有属性测试
  - 运行端到端测试
  - 询问用户是否有问题

## Notes

- 所有任务都是必需的，包括单元测试和属性测试
- 每个任务都引用了具体的需求，以确保可追溯性
- 属性测试任务明确标注了属性编号和验证的需求
- 检查点任务确保增量验证
