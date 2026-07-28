#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// ---------------------------------------------------------------------------
// Refactored configuration model (值/逻辑归位).
//
// YAML 只声明三块：app（本次构建身份）、package（压缩参数）、hooks（pre/post bat）。
// 其余稳定机制、默认值、跨版本兼容迁移全部写死/实现在引擎 C++ 中，不在此暴露。
// 详见 docs/USER_GUIDE.md。
// ---------------------------------------------------------------------------

// 压缩算法。none 表示原样打包（不压缩）。
enum class CompressionAlgorithm {
    LZMA2_XZ,
    ZSTD,
    NONE
};

// Windows 注册表值类型——这是注册表原语，供引擎写注册表时使用，不来自 YAML。
enum class RegistryValueType {
    STRING,
    DWORD,
    EXPAND_STRING
};

// 打包期扫描到的一个待打包目录。
struct FolderInfo {
    std::string id;
    std::string sourcePath;
    std::string targetPath;
    std::vector<std::string> files;
    size_t totalSize;

    FolderInfo() : totalSize(0) {}

    FolderInfo(const std::string& source, const std::string& target)
        : sourcePath(source), targetPath(target), totalSize(0) {}
};

// ── app —— 产品身份（yaml 声明 / 包内嵌 / 运行期 三层共用同一结构体）──────────
// 加字段只需：此处 1 行 + configuration_loader 解析 1 行 + codec 序列化/反序列化各 1 行。
struct PackageIdentity {
    std::string productName;   // 用户可见产品名；数据目录/注册表键/快捷方式名统一用它
    std::string appName;       // 主 exe 程序名（不含 .exe）：主程序定位/杀进程/立即运行
    std::string appId;         // 产品唯一 id（如 com.comp.myapp），随包与 install.manifest.json 传递
    std::string publisher;     // 发布者/公司名（同时用作版本资源 CompanyName）
    std::string version;       // 版本号，每次发版必改（可含 -beta 等预发布后缀）
    std::string defaultDir = "%ProgramFiles%";  // GUI 默认安装目录，支持环境变量
    std::string copyright;     // 可空；缺省时引擎用 publisher + 构建年份生成
};

// ── package —— 打包基础参数 ──────────────────────────────────────────────
struct PackageCompressionConfig {
    CompressionAlgorithm algorithm;
    int level;          // 压缩级别；-1 表示用算法默认
    int blockSizeMiB;   // XZ 多线程分块大小(MiB)；0=自动(对齐解码并行度,块更大、压缩比更高)

    PackageCompressionConfig()
        : algorithm(CompressionAlgorithm::LZMA2_XZ),
          level(-1),
          blockSizeMiB(0) {}
};

// 逐文件夹落点声明（可选）。source = --input 顶层子目录名；target = 安装目标，
// 支持 %InstallDir% 与环境变量（%AppData% / %LocalAppData% / %ProgramData% 等）。
// 未声明的文件夹默认落到 %InstallDir%\<source>。
struct LayoutFolderTarget {
    std::string source;
    std::string target;
};

struct PackageConfig {
    PackageCompressionConfig compression;
    std::vector<LayoutFolderTarget> layout;
};

// ── hooks —— 安装前/后脚本（yaml 声明 / 包内嵌 / 运行期 三层共用同一结构体）────
enum class HookOnFailure {
    ABORT,     // 中止安装并回滚
    CONTINUE   // 记录日志后继续
};

// 与主钩子脚本同目录的「兄弟文件」（脚本/数据等），随包内嵌，运行期与主脚本释放到
// 同一临时目录，使主脚本可 `call common.bat`、`.\sub\helper.ps1` 或读取同目录数据文件。
struct HookAuxFile {
    std::string relativePath;      // 相对主脚本所在目录（generic '/'），如 "common.bat"、"sub/helper.ps1"
    std::vector<uint8_t> content;  // 打包期读入内嵌的文件字节
};

// 一个 pre/post 钩子脚本。同一对象贯穿三个阶段：
//   yaml 解析 → 填 sourcePath/args/onFailure/timeoutSec/keep/keepDir；
//   打包内嵌 → 就地读入 content/auxFiles、置 present/scriptName；
//   运行期   → hook_runner 释放 content 到临时目录执行（sourcePath 仅留作日志）。
struct HookScript {
    bool present = false;
    std::string sourcePath;          // yaml 声明的脚本路径（相对 --config 目录；打包期用，运行期仅日志）
    std::string scriptName;          // 脚本文件名，如 pre_install.bat（日志/释放命名用）
    std::vector<uint8_t> content;    // 打包期读入内嵌的脚本字节
    std::string args;                // 本次构建特有的额外参数
    HookOnFailure onFailure = HookOnFailure::ABORT;  // 失败处理
    uint32_t timeoutSec = 300;       // 超时上限（秒），到点 kill 按失败处理
    std::vector<HookAuxFile> auxFiles;  // 主脚本同目录的兄弟文件（递归内嵌），随主脚本一同释放
    bool keep = false;               // 执行后是否把脚本+兄弟文件保留到 keepDir（默认用完即删）
    std::string keepDir;             // 保留目标目录（keep=true 必填，支持 %INSTALL_DIR%/%VERSION%/系统环境变量）
};

// 安装前/后两个固定钩子点；每点可挂多个脚本，按声明顺序依次执行。三层共用。
struct PackageHooks {
    std::vector<HookScript> preInstall;
    std::vector<HookScript> postInstall;
};

// 根配置：身份 + 打包参数 + 钩子（+打包期独有的图标路径）。
struct PackagerConfiguration {
    PackageIdentity app;       // yaml app 节（三层共用的身份结构体）
    std::string appIcon;       // yaml app.icon：安装器 exe 图标，打包期独有，不进包元数据
    PackageConfig package;
    PackageHooks hooks;
};

} // namespace MultiThreadedInstaller
