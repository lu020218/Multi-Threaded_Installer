#pragma once

#include "common/config_types.h"
#include <string>
#include <vector>
#include <utility>

namespace MultiThreadedInstaller {

class CliSupport {
public:
    CliSupport() = default;
    ~CliSupport() = default;
    

    void showPackagerMenu();
    bool getPackagerInput(std::string& inputPath, std::string& outputPath, 
                         CompressionAlgorithm& algorithm);
    void showPackagingProgress(const std::string& currentFolder, float progress);
    

    void showInstallationProgress(const std::string& currentFolder, float progress);
    void showInstallationResult(bool success,
                                bool rebootRequired,
                                const std::vector<std::string>& errors);
    

    void showError(const std::string& message);
    void showWarning(const std::string& message);
    bool confirmAction(const std::string& prompt);
    void showInfo(const std::string& message);
    

    struct PackagerArgs {
        std::string inputPath;
        std::string configPath;
        std::string outputPath;
        std::string error;
        bool showHelp = false;
    };
    
    struct InstallerArgs {
        std::string defaultDestination;
        std::vector<std::string> selectedComponents;
        bool installAllComponents = false;
        bool silent = false;
        bool autoStartupSpecified = false;
        bool autoStartupEnabled = false;
        bool desktopIconSpecified = false;
        bool desktopIconEnabled = false;
        bool showHelp = false;
    };
    

    PackagerArgs parsePackagerArgs(int argc, char* argv[]);
    

    InstallerArgs parseInstallerArgs(int argc, char* argv[]);
    

    void showPackagerHelp();
    void showInstallerHelp();
    void showUninstallerHelp();
    
private:

    void clearScreen();
    

    void showProgressBar(float progress, int width = 50);
    

    std::string getUserInput(const std::string& prompt);
    

    bool validatePath(const std::string& path, bool shouldExist = true);
    

    CompressionAlgorithm parseCompressionAlgorithm(const std::string& algorithmStr);
    

    std::string compressionAlgorithmToString(CompressionAlgorithm algorithm);
};

} // namespace MultiThreadedInstaller
