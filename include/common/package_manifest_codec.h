#pragma once

#include "common/package_manifest.h"

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// PackageManifest 的二进制编解码（构建期序列化嵌入 exe，运行期反序列化）。
// 二进制版本由 Constants::VERSION 标识；安装器拒绝读版本不符的旧包。

/// 序列化为字节流（打包期写入安装器 exe 的元数据区）。
std::vector<uint8_t> SerializePackageManifest(const PackageManifest& manifest);

/// 反序列化字节流为 manifest。
/// @param deferFileIndex true 时跳过载荷里逐文件的 fileIndex（留空）——供只需文件夹/标量
///        元数据的调用方（如安装器 GUI 启动）加速；安装解压路径会以 false 重解析以拿到。
/// @return 成功返回 true；失败返回 false 并填充 error。
bool DeserializePackageManifest(const std::vector<uint8_t>& data,
                                PackageManifest& manifest,
                                std::string& error,
                                bool deferFileIndex = false);

} // namespace MultiThreadedInstaller
