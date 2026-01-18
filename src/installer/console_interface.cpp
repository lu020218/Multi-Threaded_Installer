#include "installer/console_interface.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace MultiThreadedInstaller {

void ConsoleInterface::showPackagerMenu() {
    clearScreen();
    std::cout << "=== Multi-Threaded Installer Packager ===" << std::endl;
    std::cout << "This tool will package folders into a self-extracting installer." << std::endl;
    std::cout << std::endl;
}

bool ConsoleInterface::getPackagerInput(std::string& inputPath, std::string& outputPath,
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
    
    std::string algorithmStr = getUserInput("Choose compression algorithm (lzma) [lzma]: ");
    if (algorithmStr.empty()) {
        algorithmStr = "lzma";
    }
    
    algorithm = parseCompressionAlgorithm(algorithmStr);
    
    return true;
}

void ConsoleInterface::showPackagingProgress(const std::string& currentFolder, float progress) {
    std::cout << "\rPackaging: " << currentFolder << " ";
    showProgressBar(progress);
    std::cout.flush();
}

void ConsoleInterface::showInstallerMenu() {
    clearScreen();
    std::cout << "=== Multi-Threaded Installer ===" << std::endl;
    std::cout << "This installer will extract and install packaged folders." << std::endl;
    std::cout << std::endl;
}

bool ConsoleInterface::getInstallationPaths(std::vector<std::pair<std::string, std::string>>& folderMappings) {
    std::cout << "Enter folder mappings (format: folderName=targetPath)" << std::endl;
    std::cout << "Press Enter with empty line to finish:" << std::endl;
    
    std::string line;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, line);
        
        if (line.empty()) {
            break;
        }
        
        size_t equalPos = line.find('=');
        if (equalPos == std::string::npos) {
            showWarning("Invalid format. Use: folderName=targetPath");
            continue;
        }
        
        std::string folderName = line.substr(0, equalPos);
        std::string targetPath = line.substr(equalPos + 1);
        
        folderMappings.emplace_back(folderName, targetPath);
    }
    
    return !folderMappings.empty();
}

void ConsoleInterface::showInstallationProgress(const std::string& currentFolder, float progress) {
    std::cout << "\rInstalling: " << currentFolder << " ";
    showProgressBar(progress);
    std::cout.flush();
}

void ConsoleInterface::showInstallationResult(bool success, const std::vector<std::string>& errors) {
    std::cout << std::endl;
    
    if (success) {
        std::cout << "OK: Installation completed successfully!" << std::endl;
    } else {
        std::cout << "ERROR: Installation completed with errors:" << std::endl;
        for (const auto& error : errors) {
            std::cout << "  - " << error << std::endl;
        }
    }
}

void ConsoleInterface::showError(const std::string& message) {
    std::cout << "ERROR: " << message << std::endl;
}

void ConsoleInterface::showWarning(const std::string& message) {
    std::cout << "WARNING: " << message << std::endl;
}

bool ConsoleInterface::confirmAction(const std::string& prompt) {
    std::string response = getUserInput(prompt + " (y/N): ");
    std::transform(response.begin(), response.end(), response.begin(), ::tolower);
    return response == "y" || response == "yes";
}

void ConsoleInterface::showInfo(const std::string& message) {
    std::cout << "INFO: " << message << std::endl;
}

ConsoleInterface::PackagerArgs ConsoleInterface::parsePackagerArgs(int argc, char* argv[]) {
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

ConsoleInterface::InstallerArgs ConsoleInterface::parseInstallerArgs(int argc, char* argv[]) {
    InstallerArgs args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            args.showHelp = true;
        } else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
        } else if ((arg == "-p" || arg == "--data-package") && i + 1 < argc) {
            args.dataPackagePath = argv[++i];
        } else if (arg == "-s" || arg == "--silent") {
            args.silent = true;
        } else if (arg == "-f" || arg == "--force") {
            args.force = true;
        } else if (arg == "--uninstall") {
            args.uninstall = true;
        } else if ((arg == "-d" || arg == "--destination") && i + 1 < argc) {
            args.defaultDestination = argv[++i];
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            args.threadCount = std::stoi(argv[++i]);
        } else if (arg.find('=') != std::string::npos) {
            size_t equalPos = arg.find('=');
            std::string folderName = arg.substr(0, equalPos);
            std::string targetPath = arg.substr(equalPos + 1);
            args.folderMappings.emplace_back(folderName, targetPath);
        }
    }
    
    return args;
}

void ConsoleInterface::showPackagerHelp() {
    std::cout << "Multi-Threaded Installer Packager" << std::endl;
    std::cout << "Usage: packager [options] <input_directory> <output_file>" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -a, --algorithm <lzma>         Choose compression algorithm (default: lzma)" << std::endl;
    std::cout << "  -l, --level <level>            Compression level (lzma: 0-9)" << std::endl;
    std::cout << "  -p, --data-out <file>          Write external data package" << std::endl;
    std::cout << "  -t, --threads <count>          Number of compression threads (default: CPU cores)" << std::endl;
    std::cout << "  -v, --verbose                  Show detailed information" << std::endl;
    std::cout << "  -h, --help                     Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  packager -a lzma -l 5 ./input ./output/installer.exe" << std::endl;
}

void ConsoleInterface::showInstallerHelp() {
    std::cout << "Multi-Threaded Installer" << std::endl;
    std::cout << "Usage: installer [options] [folder_mappings...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -d, --destination <directory>  Default installation directory" << std::endl;
    std::cout << "  -p, --data-package <file>      Use external data package" << std::endl;
    std::cout << "  -t, --threads <count>          Number of decompression threads (default: CPU cores)" << std::endl;
    std::cout << "  -f, --force                    Force overwrite existing files" << std::endl;
    std::cout << "  -s, --silent                   Silent installation mode" << std::endl;
    std::cout << "  --uninstall                    Uninstall using saved manifest" << std::endl;
    std::cout << "  -v, --verbose                  Show detailed information" << std::endl;
    std::cout << "  -h, --help                     Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Folder mapping format:" << std::endl;
    std::cout << "  <folder_name>=<target_path>" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  installer -d C:\\Program Files\\MyApp" << std::endl;
    std::cout << "  installer folderA=C:\\App\\A folderB=C:\\App\\B" << std::endl;
    std::cout << "  installer -s -f -d C:\\Program Files\\MyApp" << std::endl;
    std::cout << "  uninstall.exe (runs uninstall automatically)" << std::endl;
}

void ConsoleInterface::clearScreen() {
    #ifdef _WIN32
    system("cls");
    #else
    system("clear");
    #endif
}

void ConsoleInterface::showProgressBar(float progress, int width) {
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

std::string ConsoleInterface::getUserInput(const std::string& prompt) {
    std::cout << prompt;
    std::string input;
    std::getline(std::cin, input);
    return input;
}

bool ConsoleInterface::validatePath(const std::string& path, bool shouldExist) {
    if (path.empty()) {
        return false;
    }
    
    if (shouldExist) {
        try {
            return std::filesystem::exists(path);
        } catch (const std::exception&) {
            return false;
        }
    }
    
    return true;
}

CompressionAlgorithm ConsoleInterface::parseCompressionAlgorithm(const std::string& algorithmStr) {
    std::string lower = algorithmStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "lzma" || lower == "7z") {
        return CompressionAlgorithm::LZMA_HIGH;
    }
    
    return CompressionAlgorithm::LZMA_HIGH;
}

std::string ConsoleInterface::compressionAlgorithmToString(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::LZMA_HIGH:
            return "LZMA";
        default:
            return "Unknown";
    }
}

} // namespace MultiThreadedInstaller
