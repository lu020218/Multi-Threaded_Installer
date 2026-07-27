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

// ── app —— 本次构建的身份与取值 ──────────────────────────────────────────
struct AppConfig {
    std::string productName;   // 用户可见产品名
    std::string appName;       // 主 exe 程序名（不含 .exe），用于主程序定位/杀进程/立即运行
    std::string appId;         // 产品唯一 id（如 com.comp.myapp），随包与 install.manifest.json 传递
    std::string publisher;     // 发布者/公司名（同时用作版本资源 CompanyName）
    std::string version;       // 版本号，每次发版必改（可含 -beta 等预发布后缀）
    std::string defaultDir;    // GUI 默认安装目录，支持 %ProgramFiles% 等环境变量
    std::string icon;          // 安装器 exe 图标，路径相对 --config 目录解析
    std::string copyright;     // 可空；缺省时引擎用 publisher + 构建年份生成

    AppConfig()
        : defaultDir("%ProgramFiles%") {}
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

// ── hooks —— 安装前/后脚本 ───────────────────────────────────────────────
enum class HookOnFailure {
    ABORT,     // 中止安装并回滚
    CONTINUE   // 记录日志后继续
};

struct HookConfig {
    std::string path;        // 脚本路径，相对 --config 目录解析
    std::string args;        // 本次构建特有的额外参数，可为空
    HookOnFailure onFailure; // 失败处理
    uint32_t timeoutSec;     // 超时上限（秒）
    bool present;            // 该 hook 是否在 YAML 中配置
    bool keep;              // 执行后是否保留脚本+兄弟文件到 keepDir（默认 false=用完即删）
    std::string keepDir;    // 保留目标目录（keep=true 时必填，支持 %INSTALL_DIR%/%VERSION%/系统环境变量）

    HookConfig()
        : onFailure(HookOnFailure::ABORT),
          timeoutSec(300),
          present(false),
          keep(false) {}
};

// preInstall / postInstall 均支持配置多个脚本，按声明顺序依次执行。
struct HooksConfig {
    std::vector<HookConfig> preInstall;
    std::vector<HookConfig> postInstall;
};

// 根配置：仅三块。
struct PackagerConfiguration {
    AppConfig app;
    PackageConfig package;
    HooksConfig hooks;
};

} // namespace MultiThreadedInstaller
