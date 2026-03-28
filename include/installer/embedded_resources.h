#pragma once

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/**
 *
 *
 */
class EmbeddedResourceManager {
public:
    EmbeddedResourceManager();
    ~EmbeddedResourceManager();
    
    /**
     *
     *
     */
    std::string extractResources();
    
    /**
     *
     */
    void cleanup();
    
    /**
     *
     */
    std::string getResourcePath() const { return m_resourcePath; }

    std::vector<uint8_t> getEmbeddedResource(const std::string& name);
    
private:
    std::string m_resourcePath;
    bool m_extracted;
    
    /**
     *
     */
    std::string createTempDirectory();
    
    /**
     *
     */
    bool extractFile(const std::string& relativePath, const std::vector<uint8_t>& data);
    
    /**
     *
     *
     */
    /**
     *
     */
    std::vector<uint8_t> readEmbeddedResourceFromFile(const std::string& name);
};

std::vector<uint8_t> LoadEmbeddedBinaryResource(const std::string& name);
bool ExtractEmbeddedBinaryResourceToFile(const std::string& name, const std::string& outputPath);

} // namespace MultiThreadedInstaller
