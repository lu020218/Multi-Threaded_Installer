#include "packager/configuration_loader.h"
#include "packager/configuration_validator.h"
#include "common/types.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using MultiThreadedInstaller::CompressionAlgorithm;
using MultiThreadedInstaller::ConfigurationLoader;
using MultiThreadedInstaller::ConfigurationValidator;
using MultiThreadedInstaller::InstallStateMode;

void AssertTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool ContainsText(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool ContainsError(const std::vector<std::string>& errors, const std::string& needle) {
    for (const auto& error : errors) {
        if (ContainsText(error, needle)) {
            return true;
        }
    }
    return false;
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    AssertTrue(static_cast<bool>(out), "Failed to open file for writing: " + path.string());
    out << text;
    AssertTrue(static_cast<bool>(out), "Failed to write file: " + path.string());
}

void MakeConfigValidatorFriendly(MultiThreadedInstaller::PackagerConfiguration& config) {
    config.installState.mode = InstallStateMode::FILE;
    config.installState.filePath = "%ProgramData%\\CompressionOptTest\\install.state";
    config.installState.useMutex = false;
    config.installState.mutexName.clear();
}

void TestAcceptsJsonZstdCompressionOptions(const std::filesystem::path& tempRoot) {
    const auto path = tempRoot / "valid_zstd.json";
    WriteTextFile(path,
                  "{\n"
                  "  \"Version\": \"1.0\",\n"
                  "  \"AppName\": \"CompressionConfigJson\",\n"
                  "  \"InstallDir\": \"%ProgramFiles%\",\n"
                  "  \"compressionAlgorithm\": \"zstd\",\n"
                  "  \"compressionLevel\": 5\n"
                  "}\n");

    ConfigurationLoader loader;
    auto loaded = loader.loadConfigurationFromPath(path.string());
    AssertTrue(loaded.has_value(), "Expected valid zstd json config to load: " + loader.getLastError());
    AssertTrue(loaded->compressionAlgorithm == CompressionAlgorithm::ZSTD, "Expected algorithm zstd");
    AssertTrue(loaded->compressionLevel == 5, "Expected compressionLevel 5");

    MakeConfigValidatorFriendly(*loaded);
    ConfigurationValidator validator;
    const auto validation = validator.validate(*loaded, tempRoot.string());
    AssertTrue(validation.isValid, "Expected valid zstd config to pass validator");
}

void TestAcceptsYamlLzmaCompressionOptions(const std::filesystem::path& tempRoot) {
    const auto path = tempRoot / "valid_lzma.yaml";
    WriteTextFile(path,
                  "Version: \"1.0\"\n"
                  "AppName: \"CompressionConfigYaml\"\n"
                  "InstallDir: \"%ProgramFiles%\"\n"
                  "compressionAlgorithm: \"lzma\"\n"
                  "compressionLevel: 9\n");

    ConfigurationLoader loader;
    auto loaded = loader.loadConfigurationFromPath(path.string());
    AssertTrue(loaded.has_value(), "Expected valid lzma yaml config to load: " + loader.getLastError());
    AssertTrue(loaded->compressionAlgorithm == CompressionAlgorithm::LZMA_HIGH, "Expected algorithm lzma");
    AssertTrue(loaded->compressionLevel == 9, "Expected compressionLevel 9");
}

void TestRejectsUnknownCompressionAlgorithm(const std::filesystem::path& tempRoot) {
    const auto path = tempRoot / "invalid_algo.json";
    WriteTextFile(path,
                  "{\n"
                  "  \"Version\": \"1.0\",\n"
                  "  \"AppName\": \"InvalidAlgo\",\n"
                  "  \"InstallDir\": \"%ProgramFiles%\",\n"
                  "  \"compressionAlgorithm\": \"brotli\"\n"
                  "}\n");

    ConfigurationLoader loader;
    auto loaded = loader.loadConfigurationFromPath(path.string());
    AssertTrue(!loaded.has_value(), "Expected unknown algorithm to be rejected");
    AssertTrue(ContainsText(loader.getLastError(), "compressionAlgorithm"),
               "Expected error message to mention compressionAlgorithm");
}

void TestRejectsNonIntegerCompressionLevel(const std::filesystem::path& tempRoot) {
    const auto path = tempRoot / "invalid_level_type.yaml";
    WriteTextFile(path,
                  "Version: \"1.0\"\n"
                  "AppName: \"InvalidLevelType\"\n"
                  "InstallDir: \"%ProgramFiles%\"\n"
                  "compressionAlgorithm: \"zstd\"\n"
                  "compressionLevel: \"fast\"\n");

    ConfigurationLoader loader;
    auto loaded = loader.loadConfigurationFromPath(path.string());
    AssertTrue(!loaded.has_value(), "Expected non-integer compressionLevel to be rejected");
    AssertTrue(ContainsText(loader.getLastError(), "compressionLevel"),
               "Expected error message to mention compressionLevel");
}

void TestRejectsOutOfRangeCompressionLevelViaValidator(const std::filesystem::path& tempRoot) {
    const auto path = tempRoot / "invalid_level_range.json";
    WriteTextFile(path,
                  "{\n"
                  "  \"Version\": \"1.0\",\n"
                  "  \"AppName\": \"InvalidLevelRange\",\n"
                  "  \"InstallDir\": \"%ProgramFiles%\",\n"
                  "  \"compressionAlgorithm\": \"zstd\",\n"
                  "  \"compressionLevel\": 99\n"
                  "}\n");

    ConfigurationLoader loader;
    auto loaded = loader.loadConfigurationFromPath(path.string());
    AssertTrue(loaded.has_value(), "Expected numeric compressionLevel to parse");

    MakeConfigValidatorFriendly(*loaded);
    ConfigurationValidator validator;
    const auto validation = validator.validate(*loaded, tempRoot.string());
    AssertTrue(!validation.isValid, "Expected out-of-range zstd level to be rejected");
    AssertTrue(ContainsError(validation.errors, "Invalid compressionLevel"),
               "Expected validation error for invalid compressionLevel");
}

} // namespace

int main() {
    try {
        const auto tempRoot =
            std::filesystem::temp_directory_path() / "mti_config_compression_options_test";
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
        std::filesystem::create_directories(tempRoot, ec);
        AssertTrue(!ec, "Failed to prepare temporary test directory");

        TestAcceptsJsonZstdCompressionOptions(tempRoot);
        TestAcceptsYamlLzmaCompressionOptions(tempRoot);
        TestRejectsUnknownCompressionAlgorithm(tempRoot);
        TestRejectsNonIntegerCompressionLevel(tempRoot);
        TestRejectsOutOfRangeCompressionLevelViaValidator(tempRoot);

        std::filesystem::remove_all(tempRoot, ec);
        std::cout << "config compression options test passed" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "config compression options test failed: " << ex.what() << std::endl;
        return 1;
    }
}
