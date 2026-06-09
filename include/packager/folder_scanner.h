#pragma once

#include "common/config_types.h"
#include <vector>
#include <string>

namespace MultiThreadedInstaller {

/// 输入目录扫描器：把 --input 下的每个顶层子目录扫成一个 FolderInfo（id=目录名、文件清单、总大小）。
class FolderScanner {
public:
    FolderScanner() = default;
    ~FolderScanner() = default;

    /// 扫描 --input 顶层子目录，每个子目录产出一个 FolderInfo（递归收集其内文件）。
    std::vector<FolderInfo> scanInputDirectory(const std::string& inputPath);

    /// 校验扫描出的结构合法（非空、id 不重复等）。
    bool validateFolderStructure(const std::vector<FolderInfo>& folders);

private:
    void scanSingleFolder(const std::string& folderPath, FolderInfo& folderInfo);  ///< 递归扫描单个目录。
    size_t calculateFolderSize(const std::vector<std::string>& files);             ///< 累加文件大小。
    bool isDirectory(const std::string& path);        ///< 是否目录。
    bool isFileReadable(const std::string& filePath); ///< 文件是否可读。
};

} // namespace MultiThreadedInstaller
