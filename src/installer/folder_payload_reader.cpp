#include "installer/folder_payload_reader.h"

#include "common/archive_types.h"
#include "common/utf8_utils.h"
#include "installer/installer_helpers.h"

#include <fstream>

namespace MultiThreadedInstaller {

namespace {

struct DataLocator {
    uint64_t dataOffset = 0;
    uint64_t dataSize = 0;
};

bool ReadExternalHeader(const std::string& dataPackagePath,
                        DataPackageHeader& header,
                        std::ifstream& file,
                        std::string& errorMessage) {
    file.open(PathFromUtf8(dataPackagePath), std::ios::binary);
    if (!file) {
        errorMessage = "Failed to open external data package: " + dataPackagePath;
        return false;
    }

    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != Constants::DATA_MAGIC_NUMBER) {
        errorMessage = "Invalid external data package header.";
        return false;
    }
    return true;
}

bool ResolveEmbeddedDataLocator(const std::string& executablePath,
                                std::ifstream& file,
                                DataLocator& locator,
                                std::string& errorMessage) {
    file.open(PathFromUtf8(executablePath), std::ios::binary);
    if (!file) {
        errorMessage = "Failed to open installer executable: " + executablePath;
        return false;
    }

    file.seekg(0, std::ios::end);
    const uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    uint64_t trailerEnd = fileSize;
    EmbeddedDataLocatorRecord record{};
    if (!findEmbeddedDataLocator(file, fileSize, trailerEnd, record)) {
        errorMessage = "Embedded payload locator not found.";
        return false;
    }

    locator.dataOffset = record.dataOffset;
    locator.dataSize = record.dataSize;
    return true;
}

} // namespace

FolderPayloadReader::FolderPayloadReader(std::string dataPackagePath)
    : dataPackagePath_(std::move(dataPackagePath)) {}

std::vector<uint8_t> FolderPayloadReader::readPayload(uint64_t offset,
                                                      uint64_t size,
                                                      std::string* errorMessage) const {
    std::string localError;
    std::ifstream file;
    uint64_t dataOffset = 0;
    uint64_t dataSize = 0;

    if (!dataPackagePath_.empty()) {
        DataPackageHeader header{};
        if (!ReadExternalHeader(dataPackagePath_, header, file, localError)) {
            if (errorMessage) {
                *errorMessage = localError;
            }
            return {};
        }
        dataOffset = header.dataOffset;
        dataSize = header.dataSize;
    } else {
        const std::string executablePath = getCurrentExecutablePath();
        if (executablePath.empty()) {
            if (errorMessage) {
                *errorMessage = "Failed to resolve current executable path.";
            }
            return {};
        }

        DataLocator locator{};
        if (!ResolveEmbeddedDataLocator(executablePath, file, locator, localError)) {
            if (errorMessage) {
                *errorMessage = localError;
            }
            return {};
        }
        dataOffset = locator.dataOffset;
        dataSize = locator.dataSize;
    }

    if (offset > dataSize || size > dataSize || offset + size > dataSize) {
        if (errorMessage) {
            *errorMessage = "Payload range is out of package bounds.";
        }
        return {};
    }

    const uint64_t absoluteOffset = dataOffset + offset;
    file.seekg(static_cast<std::streamoff>(absoluteOffset), std::ios::beg);
    if (!file) {
        if (errorMessage) {
            *errorMessage = "Failed to seek payload data.";
        }
        return {};
    }

    std::vector<uint8_t> payload(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(size));
    if (file.gcount() != static_cast<std::streamsize>(size)) {
        if (errorMessage) {
            *errorMessage = "Failed to read complete folder payload.";
        }
        return {};
    }

    return payload;
}

} // namespace MultiThreadedInstaller
