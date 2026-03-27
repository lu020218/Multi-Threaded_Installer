#include "packager/resource_embedder.h"

#include "packager/embedded_resource_table.h"
#include "packager/resource_zip_builder.h"

#include "common/utf8_utils.h"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

std::string ToResourceName(const std::string& prefix, const std::string& fileName) {
    std::string name = prefix + fileName;
    for (char& ch : name) {
        if (ch == '.') {
            ch = '_';
        } else {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
    }
    return name;
}

void AppendListEntry(std::vector<uint8_t>& installerTemplate,
                     const char* name,
                     const std::vector<std::string>& values,
                     bool& anyEmbedded) {
    if (values.empty()) {
        return;
    }

    std::string listText;
    for (const auto& value : values) {
        listText += value;
        listText += "\n";
    }

    const std::vector<uint8_t> listData(listText.begin(), listText.end());
    if (AppendEmbeddedRawEntry(installerTemplate, name, listData)) {
        anyEmbedded = true;
    }
}

} // namespace

bool AppendEmbeddedResources(std::vector<uint8_t>& installerTemplate,
                             const std::filesystem::path& resourceDir,
                             std::string& error) {
    error.clear();

    try {
        if (!std::filesystem::exists(resourceDir) || !std::filesystem::is_directory(resourceDir)) {
            error = "UI resources directory not found: " + Utf8FromPath(resourceDir);
            return false;
        }

        if (HasEmbeddedResourceTable(installerTemplate)) {
            std::cout << "Installer template already contains embedded resources; appending updated resources"
                      << std::endl;
        }

        bool anyEmbedded = false;
        bool useZip = false;
        std::vector<uint8_t> zipData;
        std::string zipError;
        if (BuildResourceZip(resourceDir, zipData, zipError) &&
            AppendEmbeddedRawEntry(installerTemplate, "RES_ZIP", zipData)) {
            std::cout << "  Embedded: resources.zip" << std::endl;
            anyEmbedded = true;
            useZip = true;
        }

        std::vector<std::string> imageNames;
        std::vector<std::string> langFiles;
        std::vector<std::string> licenseFiles;

        if (!useZip) {
            const std::filesystem::path skinsDir = resourceDir / "skins";
            if (std::filesystem::exists(skinsDir) && std::filesystem::is_directory(skinsDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(skinsDir)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".xml") {
                        continue;
                    }
                    const std::string fileName = Utf8FromPath(entry.path().filename());
                    if (AppendEmbeddedFileEntry(
                            installerTemplate, ToResourceName("XML_", fileName), entry.path())) {
                        std::cout << "  Embedded: " << fileName << std::endl;
                        anyEmbedded = true;
                    }
                }
            }

            const std::filesystem::path imagesDir = resourceDir / "images";
            if (std::filesystem::exists(imagesDir) && std::filesystem::is_directory(imagesDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(imagesDir)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    const std::string fileName = Utf8FromPath(entry.path().filename());
                    if (fileName.empty() || fileName.front() == '.') {
                        continue;
                    }
                    if (AppendEmbeddedFileEntry(
                            installerTemplate, ToResourceName("IMG_", fileName), entry.path())) {
                        std::cout << "  Embedded: " << fileName << std::endl;
                        anyEmbedded = true;
                        imageNames.push_back(fileName);
                    }
                }
            }

            const std::filesystem::path langDir = resourceDir / "lang";
            if (std::filesystem::exists(langDir) && std::filesystem::is_directory(langDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(langDir)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    const std::string fileName = Utf8FromPath(entry.path().filename());
                    if (fileName.empty() || fileName.front() == '.') {
                        continue;
                    }
                    if (AppendEmbeddedFileEntry(
                            installerTemplate, ToResourceName("LANG_", fileName), entry.path())) {
                        std::cout << "  Embedded: " << fileName << std::endl;
                        anyEmbedded = true;
                        langFiles.push_back(fileName);
                    }
                }
            }

            const std::filesystem::path licenseDir = resourceDir / "license";
            if (std::filesystem::exists(licenseDir) && std::filesystem::is_directory(licenseDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(licenseDir)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    const std::string fileName = Utf8FromPath(entry.path().filename());
                    if (fileName.empty() || fileName.front() == '.') {
                        continue;
                    }
                    if (AppendEmbeddedFileEntry(installerTemplate,
                                                ToResourceName("LICENSE_", fileName),
                                                entry.path())) {
                        std::cout << "  Embedded: license/" << fileName << std::endl;
                        anyEmbedded = true;
                        licenseFiles.push_back(fileName);
                    }
                }
            }
        }

        if (!useZip) {
            AppendListEntry(installerTemplate, "IMAGES_LIST", imageNames, anyEmbedded);
            AppendListEntry(installerTemplate, "LANG_LIST", langFiles, anyEmbedded);
            AppendListEntry(installerTemplate, "LICENSE_LIST", licenseFiles, anyEmbedded);

            const std::filesystem::path licensePath = resourceDir / "license.txt";
            if (std::filesystem::exists(licensePath) &&
                AppendEmbeddedFileEntry(installerTemplate, "LICENSE_TXT", licensePath)) {
                std::cout << "  Embedded: license.txt" << std::endl;
                anyEmbedded = true;
            }
        }

        if (!anyEmbedded) {
            error = zipError.empty() ? "No embeddable UI resources found in: " + Utf8FromPath(resourceDir)
                                     : zipError;
            return false;
        }

        AppendEmbeddedResourceMagic(installerTemplate);
        std::cout << "Embedded UI resources into installer template" << std::endl;
        return true;
    } catch (const std::exception& e) {
        error = std::string("Failed to embed UI resources: ") + e.what();
        return false;
    }
}

} // namespace MultiThreadedInstaller
