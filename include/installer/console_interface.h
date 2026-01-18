#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <utility>

namespace MultiThreadedInstaller {

class ConsoleInterface {
public:
    ConsoleInterface() = default;
    ~ConsoleInterface() = default;
    
    // 打包器界面
    void showPackagerMenu();
    bool getPackagerInput(std::string& inputPath, std::string& outputPath, 
                         CompressionAlgorithm& algorithm);
    void showPackagingProgress(const std::string& currentFolder, float progress);
    
    // 安装程序界面
    void showInstallerMenu();
    bool getInstallationPaths(std::vector<std::pair<std::string, std::string>>& folderMappings);
    void showInstallationProgress(const std::string& currentFolder, float progress);
    void showInstallationResult(bool success, const std::vector<std::string>& errors);
    
    // 通用界面
    void showError(const std::string& message);
    void showWarning(const std::string& message);
    bool confirmAction(const std::string& prompt);
    void showInfo(const std::string& message);
    
    // 命令行参数解析
    struct PackagerArgs {
        std::string inputPath;
        std::string outputPath;
        std::string dataPackagePath;
        CompressionAlgorithm algorithm = CompressionAlgorithm::LZMA_HIGH;
        int compressionLevel = -1;  // -1表示使用默认值
        int threadCount = -1;       // -1表示使用默认值
        bool verbose = false;
        bool showHelp = false;
    };
    
    struct InstallerArgs {
        std::string defaultDestination;
        std::string dataPackagePath;
        std::vector<std::pair<std::string, std::string>> folderMappings;
        int threadCount = -1;       // -1表示使用默认值
        bool force = false;
        bool silent = false;
        bool verbose = false;
        bool showHelp = false;
        bool uninstall = false;
    };
    
    // 解析打包器命令行参数
    PackagerArgs parsePackagerArgs(int argc, char* argv[]);
    
    // 解析安装程序命令行参数
    InstallerArgs parseInstallerArgs(int argc, char* argv[]);
    
    // 显示帮助信息
    void showPackagerHelp();
    void showInstallerHelp();
    
private:
    // 清屏
    void clearScreen();
    
    // 显示进度条
    void showProgressBar(float progress, int width = 50);
    
    // 获取用户输入
    std::string getUserInput(const std::string& prompt);
    
    // 验证路径
    bool validatePath(const std::string& path, bool shouldExist = true);
    
    // 解析压缩算法字符串
    CompressionAlgorithm parseCompressionAlgorithm(const std::string& algorithmStr);
    
    // 压缩算法转字符串
    std::string compressionAlgorithmToString(CompressionAlgorithm algorithm);
};

} // namespace MultiThreadedInstaller
