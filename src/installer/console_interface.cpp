#include "installer/console_interface.h"
#include <iostream>
#include <sstream>
#include "common/utf8_utils.h"
#include <algorithm>
#include <cctype>
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
    
    std::string algorithmStr = getUserInput("Choose compression algorithm (lzma|zstd) [lzma]: ");
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

ConsoleInterface::InstallerArgs ConsoleInterface::parseInstallerArgs(int argc, char* argv[]) {
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
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            args.showHelp = true;
        } else if (arg == "-s" || arg == "--silent") {
            args.silent = true;
        } else if (arg == "--uninstall") {
            args.uninstall = true;
        } else if (arg == "--all-components") {
            args.installAllComponents = true;
        } else if (arg == "--component" && i + 1 < argc) {
            std::string id = trim(argv[++i]);
            if (!id.empty()) {
                args.selectedComponents.push_back(id);
            }
        } else if (arg == "--components" && i + 1 < argc) {
            appendComponents(argv[++i]);
        } else if ((arg == "-d" || arg == "--destination") && i + 1 < argc) {
            args.defaultDestination = argv[++i];
        }
    }
    
    return args;
}

void ConsoleInterface::showPackagerHelp() {
    std::cout << "Multi-Threaded Installer Packager" << std::endl;
    std::cout << "Usage: packager [options] <input_directory> <output_file>" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -a, --algorithm <lzma|zstd>    Choose compression algorithm (default: lzma)" << std::endl;
    std::cout << "  -l, --level <level>            Compression level (lzma: 0-9, zstd: 1-22)" << std::endl;
    std::cout << "  -p, --data-out <file>          Write external data package" << std::endl;
    std::cout << "  -t, --threads <count>          Number of compression threads (default: CPU cores)" << std::endl;
    std::cout << "  -v, --verbose                  Show detailed information" << std::endl;
    std::cout << "  -h, --help                     Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  packager -a lzma -l 5 ./input ./output/installer.exe" << std::endl;
    std::cout << "  packager -a zstd -l 3 ./input ./output/installer.exe" << std::endl;
}

void ConsoleInterface::showInstallerHelp() {
    std::cout << "Multi-Threaded Installer" << std::endl;
    std::cout << "Usage: installer [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -d, --destination <directory>  Default installation directory" << std::endl;
    std::cout << "  -s, --silent                   Silent installation mode" << std::endl;
    std::cout << "  --debug                        Show console alongside GUI" << std::endl;
    std::cout << "  --uninstall                    Uninstall using saved manifest" << std::endl;
    std::cout << "  --component <id>               Select one component id (repeatable)" << std::endl;
    std::cout << "  --components <id1,id2,...>     Select multiple component ids" << std::endl;
    std::cout << "  --all-components               Install all optional components" << std::endl;
    std::cout << "  -h, --help                     Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  installer -d C:\\Program Files\\MyApp" << std::endl;
    std::cout << "  installer -s -d C:\\Program Files\\MyApp" << std::endl;
    std::cout << "  installer -s --components main_app,chrome_plugin" << std::endl;
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
            return std::filesystem::exists(PathFromUtf8(path));
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
    if (lower == "zstd") {
        return CompressionAlgorithm::ZSTD;
    }
    
    return CompressionAlgorithm::LZMA_HIGH;
}

std::string ConsoleInterface::compressionAlgorithmToString(CompressionAlgorithm algorithm) {
    switch (algorithm) {
        case CompressionAlgorithm::LZMA_HIGH:
            return "LZMA";
        case CompressionAlgorithm::ZSTD:
            return "ZSTD";
        default:
            return "Unknown";
    }
}

} // namespace MultiThreadedInstaller
