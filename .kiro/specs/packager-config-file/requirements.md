# Requirements Document

## Introduction

本文档定义了打包器配置文件支持功能的需求。该功能允许用户通过配置文件来指定打包和安装的各种选项，包括默认安装目录、压缩算法选择、以及特殊文件的安装位置等。这将简化打包器的命令行接口，并提供更灵活的配置管理方式。

## Glossary

- **Packager**: 打包器，负责将文件夹打包成自解压安装程序的系统组件
- **Configuration_File**: 配置文件，包含打包和安装选项的文件
- **Compression_Algorithm**: 压缩算法，可选择ZSTD或LZMA
- **Installation_Directory**: 安装目录，文件将被解压到的目标位置
- **Special_Directory**: 特殊目录，如Windows的Roaming、Local等系统目录
- **Input_Directory**: 输入目录，需要被打包的源文件夹
- **Output_File**: 输出文件，生成的自解压安装程序文件

## Requirements

### Requirement 1: Configuration File Parsing

**User Story:** 作为打包器用户，我希望能够使用配置文件来指定打包选项，这样我就可以方便地管理和复用打包配置。

#### Acceptance Criteria

1. WHEN 打包器在输入目录中找到配置文件 THEN THE Packager SHALL 解析该配置文件并读取所有配置选项
2. WHEN 配置文件格式无效 THEN THE Packager SHALL 返回清晰的错误信息并终止执行
3. WHEN 配置文件不存在 THEN THE Packager SHALL 使用默认配置值继续执行
4. THE Configuration_File SHALL 使用JSON格式
5. WHEN 配置文件包含未知的配置项 THEN THE Packager SHALL 记录警告信息但继续执行
6. THE Configuration_File SHALL 包含应用程序名称配置项

### Requirement 2: Default Installation Directory Configuration

**User Story:** 作为打包器用户，我希望能够在配置文件中指定建议的默认安装目录，这样安装程序就可以向用户建议一个合适的安装位置。

#### Acceptance Criteria

1. THE Configuration_File SHALL 支持指定建议的默认安装目录路径
2. WHEN 默认安装目录使用环境变量（如%ProgramFiles%）THEN THE Packager SHALL 正确处理并在元数据中保存
3. WHEN 配置文件未指定默认安装目录 THEN THE Packager SHALL 使用应用程序名称生成默认值（如%ProgramFiles%\\[ApplicationName]）
4. THE Packager SHALL 验证指定的安装目录路径格式是否有效
5. THE Installer SHALL 将默认安装目录作为建议值显示给用户，用户可以修改

### Requirement 3: Compression Algorithm Selection

**User Story:** 作为打包器用户，我希望能够在配置文件中选择压缩算法（ZSTD或LZMA），这样我就可以根据需求平衡压缩率和速度。

#### Acceptance Criteria

1. THE Configuration_File SHALL 支持指定压缩算法为"zstd"或"lzma"
2. WHEN 指定的压缩算法无效 THEN THE Packager SHALL 返回错误信息并终止执行
3. WHEN 配置文件未指定压缩算法 THEN THE Packager SHALL 使用默认算法（ZSTD）
4. THE Packager SHALL 根据配置的压缩算法调用相应的压缩模块

### Requirement 4: Folder-Level Target Directory Configuration

**User Story:** 作为打包器用户，我希望能够为输入目录中的每个文件夹指定不同的安装目标目录，这样我就可以将不同的文件夹安装到不同的位置（如程序文件夹安装到用户选择的安装目录，插件安装到AppData\Roaming）。

#### Acceptance Criteria

1. THE Configuration_File SHALL 支持为每个文件夹指定目标安装目录
2. WHEN 配置文件指定文件夹目标目录 THEN THE Packager SHALL 在元数据中记录每个文件夹的目标目录
3. THE Packager SHALL 支持以下目标目录类型：用户选择的安装目录（"installDirectory"）、环境变量路径、绝对路径
4. WHEN 文件夹未在配置中指定目标目录 THEN THE Packager SHALL 使用用户选择的安装目录
5. THE Installer SHALL 根据元数据中的目标目录信息将每个文件夹安装到正确的位置
6. WHEN 目标目录为"installDirectory" THEN THE Installer SHALL 将文件夹安装到用户选择的安装目录

### Requirement 5: Special Directory File Mapping

**User Story:** 作为打包器用户，我希望能够使用环境变量（如%AppData%、%ProgramData%）来指定安装目录，这样安装程序就可以在不同用户的系统上正确地安装文件到用户特定的目录。

#### Acceptance Criteria

1. THE Configuration_File SHALL 支持使用Windows环境变量来指定目标目录
2. THE Packager SHALL 支持以下环境变量：%ProgramFiles%、%AppData%、%LocalAppData%、%ProgramData%、%USERPROFILE%
3. WHEN 目标目录包含环境变量 THEN THE Packager SHALL 在元数据中保存环境变量引用（不展开）
4. THE Installer SHALL 在安装时根据目标系统的环境变量展开路径
5. WHEN 环境变量在目标系统上不存在 THEN THE Installer SHALL 返回错误信息

### Requirement 6: Fine-Grained File Mapping (Optional)

**User Story:** 作为打包器用户，我希望能够为特定的文件（而不是整个文件夹）指定安装位置，这样我就可以实现更细粒度的文件组织。

#### Acceptance Criteria

1. THE Configuration_File SHALL 支持定义文件路径到目标目录的映射规则（可选功能）
2. THE Packager SHALL 支持使用通配符模式（如*.config）来匹配多个文件
3. WHEN 文件同时匹配文件夹目标和文件映射规则 THEN THE Packager SHALL 优先使用文件映射规则
4. WHEN 文件同时匹配多个文件映射规则 THEN THE Packager SHALL 使用第一个匹配的规则
5. THE Packager SHALL 在元数据中记录文件映射关系

### Requirement 7: Simplified Command Line Interface

**User Story:** 作为打包器用户，我希望命令行接口简单明了，只需要指定输入目录和输出文件，这样我就可以快速执行打包操作。

#### Acceptance Criteria

1. THE Packager SHALL 接受恰好两个命令行参数：input_directory和output_file
2. WHEN 命令行参数数量不正确 THEN THE Packager SHALL 显示使用说明并返回错误代码
3. WHEN input_directory不存在 THEN THE Packager SHALL 返回错误信息并终止执行
4. WHEN output_file路径无效 THEN THE Packager SHALL 返回错误信息并终止执行
5. THE Packager SHALL 在input_directory中查找配置文件（如config.json或packager.ini）

### Requirement 8: Configuration File Location and Naming

**User Story:** 作为打包器用户，我希望配置文件有明确的命名约定和位置，这样我就可以轻松地创建和管理配置文件。

#### Acceptance Criteria

1. THE Packager SHALL 在输入目录的根目录中查找配置文件
2. THE Packager SHALL 按以下优先级查找配置文件：packager.json、.packager.json
3. WHEN 找到多个配置文件 THEN THE Packager SHALL 使用优先级最高的文件并记录警告
4. THE Packager SHALL 支持通过环境变量指定配置文件的完整路径

### Requirement 9: Configuration Validation

**User Story:** 作为打包器用户，我希望打包器能够验证配置文件的正确性，这样我就可以在打包前发现配置错误。

#### Acceptance Criteria

1. THE Packager SHALL 验证所有必需的配置项是否存在
2. THE Packager SHALL 验证配置值的类型和格式是否正确
3. WHEN 配置验证失败 THEN THE Packager SHALL 返回详细的错误信息，包括错误的配置项和原因
4. THE Packager SHALL 验证文件夹名称是否在输入目录中存在
5. THE Packager SHALL 验证目标目录路径格式是否有效

### Requirement 10: Metadata Integration

**User Story:** 作为安装程序，我需要从元数据中读取配置信息，这样我就可以根据打包时的配置正确地安装文件。

#### Acceptance Criteria

1. THE Packager SHALL 将配置文件中的安装目录信息写入元数据
2. THE Packager SHALL 将文件夹目标目录配置写入元数据
3. THE Packager SHALL 将压缩算法信息写入元数据
4. THE Metadata SHALL 保持向后兼容，未配置的选项使用默认值
5. THE Installer SHALL 能够从元数据中读取并应用这些配置

### Requirement 11: Configuration File Documentation

**User Story:** 作为打包器用户，我希望有清晰的配置文件文档和示例，这样我就可以快速学习如何编写配置文件。

#### Acceptance Criteria

1. THE Packager SHALL 提供配置文件的完整文档，说明所有支持的配置项
2. THE Documentation SHALL 包含至少一个完整的配置文件示例
3. THE Documentation SHALL 说明每个配置项的用途、类型、默认值和有效值范围
4. THE Documentation SHALL 包含常见使用场景的配置示例

### Requirement 12: Error Handling and Logging

**User Story:** 作为打包器用户，我希望在配置文件处理过程中能够获得清晰的错误信息和日志，这样我就可以快速定位和解决问题。

#### Acceptance Criteria

1. WHEN 配置文件解析失败 THEN THE Packager SHALL 记录详细的错误信息，包括文件位置和错误原因
2. THE Packager SHALL 记录所使用的配置文件路径
3. THE Packager SHALL 记录所有应用的配置选项及其值
4. WHEN 使用默认配置值 THEN THE Packager SHALL 记录信息级别的日志
5. THE Packager SHALL 在配置验证失败时提供修复建议
