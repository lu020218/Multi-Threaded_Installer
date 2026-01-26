#pragma once

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

/**
 * 嵌入资源管理器
 * 负责在运行时提取嵌入在安装程序中的资源文件（XML布局、图片等）
 */
class EmbeddedResourceManager {
public:
    EmbeddedResourceManager();
    ~EmbeddedResourceManager();
    
    /**
     * 提取所有嵌入的资源到临时目录
     * @return 临时资源目录的路径，失败返回空字符串
     */
    std::string extractResources();
    
    /**
     * 清理提取的临时资源
     */
    void cleanup();
    
    /**
     * 获取资源目录路径
     */
    std::string getResourcePath() const { return m_resourcePath; }
    
private:
    std::string m_resourcePath;
    bool m_extracted;
    
    /**
     * 创建临时目录
     */
    std::string createTempDirectory();
    
    /**
     * 提取单个资源文件
     */
    bool extractFile(const std::string& relativePath, const std::vector<uint8_t>& data);
    
    /**
     * 获取嵌入的资源数据
     * 这些资源在打包时被嵌入到安装程序中
     */
    std::vector<uint8_t> getEmbeddedResource(const std::string& name);
    
    /**
     * 从文件末尾读取嵌入的资源（通过 embed_resources.ps1 添加的）
     */
    std::vector<uint8_t> readEmbeddedResourceFromFile(const std::string& name);
};

} // namespace MultiThreadedInstaller
