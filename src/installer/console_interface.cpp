#include "installer/console_interface.h"
#include "common/installer_logger.h"
#include <iostream>
#include <sstream>
#include "common/utf8_utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace MultiThreadedInstaller {

void CliSupport::showPackagerMenu() {
    clearScreen();
    std::cout << "=== Multi-Threaded Installer Packager ===" << std::endl;
    std::cout << "This tool will package folders into a self-extracting installer." << std::endl;
    std::cout << std::endl;
}

bool CliSupport::getPackagerInput(std::string& inputPath, std::string& outputPath,
                                       CompressionAlgorithm& algorithm) {
    inputPath = getUserInput("Enter input directory path: ");
    if (!validatePath(inputPath, true)) {
        showError("Invalid input directory path");
        return false;
    }
    
    outputPath = getUserInput("Enter output installer path: ");
    if (outputPath.empty()) {
        showError("Output path cannot be empty");
        return false;
    }
    
    std::string algorithmStr = getUserInput("Choose compression algorithm (xz|zstd) [xz]: ");
    if (algorithmStr.empty()) {
        algorithmStr = "xz";
    }
    
    algorithm = parseCompressionAlgorithm(algorithmStr);
    
    return true;
}

void CliSupport::showPackagingProgress(const std::string& currentFolder, float progress) {
    std::cout << "\rPackaging: " << currentFolder << " ";
    showProgressBar(progress);
    std::cout.flush();
}

void CliSupport::showInstallationProgress(const std::string& currentFolder, float progress) {
    std::cout << "\rInstalling: " << currentFolder << " ";
    showProgressBar(progress);
    std::cout.flush();
}

void CliSupport::showInstallationResult(bool success,
                                        bool rebootRequired,
                                        const std::vector<std::string>& errors) {
    std::cout << std::endl;
    
    if (rebootRequired) {
        std::cout << "WARNING: Installation requires a system reboot to finish replacing locked files." << std::endl;
        logInstallerWarning("[CLI] Installation requires reboot to complete locked file replacement.");
        return;
    }

    if (success) {
        std::cout << "OK: Installation completed successfully!" << std::endl;
        logInstallerInfo("[CLI] Installation completed successfully.");
    } else {
        std::cout << "ERROR: Installation completed with errors:" << std::endl;
        logInstallerError("[CLI] Installation completed with errors.");
        for (const auto& error : errors) {
            std::cout << "  - " << error << std::endl;
            logInstallerError(std::string("[CLI] ") + error);
        }
    }
}

void CliSupport::showError(const std::string& message) {
    std::cout << "ERROR: " << message << std::endl;
    logInstallerError(std::string("[CLI] ") + message);
}

void CliSupport::showWarning(const std::string& message) {
    std::cout << "WARNING: " << message << std::endl;
    logInstallerWarning(std::string("[CLI] ") + message);
}

bool CliSupport::confirmAction(const std::string& prompt) {
    std::string response = getUserInput(prompt + " (y/N): ");
    std::transform(response.begin(), response.end(), response.begin(), ::tolower);
    return response == "y" || response == "yes";
}

void CliSupport::showInfo(const std::string& message) {
    std::cout << "INFO: " << message << std::endl;
    logInstallerInfo(std::string("[CLI] ") + message);
}

CliSupport::PackagerArgs CliSupport::parsePackagerArgs(int argc, char* argv[]) {
    PackagerArgs args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            args.showHelp = true;
        } else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
        } else if ((arg == "-p" || arg == "--data-out") && i + 1 < argc) {
            args.dataPackagePath = argv[++i];
        } else if ((arg == "-a" || arg == "--algorithm") && i + 1 < argc) {
            args.algorithm = parseCompressionAlgorithm(argv[++i]);
            args.algorithmExplicitlySet = true;
        } else if ((arg == "-l" || arg == "--level") && i + 1 < argc) {
            args.compressionLevel = std::stoi(argv[++i]);
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            args.threadCount = std::stoi(argv[++i]);
        } else if (args.inputPath.empty()) {
            args.inputPath = arg;
        } else if (args.outputPath.empty()) {
            args.outputPath = arg;
        }
    }
    
    return args;
}

CliSupport::InstallerArgs CliSupport::parseInstallerArgs(int argc, char* argv[]) {
    InstallerArgs args;

    auto trim = [](std::string value) {
        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        return value;
    };

    auto appendComponents = [&](const std::string& csv) {
        size_t start = 0;
        while (start <= csv.size()) {
            size_t end = csv.find(',', start);
            if (end == std::string::npos) {
                end = csv.size();
            }
            std::string item = trim(csv.substr(start, end - start));
            if (!item.empty()) {
                args.selectedComponents.push_back(item);
            }
            if (end == csv.size()) {
                break;
            }
            start = end + 1;
        }
    };

    auto parseBoolValue = [](const std::string& value, bool& parsed) -> bool {
        std::string lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
            parsed = true;
            return true;
        }
        if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
            parsed = false;
            return true;
        }
        return false;
    };
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            args.showHelp = true;
        } else if (arg == "-s" || arg == "--silent") {
            args.silent = true;
        } else if (arg == "--repair") {
            args.repair = true;
        } else if (arg == "--components" && i + 1 < argc) {
            std::string value = trim(argv[++i]);
            std::string lowered = value;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowered == "all" || lowered == "*") {
                args.installAllComponents = true;
                args.selectedComponents.clear();
            } else {
                appendComponents(value);
            }
        } else if (arg == "--auto-startup" && i + 1 < argc) {
            bool parsed = false;
            if (parseBoolValue(trim(argv[++i]), parsed)) {
                args.autoStartupSpecified = true;
                args.autoStartupEnabled = parsed;
            }
        } else if (arg == "--desktop-icon" && i + 1 < argc) {
            bool parsed = false;
            if (parseBoolValue(trim(argv[++i]), parsed)) {
                args.desktopIconSpecified = true;
                args.desktopIconEnabled = parsed;
            }
        } else if ((arg == "-d" || arg == "--destination") && i + 1 < argc) {
            args.defaultDestination = argv[++i];
        }
    }
    
    return args;
}

void CliSupport::showPackagerHelp() {
    std::cout << "Multi-Threaded Installer Packager" << std::endl;
    std::cout << "Usage: packager [options] <input_directory> <output_file>" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -a, --algorithm <xz|zstd>      Choose compression algorithm (default: xz)" << std::endl;
    std::cout << "  -l, --level <level>            Compression level (xz/lzma2: 0-9, zstd: 1-22)" << std::endl;
    std::cout << "  -p, --data-out <file>          Write external data package" << std::endl;
    std::cout << "  -t, --threads <count>          Number of compression threads (default: CPU cores)" << std::endl;
    std::cout << "  -v, --verbose                  Show detailed information" << std::endl;
    std::cout << "  -h, --help                     Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  packager -a xz -l 5 ./input ./output/installer.exe" << std::endl;
    std::cout << "  packager -a zstd -l 3 ./input ./output/installer.exe" << std::endl;
}

void CliSupport::showInstallerHelp() {
    std::cout << "Multi-Threaded Installer" << std::endl;
    std::cout << "Usage: installer [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -d, --destination <directory>  Default installation directory" << std::endl;
    std::cout << "  -s, --silent                   Silent installation mode" << std::endl;
    std::cout << "  --repair                       Repair an existing installation" << std::endl;
    std::cout << "  --components <id1,id2,...|all> Select optional components or all" << std::endl;
    std::cout << "  --auto-startup <true|false>    Enable or disable auto startup" << std::endl;
    std::cout << "  --desktop-icon <true|false>    Enable or disable desktop icon" << std::endl;
    std::cout << "  -h, --help                     Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  installer -d C:\\Program Files\\MyApp" << std::endl;
    std::cout << "  installer -s -d C:\\Program Files\\MyApp" << std::endl;
    std::cout << "  installer --repair" << std::endl;
    std::cout << "  installer -s --components main_app,chrome_plugin" << std::endl;
    std::cout << "  installer -s --components all" << std::endl;
    std::cout << "  installer -s --auto-startup true --desktop-icon false" << std::endl;
}

void CliSupport::showUninstallerHelp() {
    std::cout << "Multi-Threaded Uninstaller" << std::endl;
    std::cout << "Usage: uninstaller [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -s, --silent                   Silent uninstall mode" << std::endl;
    std::cout << "  -h, --help                     Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  uninstaller.exe" << std::endl;
    std::cout << "  uninstaller.exe --silent" << std::endl;
}

void CliSupport::clearScreen() {
    #ifdef _WIN32
    system("cls");
    #else
    system("clear");
    #endif
}

void CliSupport::showProgressBar(float progress, int width) {
    int filledWidth = static_cast<int>(progress * width);
    
    std::cout << "[";
    for (int i = 0; i < width; ++i) {
        if (i < filledWidth) {
            std::cout << "=";
        } else if (i == filledWidth) {
            std::cout << ">";
        } else {
            std::cout << " ";
        }
    }
    std::cout << "] " << static_cast<int>(progress * 100) << "%";
}

std::string CliSupport::getUserInput(const std::string& prompt) {
    std::cout << prompt;
    std::string input;
    std::getline(std::cin, input);
    return input;
}

bool CliSupport::validatePath(const std::string& path, bool shouldExist) {
    if (path.empty()) {
        return false;
    }
    
    if (shouldExist) {
        try {
            return std::filesystem::exists(PathFromUtf8(path));
        } catch (const std::exception&) {
            return false;
        }
    }
    
    return true;
}

CompressionAlgorithm CliSupport::parseCompressionAlgorithm(const std::string& algorithmStr) {
    std::string lower = algorithmStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "xz" || lower == "lzma2" || lower == "xz_lzma2" || lower == "lzma2_xz") {
        return CompressionAlgorithm::LZMA2_XZ;
    }
    if (lower == "zstd") {
        return CompressionAlgorithm::ZSTD;
    }
    
    return CompressionAlgorithm::LZMA2_XZ;
}

std::string CliSupport::compressionAlgorithmToString(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::LZMA2_XZ:
            return "XZ/LZMA2";
        case CompressionAlgorithm::ZSTD:
            return "ZSTD";
        default:
            return "Unknown";
    }
}

} // namespace MultiThreadedInstaller
