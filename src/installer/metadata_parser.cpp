#include "installer/metadata_parser.h"
#include "common/package_manifest_codec.h"
#include "installer/package_manifest_validator.h"
#include "installer/installer_helpers.h"
#include "common/installer_logger.h"
#include "common/utf8_utils.h"
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace MultiThreadedInstaller {
namespace {

#define META_LOG() \
    do { \
        logInstallerError(std::string("[Metadata] Parse/read error at line ") + std::to_string(__LINE__)); \
    } while (0)

} // namespace

ExtendedInstallationMetadata MetadataParser::parseExtendedEmbeddedMetadata() {
    std::vector<uint8_t> embeddedData = dataPackagePath_.empty() ? readEmbeddedData() : readExternalMetadata();
    if (embeddedData.empty()) {
        META_LOG();
        return ExtendedInstallationMetadata{};
    }
    
    return deserializeExtendedMetadata(embeddedData);
}

std::vector<uint8_t> MetadataParser::readEmbeddedData() {
    std::string executablePath = getCurrentExecutablePath();
    if (executablePath.empty()) {
        return {};
    }
    
    std::ifstream file(PathFromUtf8(executablePath), std::ios::binary);
    if (!file) {
        META_LOG();
        return {};
    }
    

    file.seekg(0, std::ios::end);
    uint64_t fileSize = static_cast<uint64_t>(file.tellg());

    uint64_t logicalEnd = fileSize;
    DataLocator locator;
    if (!readEmbeddedLocator(file, fileSize, logicalEnd, locator)) {
        return {};
    }
    

    file.seekg(locator.metadataOffset);
    std::vector<uint8_t> metadata(locator.metadataSize);
    file.read(reinterpret_cast<char*>(metadata.data()), locator.metadataSize);
    
    if (file.gcount() != static_cast<std::streamsize>(locator.metadataSize)) {
        META_LOG();
        return {};
    }
    
    return metadata;
}

ExtendedInstallationMetadata MetadataParser::deserializeExtendedMetadata(const std::vector<uint8_t>& data) {
    PackageManifest manifest;
    std::string manifestError;
    if (DeserializePackageManifest(data, manifest, manifestError)) {
        std::string validationError;
        if (!ValidatePackageManifest(manifest, validationError)) {
            META_LOG();
            return ExtendedInstallationMetadata{};
        }
        return PackageManifestToExtendedMetadata(manifest);
    }
    logInstallerError("[Metadata] Unsupported package manifest format: " + manifestError);
    return ExtendedInstallationMetadata{};
}

std::vector<uint8_t> MetadataParser::readExternalMetadata() {
    std::ifstream file(PathFromUtf8(dataPackagePath_), std::ios::binary);
    if (!file) {
        META_LOG();
        return {};
    }
    
    DataPackageHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(DataPackageHeader));
    if (!file || header.magic != Constants::DATA_MAGIC_NUMBER) {
        META_LOG();
        return {};
    }
    
    file.seekg(static_cast<std::streamoff>(header.metadataOffset));
    std::vector<uint8_t> metadata(header.metadataSize);
    file.read(reinterpret_cast<char*>(metadata.data()), header.metadataSize);
    
    if (file.gcount() != static_cast<std::streamsize>(header.metadataSize)) {
        META_LOG();
        return {};
    }
    
    return metadata;
}

bool MetadataParser::validateMetadata(const ExtendedInstallationMetadata& metadata) {
    std::string error;
    if (!ValidateExtendedInstallationMetadata(metadata, error)) {
        META_LOG();
        if (!error.empty()) {
            logInstallerError("[Metadata] Validation failed: " + error);
        }
        return false;
    }
    return true;
}

bool MetadataParser::readEmbeddedLocator(std::ifstream& file,
                                         uint64_t fileSize,
                                         uint64_t& logicalEnd,
                                         DataLocator& locator) {
    EmbeddedDataLocatorRecord resolvedLocator;
    if (!findEmbeddedDataLocator(file, fileSize, logicalEnd, resolvedLocator)) {
        META_LOG();
        return false;
    }

    locator.magic = resolvedLocator.magic;
    locator.metadataOffset = resolvedLocator.metadataOffset;
    locator.metadataSize = resolvedLocator.metadataSize;
    locator.dataOffset = resolvedLocator.dataOffset;
    locator.dataSize = resolvedLocator.dataSize;

    if (locator.metadataOffset >= logicalEnd ||
        locator.metadataOffset + locator.metadataSize > logicalEnd) {
        META_LOG();
        return false;
    }

    if (locator.dataOffset >= logicalEnd ||
        locator.dataOffset + locator.dataSize > logicalEnd) {
        META_LOG();
        return false;
    }

    return true;
}

} // namespace MultiThreadedInstaller

