#pragma once

#include <string>

namespace MultiThreadedInstaller {

int compareSemanticVersion(const std::string& lhs, const std::string& rhs);

// 将语义版本（可能含 -beta 等预发布后缀或 +build 元数据）转换为 Windows 版本资源
// 要求的纯数字四段式 "a.b.c.d"。例如 "7.0.0-beta1" -> "7.0.0.0"。
std::string toNumericVersion(const std::string& version);

} // namespace MultiThreadedInstaller
