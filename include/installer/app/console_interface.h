#pragma once

#include "common/config_types.h"
#include <string>
#include <vector>
#include <utility>

namespace MultiThreadedInstaller {

/// 控制台/命令行支持：命令行参数解析、帮助文本、进度条与各类消息输出，packager 与
/// installer/uninstaller 共用。
class CliSupport {
public:
    CliSupport() = default;
    ~CliSupport() = default;

    void showPackagerMenu();  ///< 显示打包器交互菜单（交互模式）。
    /// 交互式获取打包输入（输入/输出路径 + 压缩算法）。
    bool getPackagerInput(std::string& inputPath, std::string& outputPath,
                         CompressionAlgorithm& algorithm);
    void showPackagingProgress(const std::string& currentFolder, float progress);   ///< 打包进度。

    void showInstallationProgress(const std::string& currentFolder, float progress); ///< 安装进度。
    /// 显示安装最终结果（成功/需重启/错误列表）。
    void showInstallationResult(bool success,
                                bool rebootRequired,
                                const std::vector<std::string>& errors);

    void showError(const std::string& message);    ///< 输出错误消息。
    void showWarning(const std::string& message);  ///< 输出警告消息。
    bool confirmAction(const std::string& prompt);  ///< 询问用户确认（y/n）。
    void showInfo(const std::string& message);      ///< 输出信息消息。

    /// 打包器命令行参数（--input/--config/--output）。
    struct PackagerArgs {
        std::string inputPath;   ///< --input 目录。
        std::string configPath;  ///< --config 目录（含 packager.yaml/resources）。
        std::string outputPath;  ///< --output 安装器输出文件。
        std::string error;       ///< 解析错误（非空表示出错）。
        bool showHelp = false;   ///< 是否请求帮助。
    };

    /// 安装器/卸载器命令行参数。
    struct InstallerArgs {
        std::string defaultDestination;                 ///< -d/--destination 安装目录。
        bool upgrade = false;                           ///< --upgrade 升级模式。
        bool silent = false;                            ///< -s/--silent 静默模式。
        bool autoStartupSpecified = false;              ///< 是否显式指定开机自启。
        bool autoStartupEnabled = false;                ///< 开机自启取值。
        bool desktopIconSpecified = false;              ///< 是否显式指定桌面图标。
        bool desktopIconEnabled = false;                ///< 桌面图标取值。
        std::string uninstallManifestPath;              ///< --uninstall-manifest 指定清单。
        bool showHelp = false;                          ///< 是否请求帮助。
    };

    PackagerArgs parsePackagerArgs(int argc, char* argv[]);    ///< 解析打包器参数。
    InstallerArgs parseInstallerArgs(int argc, char* argv[]);  ///< 解析安装/卸载参数。

    void showPackagerHelp();     ///< 打印打包器帮助。
    void showInstallerHelp();    ///< 打印安装器帮助。
    void showUninstallerHelp();  ///< 打印卸载器帮助。

private:
    void clearScreen();                                          ///< 清屏。
    void showProgressBar(float progress, int width = 50);        ///< 渲染文本进度条。
    std::string getUserInput(const std::string& prompt);         ///< 读一行用户输入。
    bool validatePath(const std::string& path, bool shouldExist = true);  ///< 校验路径。
    CompressionAlgorithm parseCompressionAlgorithm(const std::string& algorithmStr);  ///< 字符串→算法枚举。
    std::string compressionAlgorithmToString(CompressionAlgorithm algorithm);         ///< 算法枚举→字符串。
};

} // namespace MultiThreadedInstaller
